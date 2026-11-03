import { GoogleGenAI } from "@google/genai";

import { prefixTexts, sortedTools, usage } from "./cache";
import { geminiModels } from "./catalogue";
import { type Client, type ClientOptions, messageInside } from "./client";
import { argumentsObject, schemaObject } from "./collector";
import { kNoKey } from "./egress";
import { kindOfStatus, ProviderError } from "./errors";
import type { Request, StopReason } from "./types";

type Json = Record<string, unknown>;

const kRefused = new Set([
    "SAFETY", "RECITATION", "LANGUAGE", "BLOCKLIST", "PROHIBITED_CONTENT", "SPII", "IMAGE_SAFETY",
    "IMAGE_PROHIBITED_CONTENT", "IMAGE_RECITATION", "ESCALATION",
]);
const kMalformed = new Set(["MALFORMED_FUNCTION_CALL", "UNEXPECTED_TOOL_CALL", "TOO_MANY_TOOL_CALLS", "MALFORMED_RESPONSE"]);

/** Gemini's history: a tool result goes back as a `functionResponse` named after its call, ids kept. */
function contents(request: Request): Json[] {
    const callNames = new Map<string, string>();
    const out: Json[] = [];
    for (const message of request.messages) {
        const parts: Json[] = [];
        for (const p of message.parts)
            if (p.type === "toolResult")
                parts.push({
                    functionResponse: {
                        ...(p.callId ? { id: p.callId } : {}),
                        name: callNames.get(p.callId) ?? "",
                        response: { [p.isError ? "error" : "result"]: p.content },
                    },
                });
        for (const p of message.parts) {
            if (p.type === "text" && p.text) {
                parts.push({ text: p.text });
            } else if (p.type === "image") {
                parts.push({ inlineData: { mimeType: p.mediaType, data: p.base64 } });
            } else if (p.type === "toolCall") {
                callNames.set(p.id, p.name);
                parts.push({
                    functionCall: { ...(p.id ? { id: p.id } : {}), name: p.name, args: argumentsObject(p.argumentsJson) },
                    ...(p.signature ? { thoughtSignature: p.signature } : {}),
                });
            }
        }
        if (parts.length)
            out.push({ role: message.author === "user" ? "user" : "model", parts });
    }
    return out;
}

/** The SDK builds `<baseUrl>/<apiVersion>/...`, so the base's last path segment is the API version. */
function httpBase(baseUrl: string): { baseUrl: string; apiVersion: string } {
    const url = new URL(baseUrl);
    const segments = url.pathname.replace(/\/+$/, "").split("/");
    const apiVersion = segments.pop() ?? "";
    url.pathname = segments.join("/") || "/";
    return { baseUrl: url.toString().replace(/\/+$/, ""), apiVersion };
}

function statusOf(error: unknown): number | undefined {
    const e = error as { name?: unknown; status?: unknown };
    return e?.name === "ApiError" && typeof e.status === "number" ? e.status : undefined;
}

/** Calls `seen` with each `data:` event's JSON as the body streams through. */
function tapped(fetchFn: typeof fetch, seen: (event: Json) => void): typeof fetch {
    const wrapped = async (...args: Parameters<typeof fetch>) => {
        const response = await fetchFn(...args);
        if (!response.body)
            return response;
        const decoder = new TextDecoder();
        let pending = "";
        const body = response.body.pipeThrough(new TransformStream<Uint8Array, Uint8Array>({
            transform(chunk, controller) {
                pending += decoder.decode(chunk, { stream: true });
                const lines = pending.split(/\r\n|\r|\n/);
                pending = lines.pop() ?? "";
                for (const line of lines) {
                    if (!line.startsWith("data:"))
                        continue;
                    try {
                        seen(JSON.parse(line.slice(5)) as Json);
                    } catch {}
                }
                controller.enqueue(chunk);
            },
        }));
        return new Response(body, { status: response.status, statusText: response.statusText, headers: response.headers });
    };
    return wrapped as typeof fetch;
}

/** The system prompt and context as `systemInstruction`, then the tools by name: Gemini's cacheable prefix. */
function prefix(request: Request): Json {
    const texts = prefixTexts(request);
    const tools = sortedTools(request).map((t) => ({
        name: t.name, description: t.description, parametersJsonSchema: schemaObject(t.parametersJson),
    }));
    return {
        ...(texts.length ? { systemInstruction: { parts: texts.map((text) => ({ text })) } } : {}),
        ...(tools.length ? { tools: [{ functionDeclarations: tools }] } : {}),
    };
}

