import { ErrorCode, kDeclaredMethods, kProtocolVersion, Method, type ApiCallParams, type InitializeParams,
    type InitializeResult, type ToolsCallParams, type ToolsListParams } from "./protocol";
import { ProviderError } from "./providers/errors";
import { LinkRelay } from "./providers/relay";
import { InvalidParams, ProviderService, type ServiceOptions } from "./providers/service";
import type { UpdateParams } from "./providers/types";
import { Peer, RpcError } from "./rpc";
import type { OperationName, Operations } from "./tools/api.gen";
import { ToolSet, type HikariCalls } from "./tools/toolset";

export interface Build {
    buildId: string;
    hikariApiVersion: number;
}

/** Hikari's tools: every call goes into HA1's `Api::call` in Qt. */
export class HikariApi implements HikariCalls {
    constructor(private readonly peer: Peer) {}

    call<O extends OperationName>(operation: O, params?: Operations[O]["params"]): Promise<Operations[O]["result"]> {
        const body: ApiCallParams = params === undefined ? { operation } : { operation, params };
        return this.peer.request(Method.apiCall, body) as Promise<Operations[O]["result"]>;
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
    readonly tools: ToolSet;
    private initialized = false;

    constructor(readonly peer: Peer, private readonly build: Build, providers?: ServiceOptions) {
        this.api = new HikariApi(peer);
        this.providers = new ProviderService(providers ?? { relay: new LinkRelay(peer) });
        this.tools = new ToolSet(this.api);
        peer.onRequest(Method.initialize, (params) => this.initialize(params));
        for (const method of kDeclaredMethods)
            this.handle(method, () => {
                throw new RpcError(ErrorCode.methodNotFound, `${method} isn't implemented in this build`);
            });
        this.handle(Method.providersSet, (params) => wire(() => this.providers.set(params)));
        this.handle(Method.modelsList, (params, signal) => wire(() => this.providers.models(params, signal)));
        this.handle(Method.conversationEnd, (params, signal) => wire(() => this.providers.endConversation(params, signal)));
        this.handle(Method.complete, (params, signal) => wire(() => this.providers.complete(params, signal,
            (stream, update) => peer.notify(Method.sessionUpdate, { stream, update } satisfies UpdateParams))));
        this.handle(Method.toolsList, (params) => ({ tools: this.tools.list((params as ToolsListParams | null)?.all === true) }));
        this.handle(Method.toolsCall, (params) => {
            const p = (params ?? {}) as Partial<ToolsCallParams>;
            if (typeof p.name !== "string")
                throw new RpcError(ErrorCode.invalidParams, "_hikari/tools.call needs a tool name");
            return this.tools.call(p.name, p.arguments);
        });
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
