import { anthropicClient } from "./anthropic";
import { prefixTexts, sortedTools } from "./cache";
import type { Client, ClientOptions } from "./client";
import { Collector } from "./collector";
import { egressFetch, kSystemNetwork, type Network } from "./egress";
import { ProviderError, providerErrorOf } from "./errors";
import { geminiClient } from "./gemini";
import { openAiClient } from "./openai";
import { kSchemas, type Schema, validate } from "./schema";
import type { CompleteParams, ConversationEndParams, Finished, ModelsListParams, ModelsListResult, ProviderConfig,
    ProvidersSetParams, Request, Update } from "./types";

export const kIdleTimeoutMs = 2 * 60 * 1000;

export class InvalidParams extends Error {}

function checked<T>(schema: Schema, params: unknown): T {
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
    network?: Network;
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
    return JSON.stringify([providerId, request.model, prefixTexts(request), sortedTools(request), request.context ?? ""]);
}

/**
 * The providers Qt configured, keyless. Keys come in with each call and live only as long as it does. Every
 * request leaves through the egress fetch, and one that hears nothing for the idle timeout fails.
 */
export class ProviderService {
    private providers = new Map<string, ProviderConfig>();
    private readonly network: Network;
    private readonly idleTimeoutMs: number;
    private readonly now: () => number;
    private readonly caches = new Map<string, ConversationCache>();

    constructor(options: ServiceOptions = {}) {
        this.network = options.network ?? kSystemNetwork;
        this.idleTimeoutMs = options.idleTimeoutMs ?? kIdleTimeoutMs;
        this.now = options.now ?? Date.now;
    }

    set(params: unknown): null {
        const { providers } = checked<ProvidersSetParams>(kSchemas.providersSetParams, params);
        this.providers = new Map(providers.map((p) => [p.id, p]));
        for (const [key, cache] of this.caches)
            if (!this.providers.has(cache.providerId))
                this.caches.delete(key);
        return null;
    }

    async endConversation(params: unknown, signal: AbortSignal): Promise<null> {
        const p = checked<ConversationEndParams>(kSchemas.conversationEndParams, params);
        const cache = this.caches.get(p.cacheKey);
        this.caches.delete(p.cacheKey);
        if (cache?.name && cache.providerId === p.providerId)
            await this.run(p.providerId, p.key ?? "", signal, (client, s) => client.deleteCache?.(cache.name!, s) ?? Promise.resolve());
        return null;
    }

    async models(params: unknown, signal: AbortSignal): Promise<ModelsListResult> {
        const p = checked<ModelsListParams>(kSchemas.modelsListParams, params);
        return { models: await this.run(p.providerId, p.key ?? "", signal, (client, s) => client.models(s)) };
    }

    complete(params: unknown, signal: AbortSignal, emit: (stream: number, update: Update) => void): Promise<Finished> {
        const p = checked<CompleteParams>(kSchemas.completeParams, params);
        if (!p.request.model)
            return Promise.reject(new ProviderError("noModel"));
        return this.run(p.providerId, p.key ?? "", signal, async (client, s, touch) => {
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

    private async run<T>(providerId: string, key: string, signal: AbortSignal,
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
            const fetchFn = watched(egressFetch(config.baseUrl, key !== "", this.network), touch);
            const client = clientFor({ config, key, fetch: fetchFn });
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
