import { AgentSessions, whileTurnRuns } from "./agent/session";
import { AcpSession } from "./agents/acp";
import { ClaudeSession } from "./agents/claude";
import { CodexSession } from "./agents/codex";
import { AgentRelay } from "./agents/relay";
import { type ClaudeAgentSdk, loadClaudeAgentSdk } from "./agents/vendor";
import { connector, mcpAgent, McpClients } from "./mcp/client";
import { McpServer, type ServerInfo } from "./mcp/server";
import { ErrorCode, kProtocolVersion, kRequiredApiVersion, Method, type ApiCallParams, type InitializeParams,
    type InitializeResult, type McpClosedParams, type McpMessageParams, type McpServerConfig, type McpServersSetParams, type McpMessageResult, type McpVia,
    type SessionCompactParams, type SessionIdParams, type SessionNewParams, type SessionPromptParams, type SessionSteerParams,
    type ToolCallKind, type ToolsCallParams, type ToolsListParams } from "./protocol";
import { ProviderError } from "./providers/errors";
import { LinkRelay, relayFetch } from "./providers/relay";
import { kSchemas } from "./providers/schema";
import { checked, InvalidParams, ProviderService, type ServiceOptions } from "./providers/service";
import type { SearchService, UpdateParams } from "./providers/types";
import { Peer, RpcError } from "./rpc";
import type { OperationName, Operations } from "./tools/api.gen";
import { Skills } from "./skills/skills";
import { Catalogue, kCatalogue, type Reference } from "./tools/catalogue";
import { ToolSet, type HikariCalls } from "./tools/toolset";
import { WebSearch, whileOn } from "./tools/web";
import { checkedTranslate, translate } from "./translate/run";

const kChannelPrefix = "channel:";

export interface Build {
    buildId: string;
    hikariApiVersion: number;
    version: string;
    /** The bundled packages' versions. */
    packages: Record<string, string>;
}

function isReference(value: unknown): value is Reference {
    if (typeof value !== "object" || value === null)
        return false;
    const { operations, $defs } = value as Partial<Reference>;
    return Array.isArray(operations) && operations.every((op) => typeof op?.name === "string" && typeof op.params === "object")
        && typeof $defs === "object" && $defs !== null;
}

/** Hikari's tools: every call goes into HA1's `Api::call` in Qt. */
export class HikariApi implements HikariCalls {
    constructor(private readonly peer: Peer, private readonly via?: () => McpVia, private readonly session?: string) {}

    call<O extends OperationName>(operation: O, params?: Operations[O]["params"]): Promise<Operations[O]["result"]> {
        const body: ApiCallParams = params === undefined ? { operation } : { operation, params };
        if (this.via)
            body.mcp = this.via();
        if (this.session !== undefined)
            body.session = this.session;
        return this.peer.request(Method.apiCall, body) as Promise<Operations[O]["result"]>;
    }
}

function isObject(value: unknown): value is Record<string, unknown> {
    return typeof value === "object" && value !== null && !Array.isArray(value);
}

function isSessionModel(value: unknown): boolean {
    return isObject(value) && typeof value.providerId === "string" && typeof value.model === "string";
}

function sessionId(method: string, params: unknown): string {
    const id = isObject(params) ? params.sessionId : undefined;
    if (typeof id !== "string" || id.length === 0)
        throw new RpcError(ErrorCode.invalidParams, `${method} needs a sessionId`);
    return id;
}

function checkedNew(params: unknown): SessionNewParams {
    sessionId(Method.sessionNew, params);
    const p = params as Record<string, unknown>;
    if (!isSessionModel(p.model))
        throw new RpcError(ErrorCode.invalidParams, "session/new needs a model with a providerId and a model");
    if (p.history !== undefined && !Array.isArray(p.history))
        throw new RpcError(ErrorCode.invalidParams, "session/new's history is a list of messages");
    if (p.agent !== undefined && typeof p.agent !== "string")
        throw new RpcError(ErrorCode.invalidParams, "session/new's agent is a name");
    return p as unknown as SessionNewParams;
}

