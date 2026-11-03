import { anthropicClient } from "./anthropic";
import type { Client, ClientOptions } from "./client";
import { Collector } from "./collector";
import { egressFetch, kSystemNetwork, type Network } from "./egress";
import { ProviderError, providerErrorOf } from "./errors";
import { geminiClient } from "./gemini";
import { openAiClient } from "./openai";
import { kSchemas, type Schema, validate } from "./schema";
import type { CompleteParams, Finished, ModelsListParams, ModelsListResult, ProviderConfig, ProvidersSetParams,
    Update } from "./types";

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
}

/**
 * The providers Qt configured, keyless. Keys come in with each call and live only as long as it does. Every
 * request leaves through the egress fetch, and one that hears nothing for the idle timeout fails.
 */
export class ProviderService {
    private providers = new Map<string, ProviderConfig>();
    private readonly network: Network;
    private readonly idleTimeoutMs: number;

    constructor(options: ServiceOptions = {}) {
        this.network = options.network ?? kSystemNetwork;
        this.idleTimeoutMs = options.idleTimeoutMs ?? kIdleTimeoutMs;
    }

    set(params: unknown): null {
        const { providers } = checked<ProvidersSetParams>(kSchemas.providersSetParams, params);
        this.providers = new Map(providers.map((p) => [p.id, p]));
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
            return out.finish(await client.complete(p.request, out, s));
        });
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
