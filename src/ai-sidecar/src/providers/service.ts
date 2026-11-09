import { anthropicClient } from "./anthropic";
import { prefixTexts, sortedTools, withCaching } from "./cache";
import type { Client, ClientOptions } from "./client";
import { Collector } from "./collector";
import { ProviderError, providerErrorOf } from "./errors";
import { geminiClient } from "./gemini";
import { openAiClient } from "./openai";
import { type Relay, relayFetch } from "./relay";
import { kSchemas, type Schema, validate } from "./schema";
import type { CompleteParams, ConversationEndParams, Finished, ModelsListParams, ModelsListResult, ProviderConfig,
    ProvidersSetParams, Request, SearchService, Update } from "./types";

export const kIdleTimeoutMs = 2 * 60 * 1000;

export class InvalidParams extends Error {}

export function checked<T>(schema: Schema, params: unknown): T {
    const broken = validate(schema, params);
    if (broken)
        throw new InvalidParams(broken);
    return params as T;
}

function clientFor(options: ClientOptions): Client {
    switch (options.config.wire) {
    case "anthropic-messages":
        return anthropicClient(options);
    case "gemini":
        return geminiClient(options);
    case "chat-completions":
    case "responses":
        return openAiClient(options);
    }
}

/** Passes the body through, calling `touch` for every chunk that arrives. */
function watched(fetchFn: typeof fetch, touch: () => void): typeof fetch {
    const wrapped = async (...args: Parameters<typeof fetch>) => {
        const response = await fetchFn(...args);
        touch();
        if (!response.body)
            return response;
        const body = response.body.pipeThrough(new TransformStream({
            transform(chunk, controller) {
                touch();
                controller.enqueue(chunk);
            },
        }));
        return new Response(body, { status: response.status, statusText: response.statusText, headers: response.headers });
    };
    return wrapped as typeof fetch;
}

export interface ServiceOptions {
    relay: Relay;
    idleTimeoutMs?: number;
    /** Milliseconds, for explicit cache expiry. */
    now?: () => number;
}

/** A cache with less than this left is made again rather than used. */
const kCacheMarginMs = 30_000;

/** An explicit provider cache kept for one conversation. */
interface ConversationCache {
    providerId: string;
    /** The provider, model and prefix the cache holds. */
    holds: string;
    /** Absent when the provider refused to make one for this prefix. */
    name?: string;
    expires: number;
}

function cacheContents(providerId: string, request: Request): string {
    return JSON.stringify([providerId, request.model, prefixTexts(request), sortedTools(request), request.context ?? "",
        request.webSearch === true]);
}

/**
 * The providers Qt configured, keyless: every request leaves through Qt's relay, which adds the key. One that hears
 * nothing for the idle timeout fails.
 */
export class ProviderService {
    private providers = new Map<string, ProviderConfig>();
    private search: SearchService | undefined;
    private readonly relay: Relay;
    private readonly idleTimeoutMs: number;
    private readonly now: () => number;
    private readonly caches = new Map<string, ConversationCache>();

    constructor(options: ServiceOptions) {
        this.relay = options.relay;
        this.idleTimeoutMs = options.idleTimeoutMs ?? kIdleTimeoutMs;
        this.now = options.now ?? Date.now;
    }

    set(params: unknown): null {
        const { providers, searchService } = checked<ProvidersSetParams>(kSchemas.providersSetParams, params);
        this.providers = new Map(providers.map((p) => [p.id, p]));
        this.search = searchService;
        for (const [key, cache] of this.caches)
            if (!this.providers.has(cache.providerId))
                this.caches.delete(key);
        return null;
    }

    /** The service that searches for providers without their own search, while web search is on. */
    get searchService(): SearchService | undefined {
        return this.search;
    }

    async endConversation(params: unknown, signal: AbortSignal): Promise<null> {
        const p = checked<ConversationEndParams>(kSchemas.conversationEndParams, params);
        const cache = this.caches.get(p.cacheKey);
        this.caches.delete(p.cacheKey);
        if (cache?.name && cache.providerId === p.providerId)
            await this.run(p.providerId, signal, (client, s) => client.deleteCache?.(cache.name!, s) ?? Promise.resolve());
        return null;
    }

