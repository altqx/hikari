import { McpServer } from "./mcp/server";
import { ErrorCode, kDeclaredMethods, kProtocolVersion, Method, type ApiCallParams, type InitializeParams,
    type InitializeResult, type McpClosedParams, type McpMessageParams, type McpMessageResult, type McpVia,
    type ToolsCallParams, type ToolsListParams } from "./protocol";
import { ProviderError } from "./providers/errors";
import { LinkRelay } from "./providers/relay";
import { kSchemas } from "./providers/schema";
import { checked, InvalidParams, ProviderService, type ServiceOptions } from "./providers/service";
import type { SearchService, UpdateParams } from "./providers/types";
import { Peer, RpcError } from "./rpc";
import type { OperationName, Operations } from "./tools/api.gen";
import { ToolSet, type HikariCalls } from "./tools/toolset";
import { WebSearch } from "./tools/web";

export interface Build {
    buildId: string;
    hikariApiVersion: number;
}

/** Hikari's tools: every call goes into HA1's `Api::call` in Qt. */
export class HikariApi implements HikariCalls {
    constructor(private readonly peer: Peer, private readonly via?: () => McpVia) {}

    call<O extends OperationName>(operation: O, params?: Operations[O]["params"]): Promise<Operations[O]["result"]> {
        const body: ApiCallParams = params === undefined ? { operation } : { operation, params };
        if (this.via)
            body.mcp = this.via();
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
    readonly web: WebSearch;
    readonly mcp: McpServer;
    private initialized = false;

    constructor(readonly peer: Peer, private readonly build: Build, providers?: ServiceOptions) {
        this.api = new HikariApi(peer);
        const options = providers ?? { relay: new LinkRelay(peer) };
        this.providers = new ProviderService(options);
        this.web = new WebSearch(options.relay, () => this.providers.searchService);
        this.tools = new ToolSet(this.api, [this.web]);
        this.mcp = new McpServer((connection, client) => new HikariApi(peer, () => ({ connection, client: client() })),
            { name: "hikarisub", title: "HikariSub", version: build.buildId });
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
        this.handle(Method.searchTest, (params) => wire(() =>
            this.web.test(checked<{ service: SearchService }>(kSchemas.searchTestParams, params).service)));
        this.handle(Method.toolsList, (params) => ({ tools: this.tools.list((params as ToolsListParams | null)?.all === true) }));
        this.handle(Method.toolsCall, (params) => {
            const p = (params ?? {}) as Partial<ToolsCallParams>;
            if (typeof p.name !== "string")
                throw new RpcError(ErrorCode.invalidParams, "_hikari/tools.call needs a tool name");
            return this.tools.call(p.name, p.arguments);
        });
        this.handle(Method.mcpMessage, (params): Promise<McpMessageResult> => {
            const p = (params ?? {}) as Partial<McpMessageParams>;
            if (typeof p.connection !== "string" || typeof p.text !== "string")
                throw new RpcError(ErrorCode.invalidParams, "_hikari/mcp.message needs a connection and a text");
            return this.mcp.message(p.connection, p.text, p.http);
        });
        peer.onNotification(Method.mcpClosed, (params) => {
            const connection = (params as Partial<McpClosedParams> | null)?.connection;
            if (typeof connection === "string")
                this.mcp.closed(connection);
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
