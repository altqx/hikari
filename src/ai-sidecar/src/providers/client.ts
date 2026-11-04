import type { Collector } from "./collector";
import type { Model, ProviderConfig, Request, StopReason } from "./types";

/** One provider, reached through its official SDK and the relay fetch. */
export interface Client {
    /**
     * Streams `request` into `out`; throws a ProviderError, or anything `providerErrorOf` maps. With `cache`, the
     * provider's explicit cache already holds the request's system, tools and context, which then stay out.
     */
    complete(request: Request, out: Collector, signal: AbortSignal, cache?: string): Promise<StopReason>;
    models(signal: AbortSignal): Promise<Model[]>;
    /** Stores the request's system, tools and context on the provider; answers the cache's name. */
    createCache?(request: Request, ttlSeconds: number, signal: AbortSignal): Promise<string>;
    deleteCache?(name: string, signal: AbortSignal): Promise<void>;
}

export interface ClientOptions {
    config: ProviderConfig;
    fetch: typeof fetch;
}

/** The provider's message when the SDK wrapped it as `got status: N. {json}` or `N {json}`. */
export function messageInside(text: string): string {
    const start = text.indexOf("{");
    if (start < 0)
        return text;
    try {
        const parsed = JSON.parse(text.slice(start)) as { error?: { message?: unknown }; message?: unknown };
        const message = parsed.error?.message ?? parsed.message;
        return typeof message === "string" ? message : text;
    } catch {
        return text;
    }
}
