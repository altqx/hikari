import OpenAI, { APIError } from "openai";

import { cacheControl, markLast, prefixTexts, sortedTools, usage } from "./cache";
import { catalogueModels } from "./catalogue";
import { type Client, type ClientOptions, messageInside } from "./client";
import { type Collector, schemaObject } from "./collector";
import { kKeyPlaceholder } from "./relay";
import { ProviderError, streamError, wrapsProviderError } from "./errors";
import type { Message, Part, ProviderConfig, Request, StopReason } from "./types";

type Json = Record<string, unknown>;

const dataUrl = (part: Extract<Part, { type: "image" }>) => `data:${part.mediaType};base64,${part.base64}`;

function sdk({ config, fetch }: ClientOptions): OpenAI {
    const openRouter = config.preset === "openrouter";
    return new OpenAI({
        apiKey: kKeyPlaceholder,
        baseURL: config.baseUrl,
        fetch,
        maxRetries: 0,
        organization: null,
        project: null,
        defaultHeaders: openRouter ? { "HTTP-Referer": "https://hikarisub.org", "X-Title": "HikariSub" } : {},
    });
}

async function models(client: OpenAI, signal: AbortSignal) {
    const body = await client.get<{ data?: unknown }>("/models", { signal });
    return catalogueModels(Array.isArray(body?.data) ? body.data : []);
}

/** OpenRouter caches these vendors' models only where the request marks the prefix. */
function takesCacheMarks(config: ProviderConfig, model: string): boolean {
    return config.preset === "openrouter" && (model.startsWith("anthropic/") || model.startsWith("google/"));
}

const detail = (value: unknown, key: string) => Number((value as Json | undefined)?.[key] ?? 0);

function chatMessages(request: Request, markCache: boolean): Json[] {
    const out: Json[] = [];
    const prefix: Json[] = prefixTexts(request).map((text) => ({ type: "text", text }));
    if (markCache)
        markLast(prefix, cacheControl(request.cacheLifetime));
    for (const block of prefix)
        out.push({ role: "system", content: block.cache_control ? [block] : block.text });
    for (const message of request.messages) {
        if (message.author === "assistant") {
            const text = message.parts.flatMap((p) => (p.type === "text" ? [p.text] : [])).join("");
            const calls = message.parts.flatMap((p) => (p.type === "toolCall"
                ? [{ id: p.id, type: "function", function: { name: p.name, arguments: p.argumentsJson } }]
                : []));
            out.push({ role: "assistant", content: text || null, ...(calls.length ? { tool_calls: calls } : {}) });
            continue;
        }
        for (const p of message.parts)
            if (p.type === "toolResult")
                out.push({ role: "tool", tool_call_id: p.callId, content: p.content });
        const content = message.parts.flatMap((p): Json[] => {
            if (p.type === "text")
                return [{ type: "text", text: p.text }];
            if (p.type === "image")
                return [{ type: "image_url", image_url: { url: dataUrl(p) } }];
            return [];
        });
        if (content.length)
            out.push({ role: "user", content });
    }
    if (markCache) {
        const lastUser = out.findLast((m) => m.role === "user");
        if (lastUser)
            markLast(lastUser.content as Json[], cacheControl(request.cacheLifetime));
    }
    return out;
}

const kChatStops: Record<string, StopReason> = {
    stop: "end", tool_calls: "toolUse", function_call: "toolUse", length: "maxTokens", content_filter: "refused",
};

function chatClient(options: ClientOptions): Client {
    const client = sdk(options);
    return {
        models: (signal) => models(client, signal),
        async complete(request, out, signal) {
            const body: Json = {
                model: request.model,
                messages: chatMessages(request, takesCacheMarks(options.config, request.model)),
                stream: true,
                stream_options: { include_usage: true },
            };
            if (options.config.preset === "openrouter") {
                body.usage = { include: true };
                if (request.cacheKey)
                    body.prompt_cache_key = request.cacheKey;
            }
            if (request.maxOutputTokens !== undefined)
                body.max_tokens = request.maxOutputTokens;
            if (request.tools?.length)
                body.tools = sortedTools(request).map((t) => ({
                    type: "function",
                    function: { name: t.name, description: t.description, parameters: schemaObject(t.parametersJson) },
                }));
            let stop: StopReason | undefined;
            try {
                const stream = await client.chat.completions.create(body as never, { signal });
                for await (const chunk of stream as unknown as AsyncIterable<Json>) {
                    const u = chunk.usage as Json | null | undefined;
                    if (u) {
                        out.usage = usage(Number(u.prompt_tokens ?? 0), Number(u.completion_tokens ?? 0), {
                            cachedInputTokens: detail(u.prompt_tokens_details, "cached_tokens"),
                            cacheWriteTokens: detail(u.prompt_tokens_details, "cache_write_tokens"),
                        });
                        if (typeof u.cost === "number")
                            out.usage.reportedCost = u.cost;
                    }
                    const choice = ((chunk.choices as Json[] | undefined) ?? [])[0];
                    if (!choice)
                        continue;
                    const delta = (choice.delta ?? {}) as Json;
                    out.addText(typeof delta.content === "string" ? delta.content : undefined);
                    for (const call of (delta.tool_calls as Json[] | undefined) ?? []) {
                        const index = Number(call.index ?? 0);
                        const fn = (call.function ?? {}) as Json;
                        if ("id" in call || "name" in fn)
                            out.startCall(index, String(call.id ?? ""), String(fn.name ?? ""));
                        out.addArguments(index, typeof fn.arguments === "string" ? fn.arguments : undefined);
                    }
                    const reason = typeof choice.finish_reason === "string" ? kChatStops[choice.finish_reason] : undefined;
                    if (reason)
                        stop = reason;
                }
            } catch (error) {
                if (error instanceof APIError && error.status === undefined && !wrapsProviderError(error))
                    throw streamError(error.error);
                throw error;
            }
            if (!stop)
                throw new ProviderError("malformed", "the stream ended before the response finished");
            return stop;
        },
    };
}

