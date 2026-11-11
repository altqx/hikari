import type { CacheLifetime, Request, ToolDefinition, Usage } from "./types";

type Json = Record<string, unknown>;

function namesIn(searchResult: string): string[] {
    try {
        const parsed: unknown = JSON.parse(searchResult);
        const tools = (parsed as { tools?: unknown } | null)?.tools;
        return Array.isArray(tools)
            ? tools.map((t) => (t as { name?: unknown } | null)?.name).filter((n): n is string => typeof n === "string")
            : [];
    } catch {
        return [];
    }
}

/** For each search_tools call in the history, the tools its result named. */
export function searchResults(request: Request): Map<string, string[]> {
    const searches = new Set<string>();
    const found = new Map<string, string[]>();
    for (const message of request.messages)
        for (const part of message.parts)
            if (part.type === "toolCall" && part.name === "search_tools")
                searches.add(part.id);
            else if (part.type === "toolResult" && !part.isError && searches.has(part.callId))
                found.set(part.callId, namesIn(part.content));
    return found;
}

/** The request's tools that search_tools found, in the order it found them. */
export function foundTools(request: Request): ToolDefinition[] {
    const order = [...new Set([...searchResults(request).values()].flat())];
    return (request.tools ?? []).filter((t) => order.includes(t.name))
        .sort((a, b) => order.indexOf(a.name) - order.indexOf(b.name));
}

/**
 * The tools sent from the start, by name, so the same set is the same bytes every turn. Those search_tools found
 * aren't among them: they follow, in `foundTools`.
 */
export function prefixTools(request: Request): ToolDefinition[] {
    const found = new Set(foundTools(request).map((t) => t.name));
    return (request.tools ?? []).filter((t) => !found.has(t.name))
        .sort((a, b) => (a.name < b.name ? -1 : a.name > b.name ? 1 : 0));
}

/** The prefix tools, then the found ones, so finding a tool leaves the prefix as it was. */
export function sortedTools(request: Request): ToolDefinition[] {
    return [...prefixTools(request), ...foundTools(request)];
}

/** The system prompt and the pinned context, the text half of the stable prefix. */
export function prefixTexts(request: Request): string[] {
    return [request.system ?? "", request.context ?? ""].filter((text) => text !== "");
}

/** The request as sent: with caching off, without the cache key and explicit cache. */
export function withCaching(request: Request): Request {
    if (request.caching !== false)
        return request;
    const { cacheKey: _key, explicitCache: _explicit, cacheLifetime: _lifetime, ...rest } = request;
    return rest;
}

export const marksCache = (request: Request) => request.caching !== false;

export function cacheControl(lifetime: CacheLifetime | undefined): Json {
    return lifetime === "hour" ? { type: "ephemeral", ttl: "1h" } : { type: "ephemeral" };
}

export function markLast(blocks: Json[], control: Json): void {
    const last = blocks.at(-1);
    if (last)
        last.cache_control = control;
}

export function usage(inputTokens: number, outputTokens: number, cached: Partial<Usage> = {}): Usage {
    return {
        inputTokens,
        outputTokens,
        cachedInputTokens: cached.cachedInputTokens ?? 0,
        cacheWriteTokens: cached.cacheWriteTokens ?? 0,
        cacheWriteHourTokens: cached.cacheWriteHourTokens ?? 0,
    };
}
