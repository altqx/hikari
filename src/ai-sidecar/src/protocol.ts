import type { Reference } from "./tools/catalogue";

/** The link's major version; Qt refuses a sidecar whose major differs. */
export const kProtocolVersion = 1;
/** The oldest HA1 `kApiVersion` this sidecar works with; an app with an older one keeps its previous sidecar. */
export const kRequiredApiVersion = 1;

export const kLimits = {
    maxOutstanding: 64,
    maxOutstandingBytes: 16 * 1024 * 1024,
    maxMessageBytes: 64 * 1024 * 1024,
};
export type Limits = typeof kLimits;

export const ErrorCode = {
    parseError: -32700,
    invalidRequest: -32600,
    methodNotFound: -32601,
    invalidParams: -32602,
    internalError: -32603,
    notInitialized: -32002,
    busy: -32003,
    requestCancelled: -32800,
    /** HA1 refused an `_hikari/api.call`; `data.code` is HA1's error code and `data.data` its details. */
    hikariApi: -32000,
    /** A provider failed; `data` is the ErrorData with its kind, HTTP status and message. */
    providerFailed: -32010,
} as const;

export const Method = {
    initialize: "initialize",
    cancelRequest: "$/cancel_request",
    sessionNew: "session/new",
    sessionPrompt: "session/prompt",
    sessionCancel: "session/cancel",
    sessionUpdate: "session/update",
    requestPermission: "session/request_permission",
    providersSet: "_hikari/providers.set",
    modelsList: "_hikari/models.list",
    conversationEnd: "_hikari/conversation.end",
    complete: "_hikari/complete",
    apiCall: "_hikari/api.call",
    apiSubscribe: "_hikari/api.subscribe",
    toolsList: "_hikari/tools.list",
    toolsCall: "_hikari/tools.call",
    mcpMessage: "_hikari/mcp.message",
    mcpClosed: "_hikari/mcp.closed",
    secretStore: "_hikari/secret.store",
} as const;

/** Qt's requests that later cards implement. */
export const kDeclaredMethods = [
    Method.sessionNew,
    Method.sessionPrompt,
    Method.sessionCancel,
] as const;

export interface InitializeParams {
    protocolVersion: number;
    hikariApiVersion: number;
    buildId: string;
    /** The app's HA1 reference: the sidecar offers only the operations it lists. */
    hikariApi?: Reference;
}
export interface InitializeResult {
    protocolVersion: number;
    /** The HA1 version the sidecar was built against. */
    hikariApiVersion: number;
    requiredApiVersion: number;
    /** The `ai-v*` version, without the prefix. */
    version: string;
    buildId: string;
    /** Bun's and each bundled package's version. */
    versions: Record<string, string>;
    capabilities: Record<string, unknown>;
}

export interface ApiCallParams {
    operation: string;
    params?: unknown;
    /** The MCP connection the call is for; Qt runs it as that connection's caller. */
    mcp?: McpVia;
}

export interface McpVia {
    connection: string;
    /** The client's name from its clientInfo, which names its undo steps. */
    client?: string;
}

export interface McpMessageParams {
    connection: string;
    /** One JSON-RPC message from the client, as it arrived. */
    text: string;
    /** Set for Streamable HTTP, whose headers must agree with the message. */
    http?: { protocolVersion?: string; method?: string; name?: string };
}

export interface McpMessageResult {
    /** The JSON-RPC reply to write back; none for a notification or a cancelled request. */
    reply?: string;
    /** The Streamable HTTP status. */
    status: number;
}

export interface McpClosedParams {
    connection: string;
}

export interface ToolsListParams {
    /** Every tool, the searchable groups' too; otherwise only those offered now. */
    all?: boolean;
}

export interface ToolsCallParams {
    name: string;
    arguments?: unknown;
}

export interface CancelRequestParams {
    requestId: number | string;
}