function responsesInput(messages: Message[], context: string | undefined): Json[] {
    const input: Json[] = [];
    if (context)
        input.push({ role: "developer", content: [{ type: "input_text", text: context }] });
    for (const message of messages) {
        if (message.author === "assistant") {
            for (const p of message.parts) {
                if (p.type === "text")
                    input.push({ role: "assistant", content: [{ type: "output_text", text: p.text }] });
                else if (p.type === "toolCall")
                    input.push({ type: "function_call", call_id: p.id, name: p.name, arguments: p.argumentsJson });
            }
            continue;
        }
        for (const p of message.parts)
            if (p.type === "toolResult")
                input.push({ type: "function_call_output", call_id: p.callId, output: p.content });
        const content = message.parts.flatMap((p): Json[] => {
            if (p.type === "text")
                return [{ type: "input_text", text: p.text }];
            if (p.type === "image")
                return [{ type: "input_image", image_url: dataUrl(p) }];
            return [];
        });
        if (content.length)
            input.push({ role: "user", content });
    }
    return input;
}

function responsesClient(options: ClientOptions): Client {
    const client = sdk(options);
    return {
        models: (signal) => models(client, signal),
        async complete(request, out, signal) {
            const body: Json = {
                model: request.model, input: responsesInput(request.messages, request.context), stream: true, store: false,
            };
            if (request.system)
                body.instructions = request.system;
            if (options.config.preset === "openai" && request.cacheKey)
                body.prompt_cache_key = request.cacheKey;
            if (request.maxOutputTokens !== undefined)
                body.max_output_tokens = request.maxOutputTokens;
            if (request.tools?.length)
                body.tools = sortedTools(request).map((t) => ({
                    type: "function", name: t.name, description: t.description,
                    parameters: schemaObject(t.parametersJson), strict: false,
                }));
            let hasCalls = false;
            try {
                const stream = await client.responses.create(body as never, { signal });
                for await (const event of stream as unknown as AsyncIterable<Json>) {
                    switch (event.type) {
                    case "response.output_text.delta":
                        out.addText(String(event.delta ?? ""));
                        break;
                    case "response.output_item.added": {
                        const item = (event.item ?? {}) as Json;
                        if (item.type === "function_call") {
                            hasCalls = true;
                            out.startCall(Number(event.output_index ?? 0), String(item.call_id ?? ""), String(item.name ?? ""));
                        }
                        break;
                    }
                    case "response.function_call_arguments.delta":
                        out.addArguments(Number(event.output_index ?? 0), String(event.delta ?? ""));
                        break;
                    case "response.completed":
                    case "response.incomplete": {
                        const response = (event.response ?? {}) as Json;
                        const u = (response.usage ?? {}) as Json;
                        out.usage = usage(Number(u.input_tokens ?? 0), Number(u.output_tokens ?? 0), {
                            cachedInputTokens: detail(u.input_tokens_details, "cached_tokens"),
                            cacheWriteTokens: detail(u.input_tokens_details, "cache_write_tokens"),
                        });
                        const incomplete = (response.incomplete_details as Json | undefined)?.reason;
                        if (incomplete === "max_output_tokens")
                            return "maxTokens";
                        if (incomplete === "content_filter")
                            return "refused";
                        return hasCalls ? "toolUse" : "end";
                    }
                    case "response.failed": {
                        const e = ((event.response as Json | undefined)?.error ?? {}) as Json;
                        throw new ProviderError("server", String(e.message ?? ""));
                    }
                    }
                }
            } catch (error) {
                if (error instanceof APIError && error.status === undefined && !wrapsProviderError(error)) {
                    const e = (error.error ?? {}) as Json;
                    const code = String(e.code ?? "");
                    const limited = code.includes("rate_limit") || code.includes("usage_limit");
                    throw new ProviderError(limited ? "rateLimited" : "server",
                        typeof e.message === "string" ? e.message : messageInside(error.message));
                }
                throw error;
            }
            throw new ProviderError("malformed", "the stream ended before the response finished");
        },
    };
}

export function openAiClient(options: ClientOptions): Client {
    return options.config.wire === "responses" ? responsesClient(options) : chatClient(options);
}
