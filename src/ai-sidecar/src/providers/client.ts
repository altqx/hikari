import type { Collector } from "./collector";
import type { Model, ProviderConfig, Request, StopReason } from "./types";

/** One provider, reached through its official SDK and the egress fetch. */
export interface Client {
    /** Streams `request` into `out`; throws a ProviderError, or anything `providerErrorOf` maps. */
    complete(request: Request, out: Collector, signal: AbortSignal): Promise<StopReason>;
    models(signal: AbortSignal): Promise<Model[]>;
}

export interface ClientOptions {
    config: ProviderConfig;
    /** Empty for a provider that needs none. */
    key: string;
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