function checkedPrompt<T extends SessionPromptParams | SessionSteerParams>(method: string, params: unknown): T {
    sessionId(method, params);
    const p = params as Record<string, unknown>;
    if (!Array.isArray(p.prompt) || p.prompt.length === 0)
        throw new RpcError(ErrorCode.invalidParams, `${method} needs a prompt`);
    if (p.model !== undefined && !isSessionModel(p.model))
        throw new RpcError(ErrorCode.invalidParams, `${method}'s model needs a providerId and a model`);
    return p as unknown as T;
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

/** How long a new session waits for the user's MCP servers before it starts without the slow ones. */
const kMcpConnectWaitMs = 5000;

function checkedServers(params: unknown): McpServerConfig[] {
    const servers = (params as Partial<McpServersSetParams> | null)?.servers;
    const valid = Array.isArray(servers) && servers.every((s: Partial<McpServerConfig>) =>
        typeof s === "object" && s !== null && typeof s.id === "string" && /^[a-z0-9-]{1,32}$/.test(s.id)
        && typeof s.name === "string" && (s.transport === "stdio" || s.transport === "http")
        && typeof s.revision === "number");
    if (!valid)
        throw new RpcError(ErrorCode.invalidParams, `${Method.mcpServersSet} needs servers with an id, a name, a transport and a revision`);
    return servers as McpServerConfig[];
}

export interface AgentOptions {
    /** Where fetched agent SDKs are unpacked. */
    vendor?: string;
    claudeSdk?: () => Promise<ClaudeAgentSdk>;
}

export class Sidecar {
    readonly api: HikariApi;
    readonly providers: ProviderService;
    tools: ToolSet;
    readonly web: WebSearch;
    catalogue: Catalogue = kCatalogue;
    readonly mcp: McpServer;
    readonly sessions: AgentSessions;
    readonly mcpClients: McpClients;
    /** Each agent session's private tools channel, by connection. */
    private readonly channels = new Map<string, McpServer>();
    private initialized = false;
    /** Some session asked for tools, so a server added now connects at once. */
    private toolsWanted = false;

    private claudeSdk?: Promise<ClaudeAgentSdk>;

    constructor(readonly peer: Peer, private readonly build: Build, providers?: ServiceOptions, agents: AgentOptions = {}) {
        this.api = new HikariApi(peer);
        const options = providers ?? { relay: new LinkRelay(peer) };
        const agentRelay = new AgentRelay(peer);
        const info = { name: "hikarisub", title: "HikariSub", version: build.buildId };
        this.providers = new ProviderService(options);
        this.web = new WebSearch(options.relay, () => this.providers.searchService);
        this.tools = new ToolSet(this.api, this.catalogue, [this.web]);
        this.mcpClients = new McpClients(connector(peer,
            (server) => agentRelay.start(mcpAgent(server), { args: [], env: {} }), options.relay, info));
        this.mcp = new McpServer((connection, client) => new HikariApi(peer, () => ({ connection, client: client() })),
            info, () => this.catalogue);
        this.sessions = new AgentSessions((id, webSearch) =>
            new ToolSet(new HikariApi(peer, undefined, id), this.catalogue,
                () => [whileOn(this.web, webSearch), ...this.mcpClients.sources(id)]),
            (params, signal, emit) => this.providers.complete(params, signal, emit),
            (method, params) => peer.notify(method, params),
            (providerId) => this.providers.imageBudget(providerId),
            { claude: (id, params, send) => new ClaudeSession(id, params, {
                sdk: agents.claudeSdk ?? (() => this.claudeSdk ??= agents.vendor
                    ? loadClaudeAgentSdk(relayFetch(options.relay, { kind: "npm" }), agents.vendor)
                    : Promise.reject(new Error("Qt gave the sidecar no vendor folder"))),
                spawn: (o) => agentRelay.start("claude", { args: o.args, env: o.env, signal: o.signal }),
                mcp: new McpServer(() => new HikariApi(peer, undefined, id), info, () => this.catalogue),
                kindOf: (tool) => this.kindOf(tool),
                send,
            }),
            codex: (id, params, send) => new CodexSession(id, params, {
                spawn: (session) => agentRelay.start("codex", { args: [], env: {}, session }),
                kindOf: (tool) => this.kindOf(tool),
                send,
                version: build.version,
            }),
            acp: (id, params, send) => new AcpSession(id, params, {
                spawn: (session) => agentRelay.start(params.agent!, { args: [], env: {}, session }),
                kindOf: (tool) => this.kindOf(tool),
                send,
                version: build.version,
            }) },
            (id) => new Skills(new HikariApi(peer, undefined, id)));
        peer.onRequest(Method.initialize, (params) => this.initialize(params));
        this.handle(Method.sessionNew, (params) => {
            const opened = this.sessions.open(checkedNew(params));
            this.toolsWanted = true;
            if (this.mcpClients.size === 0)
                return opened;
            return Promise.race([this.mcpClients.ensure(), Bun.sleep(kMcpConnectWaitMs)]).then(() => opened);
        });
        this.handle(Method.mcpServersSet, (params) =>
            this.mcpClients.set(checkedServers(params)).then(() => {
                if (this.toolsWanted)
                    void this.mcpClients.ensure();
                return null;
            }));
        this.handle(Method.sessionPrompt, (params) =>
            wire(() => this.sessions.prompt(checkedPrompt<SessionPromptParams>(Method.sessionPrompt, params))));
        this.handle(Method.sessionSteer, (params) =>
            this.sessions.steer(checkedPrompt<SessionSteerParams>(Method.sessionSteer, params)));
        this.handle(Method.sessionCancel, (params) => {
            this.sessions.cancel(sessionId(Method.sessionCancel, params));
            return null;
        });
        peer.onNotification(Method.sessionCancel, (params) => {
            const id = (params as Partial<SessionIdParams> | null)?.sessionId;
            if (typeof id === "string")
                this.sessions.cancel(id);
        });
        this.handle(Method.sessionCompact, (params) => wire(() => {
            const p = params as Partial<SessionCompactParams>;
            if (p.instruction !== undefined && typeof p.instruction !== "string")
                throw new RpcError(ErrorCode.invalidParams, `${Method.sessionCompact}'s instruction is text`);
            return this.sessions.compact({ ...p, sessionId: sessionId(Method.sessionCompact, params) });
        }));
        this.handle(Method.sessionClose, (params) => {
            this.sessions.close(sessionId(Method.sessionClose, params));
            return null;
        });
        this.handle(Method.providersSet, (params) => wire(() => this.providers.set(params)));
        this.handle(Method.modelsList, (params, signal) => wire(() => this.providers.models(params, signal)));
        this.handle(Method.conversationEnd, (params, signal) => wire(() => this.providers.endConversation(params, signal)));
        this.handle(Method.complete, (params, signal) => wire(() => this.providers.complete(params, signal,
            (stream, update) => peer.notify(Method.sessionUpdate, { stream, update } satisfies UpdateParams))));
        this.handle(Method.translate, (params, signal) => {
            const p = checkedTranslate(params);
            return wire(() => translate(p, signal, {
                complete: (providerId, request, s) => this.providers.complete({ stream: p.stream, providerId, request }, s,
                    () => {}),
                project: async (document) => {
                    const found = await this.api.call("translation.project", { document });
                    return found.folder === null ? undefined : found.files;
                },
                notify: (progress) => peer.notify(Method.translateProgress, progress),
            }));
        });
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
            return this.mcpFor(p.connection, info).message(p.connection, p.text, p.http);
        });
        peer.onNotification(Method.mcpClosed, (params) => {
            const connection = (params as Partial<McpClosedParams> | null)?.connection;
            if (typeof connection !== "string")
                return;
            (this.channels.get(connection) ?? this.mcp).closed(connection);
            this.channels.delete(connection);
        });
    }

    /** A `channel:<session>` connection is that session's agent, and its calls are the session's. */
    private mcpFor(connection: string, info: ServerInfo): McpServer {
        if (!connection.startsWith(kChannelPrefix))
            return this.mcp;
        const session = connection.slice(kChannelPrefix.length);
        if (!this.sessions.has(session))
            throw new RpcError(ErrorCode.invalidParams, `No session is named "${session}"`);
        let channel = this.channels.get(connection);
        if (!channel) {
            channel = new McpServer(() => whileTurnRuns(new HikariApi(this.peer, undefined, session),
                () => this.sessions.get(session)), info, () => this.catalogue);
            this.channels.set(connection, channel);
        }
        return channel;
    }

    private kindOf(tool: string): ToolCallKind {
        if (tool === "document_apply")
            return "edit";
        return this.tools.list(true).find((t) => t.name === tool)?.annotations.readOnlyHint ? "read" : "other";
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
        if (p.hikariApi !== undefined) {
            if (!isReference(p.hikariApi))
                throw new RpcError(ErrorCode.invalidParams, "initialize's hikariApi isn't an HA1 reference");
            this.catalogue = new Catalogue(p.hikariApi);
            this.tools = new ToolSet(this.api, this.catalogue, [this.web]);
        }
        this.initialized = true;
        return {
            protocolVersion: kProtocolVersion,
            hikariApiVersion: this.build.hikariApiVersion,
            requiredApiVersion: kRequiredApiVersion,
            version: this.build.version,
            buildId: this.build.buildId,
            versions: { bun: Bun.version, ...this.build.packages },
            capabilities: {},
        };
    }
}