function failure(error: unknown): unknown {
    const status = statusOf(error);
    return status === undefined ? error : new ProviderError(kindOfStatus(status), messageInside((error as Error).message), status);
}

export function geminiClient({ config, key, fetch }: ClientOptions): Client {
    let finishMessage = "";
    // @google/genai drops a candidate's finishMessage, so it is read from the raw stream.
    const raw = tapped(fetch, (event) => {
        const message = ((event.candidates as Json[] | undefined)?.[0])?.finishMessage;
        if (typeof message === "string")
            finishMessage = message;
    });
    const ai = new GoogleGenAI({
        apiKey: key || kNoKey,
        vertexai: false,
        httpOptions: { ...httpBase(config.baseUrl), fetch: raw },
    });
    return {
        async models(signal) {
            const pager = await ai.models.list({ config: { pageSize: 1000, abortSignal: signal } });
            return geminiModels(pager.page as unknown[]);
        },
        async createCache(request, ttlSeconds, signal) {
            try {
                const cache = await ai.caches.create({
                    model: request.model,
                    config: { abortSignal: signal, ttl: `${ttlSeconds}s`, ...prefix(request) } as never,
                });
                if (!cache.name)
                    throw new ProviderError("malformed", "the provider made a cache without a name");
                return cache.name;
            } catch (error) {
                throw failure(error);
            }
        },
        async deleteCache(name, signal) {
            try {
                await ai.caches.delete({ name, config: { abortSignal: signal } });
            } catch (error) {
                throw failure(error);
            }
        },
        async complete(request, out, signal, cache) {
            let stop: StopReason | undefined;
            try {
                const stream = await ai.models.generateContentStream({
                    model: request.model,
                    contents: contents(request) as never,
                    config: {
                        abortSignal: signal,
                        ...(cache ? { cachedContent: cache } : prefix(request) as never),
                        ...(request.maxOutputTokens !== undefined ? { maxOutputTokens: request.maxOutputTokens } : {}),
                    },
                });
                for await (const chunk of stream as AsyncIterable<Json>) {
                    const u = chunk.usageMetadata as Json | undefined;
                    if (u && Object.keys(u).length) {
                        out.usage = usage(
                            Number(u.promptTokenCount ?? 0) + Number(u.toolUsePromptTokenCount ?? 0),
                            Number(u.candidatesTokenCount ?? 0) + Number(u.thoughtsTokenCount ?? 0),
                            { cachedInputTokens: Number(u.cachedContentTokenCount ?? 0) });
                    }
                    if ((chunk.promptFeedback as Json | undefined)?.blockReason)
                        return "refused";
                    const candidate = ((chunk.candidates as Json[] | undefined) ?? [])[0] ?? {};
                    for (const part of (((candidate.content as Json | undefined)?.parts as Json[] | undefined) ?? [])) {
                        if (part.thought)
                            continue;
                        const call = part.functionCall as Json | undefined;
                        if (call) {
                            const index = out.callCount;
                            out.startCall(index, String(call.id || `call_${index}`), String(call.name ?? ""));
                            out.addArguments(index, JSON.stringify(call.args ?? {}));
                            out.setSignature(index, typeof part.thoughtSignature === "string" ? part.thoughtSignature : undefined);
                        } else {
                            out.addText(typeof part.text === "string" ? part.text : undefined);
                        }
                    }
                    const reason = typeof candidate.finishReason === "string" ? candidate.finishReason : "";
                    if (!reason)
                        continue;
                    const detail = finishMessage || reason;
                    if (reason === "MAX_TOKENS" || reason === "CONTINUATION")
                        stop = "maxTokens";
                    else if (kRefused.has(reason))
                        stop = "refused";
                    else if (kMalformed.has(reason))
                        throw new ProviderError("malformed", detail);
                    else if (reason === "MISSING_THOUGHT_SIGNATURE")
                        throw new ProviderError("rejected", detail);
                    else if (reason === "PUP_LIMITED_DISABLED")
                        throw new ProviderError("unauthorized", detail);
                    else
                        stop = out.callCount > 0 ? "toolUse" : "end";
                }
            } catch (error) {
                throw failure(error);
            }
            if (!stop)
                throw new ProviderError("malformed", "the stream ended before the response finished");
            return stop;
        },
    };
}
