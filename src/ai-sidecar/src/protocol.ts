/** The link's major version; Qt refuses a sidecar whose major differs. */
export const kProtocolVersion = 1;

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
}
export interface InitializeResult extends InitializeParams {
    capabilities: Record<string, unknown>;
}

export interface ApiCallParams {
    operation: string;
    params?: unknown;
}

export interface CancelRequestParams {
    requestId: number | string;
}
