import { ErrorCode, kDeclaredMethods, kProtocolVersion, Method, type ApiCallParams, type InitializeParams,
    type InitializeResult } from "./protocol";
import { ProviderError } from "./providers/errors";
import { InvalidParams, ProviderService, type ServiceOptions } from "./providers/service";
import type { UpdateParams } from "./providers/types";
import { Peer, RpcError } from "./rpc";

export interface Build {
    buildId: string;
    hikariApiVersion: number;
}

/** Hikari's tools: every call goes into HA1's `Api::call` in Qt. */
export class HikariApi {
    constructor(private readonly peer: Peer) {}

    call(operation: string, params?: unknown): Promise<unknown> {
        const body: ApiCallParams = params === undefined ? { operation } : { operation, params };
        return this.peer.request(Method.apiCall, body);
    }
}

/** A provider's failure as its JSON-RPC error. */
async function wire<T>(work: () => T | Promise<T>): Promise<T> {
    try {
        return await work();
    } catch (error) {
        if (error instanceof InvalidParams)
            throw new RpcError(ErrorCode.invalidParams, error.message);
        if (error instanceof ProviderError)
            throw new RpcError(ErrorCode.providerFailed, error.detail || error.kind, error.data);
        throw error;
    }
}

export class Sidecar {
    readonly api: HikariApi;
    readonly providers: ProviderService;
    private initialized = false;

    constructor(readonly peer: Peer, private readonly build: Build, providers: ServiceOptions = {}) {
        this.api = new HikariApi(peer);
        this.providers = new ProviderService(providers);
        peer.onRequest(Method.initialize, (params) => this.initialize(params));
        for (const method of kDeclaredMethods)
            this.handle(method, () => {
                throw new RpcError(ErrorCode.methodNotFound, `${method} isn't implemented in this build`);
            });
        this.handle(Method.providersSet, (params) => wire(() => this.providers.set(params)));
        this.handle(Method.modelsList, (params, signal) => wire(() => this.providers.models(params, signal)));
        this.handle(Method.complete, (params, signal) => wire(() => this.providers.complete(params, signal,
            (stream, update) => peer.notify(Method.sessionUpdate, { stream, update } satisfies UpdateParams))));
    }

    /** Registers a Qt request that only runs after `initialize`. */
    handle(method: string, handler: (params: unknown, signal: AbortSignal) => unknown): void {
        this.peer.onRequest(method, (params, signal) => {
            if (!this.initialized)
                throw new RpcError(ErrorCode.notInitialized, "initialize first");
            return handler(params, signal);
        });
    }

    private initialize(params: unknown): InitializeResult {
        const p = (params ?? {}) as Partial<InitializeParams>;
        if (typeof p.protocolVersion !== "number")
            throw new RpcError(ErrorCode.invalidParams, "initialize needs protocolVersion");
        this.initialized = true;
        return {
            protocolVersion: kProtocolVersion,
            hikariApiVersion: this.build.hikariApiVersion,
            buildId: this.build.buildId,
            capabilities: {},
        };
    }
}
