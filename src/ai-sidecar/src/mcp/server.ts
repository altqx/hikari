import { RpcError } from "../rpc";
import { kReadSkillTool, Skills } from "../skills/skills";
import { kCatalogue, type Catalogue } from "../tools/catalogue";
import { ToolSet, type HikariCalls } from "../tools/toolset";
import { httpStatus, McpError } from "./errors";
import { Resources } from "./resources";

export const kProtocolVersion = "2026-07-28";
/** The versions before 2026-07-28, which open with `initialize`; the newest first. */
export const kLegacyVersions = ["2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05"] as const;

export const kMeta = {
    protocolVersion: "io.modelcontextprotocol/protocolVersion",
    clientCapabilities: "io.modelcontextprotocol/clientCapabilities",
    clientInfo: "io.modelcontextprotocol/clientInfo",
    serverInfo: "io.modelcontextprotocol/serverInfo",
} as const;

const kInstructions = "Tools and resources for the subtitle Documents open in HikariSub. documents_list lists them; "
    + "lines_find and lines_list read their Lines; hikari://documents/<id>/lines, /styles and /info hold each "
    + "Document's state. read_skill, when listed, reads skills: guidance for kinds of work that grants no permission.";

/** The Streamable HTTP headers that must agree with the message. */
export interface HttpHeaders {
    protocolVersion?: string;
    method?: string;
    name?: string;
}

/** The answer to one client message: the JSON-RPC reply, if any, and its Streamable HTTP status. */
export interface Answer {
    reply?: string;
    status: number;
}

export interface ServerInfo {
    name: string;
    title: string;
    version: string;
}

/** The calls one connection's tools make into HA1, which Qt runs as that connection's caller. */
export type CallsFor = (connection: string, client: () => string | undefined) => HikariCalls;
export type SkillsFor = (calls: HikariCalls) => Skills;

type Id = string | number;

class Failure extends RpcError {
    constructor(code: number, message: string, data?: unknown, readonly status = httpStatus(code)) {
        super(code, message, data);
    }
}

interface Connection {
    calls: HikariCalls;
    tools: ToolSet;
    /** Looked up again on each tools/list, so read_skill reads what the list offered. */
    skills?: Skills;
    resources: Resources;
    client?: string;
    running: Set<Id>;
    cancelled: Set<Id>;
}

function isObject(value: unknown): value is Record<string, unknown> {
    return typeof value === "object" && value !== null && !Array.isArray(value);
}

function isId(value: unknown): value is Id {
    return typeof value === "string" || (typeof value === "number" && Number.isInteger(value));
}

function clientName(info: unknown): string | undefined {
    if (!isObject(info))
        return undefined;
    const name = typeof info.title === "string" && info.title ? info.title : info.name;
    return typeof name === "string" && name ? name.slice(0, 80) : undefined;
}

/** `Mcp-Name`'s value: plain, or `=?base64?…?=` for what HTTP headers can't carry. */
function decodedHeader(value: string): string {
    const encoded = /^=\?base64\?(.*)\?=$/.exec(value);
    return encoded ? Buffer.from(encoded[1]!, "base64").toString("utf8") : value;
}

function reply(id: Id | null, body: { result: unknown } | { error: unknown }, status = 200): Answer {
    return { reply: JSON.stringify({ jsonrpc: "2.0", id, ...body }), status };
}

function failed(id: Id | null, error: Failure): Answer {
    const body = error.data === undefined ? { code: error.code, message: error.message }
        : { code: error.code, message: error.message, data: error.data };
    return reply(id, { error: body }, error.status);
}

/**
 * HikariSub's MCP server for agents outside it: MCP 2026-07-28, where every request carries its version in
 * `_meta`, and the versions before it, which open with `initialize`. It answers one message at a time for any
 * transport; Qt owns the listeners and checks who connects.
 */
export class McpServer {
    private readonly connections = new Map<string, Connection>();

