import type { CacheLifetime, Request, ToolDefinition, Usage } from "./types";

type Json = Record<string, unknown>;

/** Tools by name, so the same set is the same bytes every turn. */
export function sortedTools(request: Request): ToolDefinition[] {
    return [...(request.tools ?? [])].sort((a, b) => (a.name < b.name ? -1 : a.name > b.name ? 1 : 0));
}

/** The system prompt and the pinned context, the text half of the stable prefix. */
export function prefixTexts(request: Request): string[] {
    return [request.system ?? "", request.context ?? ""].filter((text) => text !== "");
}

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
