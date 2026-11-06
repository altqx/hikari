
import { cacheControl, markLast, marksCache, prefixTexts, sortedTools } from "./cache";
import { catalogueModels } from "./catalogue";
import { type Client, type ClientOptions, messageInside } from "./client";
import { argumentsObject, schemaObject } from "./collector";
import { kKeyPlaceholder } from "./relay";
import { ProviderError, wrapsProviderError } from "./errors";
import { sdk } from "./sdks";
import type { ErrorKind, Request, StopReason, Usage } from "./types";

type Json = Record<string, unknown>;

const kDefaultMaxTokens = 8192;

const kStops: Record<string, StopReason> = { tool_use: "toolUse", max_tokens: "maxTokens", refusal: "refused" };

const kErrorKinds: Record<string, ErrorKind> = {
    rate_limit_error: "rateLimited",
    authentication_error: "unauthorized",
    permission_error: "unauthorized",
    invalid_request_error: "rejected",
};

function messages(request: Request): Json[] {
    const out: Json[] = [];
    for (const message of request.messages) {
        const content: Json[] = [];
        if (message.author === "user")
            for (const p of message.parts)
                if (p.type === "toolResult")
                    content.push({ type: "tool_result", tool_use_id: p.callId, content: p.content, is_error: p.isError ?? false });
        for (const p of message.parts) {
            if (p.type === "text" && p.text)
                content.push({ type: "text", text: p.text });
            else if (p.type === "image")
                content.push({ type: "image", source: { type: "base64", media_type: p.mediaType, data: p.base64 } });
            else if (p.type === "toolCall")
                content.push({ type: "tool_use", id: p.id, name: p.name, input: argumentsObject(p.argumentsJson) });
        }
        if (content.length)
            out.push({ role: message.author, content });
    }
    const last = out.at(-1);
    if (last && marksCache(request))
        markLast(last.content as Json[], cacheControl(request.cacheLifetime));
    return out;
}

function readUsage(usage: Usage, u: Json | undefined): void {
    if (!u)
        return;
    if (typeof u.input_tokens === "number") {
        const read = Number(u.cache_read_input_tokens ?? 0);
        const written = Number(u.cache_creation_input_tokens ?? 0);
        usage.inputTokens = u.input_tokens + read + written;
        usage.cachedInputTokens = read;
        usage.cacheWriteTokens = written;
        usage.cacheWriteHourTokens = Number((u.cache_creation as Json | undefined)?.ephemeral_1h_input_tokens ?? 0);
    }
    if (typeof u.output_tokens === "number")
        usage.outputTokens = u.output_tokens;
}

/** The SDK appends `/v1/...` itself, so a base ending in `/v1` loses it. */
function sdkBase(baseUrl: string): string {
    return baseUrl.replace(/\/+$/, "").replace(/\/v1$/, "");
}

export function anthropicClient({ config, fetch }: ClientOptions): Client {
    const client = new (sdk("anthropic").default)({
        apiKey: kKeyPlaceholder,
        // The SDK sends the key as a bearer token; Anthropic-compatible servers expect x-api-key.
        defaultHeaders: { "x-api-key": kKeyPlaceholder, Authorization: null },
        baseURL: sdkBase(config.baseUrl),
        fetch,
        maxRetries: 0,
    });
    return {
        async models(signal) {
            const body = await client.get<{ data?: unknown }>("/v1/models", { query: { limit: 1000 }, signal });
            return catalogueModels(Array.isArray(body?.data) ? body.data : []);
        },
        async complete(request, out, signal) {
            const body: Json = {
                model: request.model,
                max_tokens: request.maxOutputTokens ?? kDefaultMaxTokens,
                messages: messages(request),
                stream: true,
            };
            const tools: Json[] = sortedTools(request).map((t) => ({
                name: t.name, description: t.description, input_schema: schemaObject(t.parametersJson),
            }));
            const system: Json[] = prefixTexts(request).map((text) => ({ type: "text", text }));
            if (marksCache(request))
                markLast(system.length ? system : tools, cacheControl(request.cacheLifetime));
            if (system.length)
                body.system = system;
            if (tools.length)
                body.tools = tools;
            let stop: StopReason | undefined;
            try {
                const stream = await client.messages.create(body as never, { signal });
                for await (const event of stream as unknown as AsyncIterable<Json>) {
                    switch (event.type) {
                    case "message_start":
                        readUsage(out.usage, (event.message as Json | undefined)?.usage as Json | undefined);
                        break;
                    case "content_block_start": {
                        const block = (event.content_block ?? {}) as Json;
                        if (block.type === "tool_use")
                            out.startCall(Number(event.index ?? 0), String(block.id ?? ""), String(block.name ?? ""));
                        else if (block.type === "text")
                            out.addText(String(block.text ?? ""));
                        break;
                    }
                    case "content_block_delta": {
                        const delta = (event.delta ?? {}) as Json;
                        if (delta.type === "text_delta")
                            out.addText(String(delta.text ?? ""));
                        else if (delta.type === "input_json_delta")
                            out.addArguments(Number(event.index ?? 0), String(delta.partial_json ?? ""));
                        break;
                    }
                    case "message_delta": {
                        readUsage(out.usage, event.usage as Json | undefined);
                        const reason = (event.delta as Json | undefined)?.stop_reason;
                        if (typeof reason === "string" && reason)
                            stop = kStops[reason] ?? "end";
                        break;
                    }
                    case "message_stop":
                        return stop ?? "end";
                    }
                }
            } catch (error) {
                if (error instanceof sdk("anthropic").APIError && error.status === undefined && !wrapsProviderError(error)) {
                    const e = ((error.error as Json | undefined)?.error ?? {}) as Json;
                    throw new ProviderError(kErrorKinds[String(e.type)] ?? "server",
                        typeof e.message === "string" ? e.message : messageInside(error.message));
                }
                throw error;
            }
            throw new ProviderError("malformed", "the stream ended before the response finished");
        },
    };
}