    async models(params: unknown, signal: AbortSignal): Promise<ModelsListResult> {
        const p = checked<ModelsListParams>(kSchemas.modelsListParams, params);
        return { models: await this.run(p.providerId, signal, (client, s) => client.models(s)) };
    }

    complete(params: unknown, signal: AbortSignal, emit: (stream: number, update: Update) => void): Promise<Finished> {
        const checkedParams = checked<CompleteParams>(kSchemas.completeParams, params);
        const p = { ...checkedParams, request: withCaching(checkedParams.request) };
        if (!p.request.model)
            return Promise.reject(new ProviderError("noModel"));
        return this.run(p.providerId, signal, async (client, s, touch) => {
            const out = new Collector((update) => {
                touch();
                emit(p.stream, update);
            });
            const cache = await this.cacheFor(p.providerId, p.request, client, s);
            try {
                return out.finish(await client.complete(p.request, out, s, cache));
            } catch (error) {
                if (cache && p.request.cacheKey)
                    this.caches.delete(p.request.cacheKey);
                throw error;
            }
        });
    }

    /**
     * The explicit cache for the request's conversation: reused while it holds the same prefix and has time left,
     * else made again. A provider that refuses one for a prefix isn't asked again for it; any other failure sends the
     * request whole and tries again next turn.
     */
    private async cacheFor(providerId: string, request: Request, client: Client, signal: AbortSignal) {
        const key = request.cacheKey;
        if (!request.explicitCache || !key || !client.createCache)
            return undefined;
        const holds = cacheContents(providerId, request);
        const known = this.caches.get(key);
        if (known?.holds === holds && (!known.name || known.expires - this.now() > kCacheMarginMs))
            return known.name;
        this.caches.delete(key);
        if (known?.name && known.providerId === providerId)
            await client.deleteCache?.(known.name, signal).catch(() => {});
        const ttlSeconds = request.cacheLifetime === "hour" ? 3600 : 300;
        try {
            const name = await client.createCache(request, ttlSeconds, signal);
            this.caches.set(key, { providerId, holds, name, expires: this.now() + ttlSeconds * 1000 });
            return name;
        } catch (error) {
            if (signal.aborted)
                throw error;
            if (error instanceof ProviderError && error.httpStatus >= 400 && error.httpStatus < 500)
                this.caches.set(key, { providerId, holds, expires: Infinity });
            return undefined;
        }
    }

    private async run<T>(providerId: string, signal: AbortSignal,
        work: (client: Client, signal: AbortSignal, touch: () => void) => Promise<T>): Promise<T> {
        const config = this.providers.get(providerId);
        if (!config)
            throw new ProviderError("noProvider");
        const controller = new AbortController();
        const abort = (error: ProviderError) => {
            if (!controller.signal.aborted)
                controller.abort(error);
        };
        let timer: ReturnType<typeof setTimeout> | undefined;
        const touch = () => {
            clearTimeout(timer);
            timer = setTimeout(() => abort(new ProviderError("timeout")), this.idleTimeoutMs);
        };
        const onCancel = () => abort(new ProviderError("cancelled"));
        signal.addEventListener("abort", onCancel);
        if (signal.aborted)
            onCancel();
        touch();
        try {
            const fetchFn = watched(relayFetch(this.relay, { kind: "provider", id: providerId }), touch);
            const client = clientFor({ config, fetch: fetchFn });
            const aborted = new Promise<never>((_, reject) => {
                controller.signal.addEventListener("abort", () => reject(controller.signal.reason));
            });
            aborted.catch(() => {});
            return await Promise.race([work(client, controller.signal, touch), aborted]);
        } catch (error) {
            throw providerErrorOf(error, controller.signal);
        } finally {
            clearTimeout(timer);
            signal.removeEventListener("abort", onCancel);
            abort(new ProviderError("cancelled"));
        }
    }
}
