
import { cacheControl, foundTools, markLast, marksCache, prefixTexts, prefixTools, searchResults } from "./cache";
import { catalogueModels } from "./catalogue";
import { type Client, type ClientOptions, messageInside } from "./client";
import { argumentsObject, schemaObject } from "./collector";
import { kKeyPlaceholder } from "./relay";
import { ProviderError, wrapsProviderError } from "./errors";
import { sdk } from "./sdks";
import { nativeSearch, providerState } from "./search";
import type { ErrorKind, ProviderConfig, Request, StopReason, ToolDefinition, Usage, WebAction } from "./types";

type Json = Record<string, unknown>;

const kDefaultMaxTokens = 8192;
const kSearchUses = 5;

/** web_search_20250305 and web_fetch_20250910 are the versions that need no code execution, so ZDR keeps them. */
const kSearchTools: Json[] = [
    { type: "web_search_20250305", name: "web_search", max_uses: kSearchUses },
    { type: "web_fetch_20250910", name: "web_fetch", max_uses: kSearchUses },
];

const kStops: Record<string, StopReason> = { tool_use: "toolUse", max_tokens: "maxTokens", refusal: "refused" };

const kErrorKinds: Record<string, ErrorKind> = {
    rate_limit_error: "rateLimited",
    authentication_error: "unauthorized",
    permission_error: "unauthorized",
    invalid_request_error: "rejected",
};

function webAction(block: Json): WebAction {
    const input = (block.input ?? {}) as Json;
    return block.name === "web_fetch"
        ? { kind: "read", ...(typeof input.url === "string" ? { url: input.url } : {}) }
        : { kind: "search", ...(typeof input.query === "string" ? { query: input.query } : {}) };
}

/**
 * Whether search_tools can load found tools as `tool_reference` blocks, which keeps the cache: Anthropic's own API on
 * Claude 4.5 and later (https://platform.claude.com/docs/en/agents-and-tools/tool-use/tool-search-tool#model-compatibility).
 */
export function loadsToolReferences(config: ProviderConfig, model: string): boolean {
    const version = /^claude-[a-z]+-(\d+)(?:-(\d{1,2})(?=-|$))?/.exec(model);
    if (config.preset !== "anthropic" || !version)
        return false;
    const major = Number(version[1]), minor = Number(version[2] ?? 0);
    return major > 4 || (major === 4 && minor >= 5);
}

function definition(tool: ToolDefinition): Json {
    return { name: tool.name, description: tool.description, input_schema: schemaObject(tool.parametersJson) };
}

/** Each search_tools result as `tool_reference` blocks for the found tools the request sends. */
function toolReferences(request: Request): Map<string, Json[]> {
    const sent = new Set((request.tools ?? []).map((t) => t.name));
    const references = new Map<string, Json[]>();
    for (const [callId, names] of searchResults(request)) {
        const blocks = names.filter((name) => sent.has(name)).map((name) => ({ type: "tool_reference", tool_name: name }));
        if (blocks.length)
            references.set(callId, blocks);
    }
    return references;
}

function messages(request: Request, references = new Map<string, Json[]>()): Json[] {
    const out: Json[] = [];
    for (const message of request.messages) {
        const kept = message.author === "assistant" ? providerState(message, "anthropic-messages") : undefined;
        if (kept) {
            out.push({ role: "assistant", content: kept });
            continue;
        }
        const content: Json[] = [];
        if (message.author === "user")
            for (const p of message.parts)
                if (p.type === "toolResult")
                    content.push({ type: "tool_result", tool_use_id: p.callId, content: references.get(p.callId) ?? p.content,
                        is_error: p.isError ?? false });
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
            const tools: Json[] = prefixTools(request).map(definition);
            const deferring = tools.length > 0 && loadsToolReferences(config, request.model);
            const body: Json = {
                model: request.model,
                max_tokens: request.maxOutputTokens ?? kDefaultMaxTokens,
                messages: messages(request, deferring ? toolReferences(request) : undefined),
                stream: true,
            };
            const system: Json[] = prefixTexts(request).map((text) => ({ type: "text", text }));
            if (marksCache(request))
                markLast(system.length ? system : tools, cacheControl(request.cacheLifetime));
            tools.push(...foundTools(request).map((t) => ({ ...definition(t), ...(deferring ? { defer_loading: true } : {}) })));
            if (nativeSearch(config, request))
                tools.push(...kSearchTools);
            if (system.length)
                body.system = system;
            if (tools.length)
                body.tools = tools;
            let stop: StopReason | undefined;
            const blocks: Json[] = [];
            const inputs = new Map<number, string>();
            const textStarts = new Map<number, number>();
            const cited = new Map<number, [string, string, string | undefined][]>();
            const placeCitations = (index: number) => {
                const start = textStarts.get(index);
                for (const [url, title, quote] of cited.get(index) ?? [])
                    out.addCitation(url, title, quote, start === undefined ? undefined
                        : { unit: "utf16", base: start, start: 0, end: out.textLength - start });
                cited.delete(index);
            };
            let searched = false;
            const finish = (reason: StopReason) => {
                if (searched)
                    out.keepProviderState("anthropic-messages", blocks.filter(Boolean));
                return reason;
            };
            try {
                const stream = await client.messages.create(body as never, { signal });
                for await (const event of stream as unknown as AsyncIterable<Json>) {
                    switch (event.type) {
                    case "message_start":
                        readUsage(out.usage, (event.message as Json | undefined)?.usage as Json | undefined);
                        break;
                    case "content_block_start": {
                        const index = Number(event.index ?? 0);
                        const block = structuredClone((event.content_block ?? {}) as Json);
                        blocks[index] = block;
                        if (block.type === "server_tool_use")
                            searched = true;
                        else if (block.type === "web_search_tool_result" && Array.isArray(block.content))
                            out.addFound((block.content as Json[]).map((r) => String(r.url ?? "")));
                        if (block.type === "tool_use")
                            out.startCall(index, String(block.id ?? ""), String(block.name ?? ""));
                        else if (block.type === "text") {
                            textStarts.set(index, out.textLength);
                            out.addText(String(block.text ?? ""));
                        }
                        break;
                    }
                    case "content_block_delta": {
                        const index = Number(event.index ?? 0);
                        const block = blocks[index] ?? {};
                        const delta = (event.delta ?? {}) as Json;
                        if (delta.type === "text_delta") {
                            block.text = String(block.text ?? "") + String(delta.text ?? "");
                            out.addText(String(delta.text ?? ""));
                        } else if (delta.type === "input_json_delta") {
                            inputs.set(index, (inputs.get(index) ?? "") + String(delta.partial_json ?? ""));
                            out.addArguments(index, String(delta.partial_json ?? ""));
                        } else if (delta.type === "citations_delta") {
                            const citation = (delta.citation ?? {}) as Json;
                            block.citations = [...((block.citations as Json[] | null | undefined) ?? []), citation];
                            if (citation.type === "web_search_result_location")
                                cited.set(index, [...(cited.get(index) ?? []), [String(citation.url ?? ""),
                                    String(citation.title ?? ""),
                                    typeof citation.cited_text === "string" ? citation.cited_text : undefined]]);
                        }
                        break;
                    }
                    case "content_block_stop": {
                        const index = Number(event.index ?? 0);
                        const input = inputs.get(index);
                        if (input !== undefined && blocks[index])
                            blocks[index].input = argumentsObject(input);
                        if (blocks[index]?.type === "server_tool_use")
                            out.addSearch(webAction(blocks[index]));
                        placeCitations(index);
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
                        for (const index of [...cited.keys()])
                            placeCitations(index);
                        return finish(stop ?? "end");
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
