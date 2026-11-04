/** MCP's JSON-RPC error codes. */
export const McpError = {
    parse: -32700,
    invalidRequest: -32600,
    methodNotFound: -32601,
    invalidParams: -32602,
    internalError: -32603,
    headerMismatch: -32020,
    missingClientCapability: -32021,
    unsupportedProtocolVersion: -32022,
} as const;

/** The Streamable HTTP status an error answers with. */
export function httpStatus(code: number): number {
    switch (code) {
    case McpError.methodNotFound:
        return 404;
    case McpError.parse:
    case McpError.invalidRequest:
    case McpError.headerMismatch:
    case McpError.missingClientCapability:
    case McpError.unsupportedProtocolVersion:
        return 400;
    default:
        return 200;
    }
}