    constructor(private readonly callsFor: CallsFor, private readonly info: ServerInfo,
        private readonly catalogue: () => Catalogue = () => kCatalogue,
        private readonly skillsFor: SkillsFor = (calls) => new Skills(calls)) {}

    async message(connection: string, text: string, http?: HttpHeaders): Promise<Answer> {
        let message: unknown;
        try {
            message = JSON.parse(text);
        } catch {
            return failed(null, new Failure(McpError.parse, "Parse error"));
        }
        if (Array.isArray(message))
            return failed(null, new Failure(McpError.invalidRequest, "Batches aren't supported"));
        if (!isObject(message) || message.jsonrpc !== "2.0")
            return failed(null, new Failure(McpError.invalidRequest, "Not a JSON-RPC 2.0 message"));
        const id = message.id;
        if (typeof message.method !== "string") {
            if (isId(id) && ("result" in message || "error" in message))
                return { status: 202 };
            return failed(isId(id) ? id : null, new Failure(McpError.invalidRequest, "A request needs a method"));
        }
        const state = this.connection(connection);
        if (id === undefined) {
            this.notified(state, message.method, message.params);
            return { status: 202 };
        }
        if (!isId(id))
            return failed(null, new Failure(McpError.invalidRequest, "A request's id is a string or an integer"));
        state.running.add(id);
        try {
            const answer = await this.request(state, id, message.method, message.params, http);
            return state.cancelled.has(id) ? { status: 202 } : answer;
        } catch (error) {
            if (state.cancelled.has(id))
                return { status: 202 };
            if (error instanceof Failure)
                return failed(id, error);
            if (error instanceof RpcError)
                return failed(id, new Failure(McpError.internalError, error.message));
            return failed(id, new Failure(McpError.internalError, error instanceof Error ? error.message : String(error)));
        } finally {
            state.running.delete(id);
            state.cancelled.delete(id);
        }
    }

    /** Forgets a connection that closed. */
    closed(connection: string): void {
        this.connections.delete(connection);
    }

    private connection(name: string): Connection {
        let state = this.connections.get(name);
        if (!state) {
            const calls = this.callsFor(name, () => state!.client);
            state = { calls, tools: new ToolSet(calls, this.catalogue()), resources: new Resources(calls), running: new Set(), cancelled: new Set() };
            this.connections.set(name, state);
        }
        return state;
    }

    private notified(state: Connection, method: string, params: unknown): void {
        if (method !== "notifications/cancelled" || !isObject(params))
            return;
        const id = params.requestId;
        if (isId(id) && state.running.has(id))
            state.cancelled.add(id);
    }

    private async request(state: Connection, id: Id, method: string, params: unknown,
                          http: HttpHeaders | undefined): Promise<Answer> {
        if (params !== undefined && !isObject(params))
            throw new Failure(McpError.invalidParams, "params must be an object");
        const meta = params?._meta;
        const version = isObject(meta) ? meta[kMeta.protocolVersion] : undefined;
        if (version === undefined)
            return reply(id, { result: await this.legacy(state, method, params, http) });
        if (typeof version !== "string" || version !== kProtocolVersion)
            throw new Failure(McpError.unsupportedProtocolVersion, "Unsupported protocol version",
                { supported: [kProtocolVersion], requested: version });
        const capabilities = (meta as Record<string, unknown>)[kMeta.clientCapabilities];
        if (!isObject(capabilities))
            throw new Failure(McpError.invalidParams, `_meta needs ${kMeta.clientCapabilities}`, undefined, 400);
        if (http)
            this.checkHeaders(http, version, method, params!);
        state.client = clientName((meta as Record<string, unknown>)[kMeta.clientInfo]) ?? state.client;
        const result = method === "server/discover" ? this.discover() : await this.dispatch(state, method, params);
        return reply(id, { result: { ...result, resultType: "complete", _meta: { [kMeta.serverInfo]: this.info } } });
    }

    private checkHeaders(http: HttpHeaders, version: string, method: string, params: Record<string, unknown>): void {
        if (http.protocolVersion !== version)
            throw new Failure(McpError.headerMismatch, "MCP-Protocol-Version differs from the request's _meta");
        if (http.method !== method)
            throw new Failure(McpError.headerMismatch, "Mcp-Method differs from the request's method");
        const named = method === "resources/read" ? params.uri
            : method === "tools/call" || method === "prompts/get" ? params.name : undefined;
        if (named !== undefined && (http.name === undefined || decodedHeader(http.name) !== named))
            throw new Failure(McpError.headerMismatch, "Mcp-Name differs from the request's params");
    }

    private async legacy(state: Connection, method: string, params: Record<string, unknown> | undefined,
                         http: HttpHeaders | undefined): Promise<Record<string, unknown>> {
        if (method === "server/discover" || http?.protocolVersion === kProtocolVersion)
            throw new Failure(McpError.invalidParams, `_meta needs ${kMeta.protocolVersion}`, undefined, 400);
        if (method !== "initialize")
            return this.dispatch(state, method, params);
        const requested = params?.protocolVersion;
        state.client = clientName(params?.clientInfo) ?? state.client;
        return {
            protocolVersion: kLegacyVersions.find((v) => v === requested) ?? kLegacyVersions[0],
            capabilities: { tools: { listChanged: false }, resources: {} },
            serverInfo: this.info,
            instructions: kInstructions,
        };
    }

    private discover(): Record<string, unknown> {
        return {
            supportedVersions: [kProtocolVersion],
            capabilities: { tools: {}, resources: {} },
            instructions: kInstructions,
        };
    }

    private async dispatch(state: Connection, method: string,
                           params: Record<string, unknown> | undefined): Promise<Record<string, unknown>> {
        switch (method) {
        case "ping":
            return {};
        case "tools/list": {
            this.firstPage(params);
            state.skills = this.skillsFor(state.calls);
            const skill = await state.skills.tool();
            return { tools: [...state.tools.list(true), ...(skill ? [skill] : [])] };
        }
        case "tools/call": {
            const name = params?.name;
            if (name === kReadSkillTool.name)
                return { ...await this.readSkill(state, params?.arguments) };
            if (typeof name !== "string" || !state.tools.has(name))
                throw new Failure(McpError.invalidParams, `Unknown tool: ${String(name)}`);
            return { ...await state.tools.call(name, params?.arguments) };
        }
        case "resources/list":
            this.firstPage(params);
            return { ...await state.resources.list() };
        case "resources/templates/list":
            this.firstPage(params);
            return { ...state.resources.templates() };
        case "resources/read": {
            const uri = params?.uri;
            if (typeof uri !== "string")
                throw new Failure(McpError.invalidParams, "resources/read needs a uri");
            return { ...await this.read(state, uri) };
        }
        default:
            throw new Failure(McpError.methodNotFound, `Method not found: ${method}`);
        }
    }

    private async readSkill(state: Connection, args: unknown) {
        state.skills ??= this.skillsFor(state.calls);
        if (!await state.skills.tool())
            throw new Failure(McpError.invalidParams, `Unknown tool: ${kReadSkillTool.name}`);
        return state.skills.read(isObject(args) ? args : {});
    }

    private async read(state: Connection, uri: string) {
        try {
            return await state.resources.read(uri);
        } catch (error) {
            if (error instanceof RpcError && error.code === McpError.invalidParams)
                throw new Failure(error.code, error.message, error.data);
            throw error;
        }
    }

    /** Every list fits one page, so no cursor is ever handed out. */
    private firstPage(params: Record<string, unknown> | undefined): void {
        if (params?.cursor !== undefined)
            throw new Failure(McpError.invalidParams, "Unknown cursor");
    }
}
