import { describe, expect, test } from "bun:test";
import { ErrorCode } from "../../src/protocol";
import { RpcError } from "../../src/rpc";
import { McpError } from "../../src/mcp/errors";
import { kMeta, kProtocolVersion, McpServer, type HttpHeaders } from "../../src/mcp/server";
import type { OperationName, Operations } from "../../src/tools/api.gen";
import type { HikariCalls } from "../../src/tools/toolset";

type Answer = unknown | RpcError | ((params: any) => unknown);

interface Call {
    connection: string;
    client?: string;
    operation: string;
    params: unknown;
}

/** HA1 for every connection: answers from `answers`, keeping each call with the connection and client it came for. */
class FakeHa1 {
    calls: Call[] = [];
    constructor(public answers: Partial<Record<OperationName, Answer>> = {}) {}

    for(connection: string, client: () => string | undefined): HikariCalls {
        return {
            call: <O extends OperationName>(operation: O, params?: Operations[O]["params"]) => {
                this.calls.push({ connection, client: client(), operation, params });
                const answer = this.answers[operation];
                if (answer instanceof RpcError)
                    return Promise.reject(answer);
                return Promise.resolve((typeof answer === "function" ? answer(params) : answer) as Operations[O]["result"]);
            },
        };
    }
}

const info = { name: "hikarisub", title: "HikariSub", version: "test" };

function server(answers: Partial<Record<OperationName, Answer>> = {}) {
    const ha1 = new FakeHa1(answers);
    const mcp = new McpServer((connection, client) => ha1.for(connection, client), info);
    const send = async (message: unknown, connection = "c1", http?: HttpHeaders) => {
        const answer = await mcp.message(connection, typeof message === "string" ? message : JSON.stringify(message), http);
        return { status: answer.status, reply: answer.reply === undefined ? undefined : JSON.parse(answer.reply) };
    };
    return { ha1, mcp, send };
}

const meta = (client = "Claude Code") => ({
    [kMeta.protocolVersion]: kProtocolVersion,
    [kMeta.clientCapabilities]: {},
    [kMeta.clientInfo]: { name: "claude-code", title: client, version: "3.0" },
});

const request = (id: number, method: string, params: Record<string, unknown> = {}) =>
    ({ jsonrpc: "2.0", id, method, params: { ...params, _meta: meta() } });

const http = (method: string, name?: string): HttpHeaders => ({ protocolVersion: kProtocolVersion, method, name });

describe("McpServer, 2026-07-28", () => {
    test("server/discover answers the versions, capabilities and server info", async () => {
        const { send } = server();
        const { status, reply } = await send(request(1, "server/discover"));
        expect(status).toBe(200);
        expect(reply.id).toBe(1);
        expect(reply.result).toMatchObject({
            resultType: "complete",
            supportedVersions: [kProtocolVersion],
            capabilities: { tools: {}, resources: {} },
            _meta: { [kMeta.serverInfo]: info },
        });
        expect(reply.result.instructions).toContain("documents_list");
    });

    test("a request without client capabilities, or for another version, is refused with 400", async () => {
        const { send } = server();
        const bare = await send({ jsonrpc: "2.0", id: 1, method: "tools/list",
            params: { _meta: { [kMeta.protocolVersion]: kProtocolVersion } } });
        expect(bare.status).toBe(400);
        expect(bare.reply.error.code).toBe(McpError.invalidParams);

        const future = await send({ jsonrpc: "2.0", id: 2, method: "tools/list",
            params: { _meta: { ...meta(), [kMeta.protocolVersion]: "2027-01-01" } } });
        expect(future.status).toBe(400);
        expect(future.reply.error).toEqual({ code: McpError.unsupportedProtocolVersion,
            message: "Unsupported protocol version", data: { supported: [kProtocolVersion], requested: "2027-01-01" } });
    });

    test("tools/list offers every tool, and tools/call runs one as the connection's client", async () => {
        const { ha1, send } = server({ "lines.find": { revision: 3, total: 0, lines: [] } });
        const listed = await send(request(1, "tools/list"));
        const names = listed.reply.result.tools.map((t: { name: string }) => t.name);
        expect(names).toContain("lines_find");
        expect(names).toContain("video_frame");
        expect(listed.reply.result.resultType).toBe("complete");

        const called = await send(request(2, "tools/call", { name: "lines_find", arguments: { text: "Kenobi" } }));
        expect(called.reply.result).toMatchObject({ resultType: "complete",
            content: [{ type: "text", text: JSON.stringify({ revision: 3, total: 0, lines: [] }) }] });
        expect(ha1.calls).toEqual([{ connection: "c1", client: "Claude Code", operation: "lines.find",
            params: { text: "Kenobi" } }]);

        const unknown = await send(request(3, "tools/call", { name: "format_disk" }));
        expect(unknown.reply.error.code).toBe(McpError.invalidParams);
    });

    test("a refused write answers HA1's words as a tool error", async () => {
        const refusal = "Through MCP, only document.apply writes; its edits land in the review.";
        const { send } = server({ "selection.set": new RpcError(ErrorCode.hikariApi, refusal, { code: "refused" }) });
        const { reply } = await send(request(1, "tools/call", { name: "selection_set", arguments: { selected: [1] } }));
        expect(reply.result).toMatchObject({ isError: true,
            content: [{ type: "text", text: "Through MCP, only document_apply writes; its edits land in the review." }] });
    });

    test("Streamable HTTP headers must agree with the message", async () => {
        const { send } = server({ "lines.find": { revision: 1, total: 0, lines: [] } });
        const call = request(1, "tools/call", { name: "lines_find", arguments: { text: "a" } });
        expect((await send(call, "http", http("tools/call", "lines_find"))).status).toBe(200);
        const encoded = `=?base64?${Buffer.from("lines_find").toString("base64")}?=`;
        expect((await send(call, "http", http("tools/call", encoded))).status).toBe(200);

        for (const headers of [{ ...http("tools/call", "lines_find"), protocolVersion: "2025-11-25" },
                               http("tools/list", "lines_find"), http("tools/call", "lines_get"), http("tools/call")]) {
            const { status, reply } = await send(call, "http", headers);
            expect(status).toBe(400);
            expect(reply.error.code).toBe(McpError.headerMismatch);
        }
        const legacyBody = await send({ jsonrpc: "2.0", id: 2, method: "tools/list" }, "http", http("tools/list"));
        expect(legacyBody.status).toBe(400);
    });
});

describe("McpServer, before 2026-07-28", () => {
    test("initialize agrees on the client's version when HikariSub speaks it, else the newest", async () => {
        const { send } = server();
        const known = await send({ jsonrpc: "2.0", id: 1, method: "initialize", params: { protocolVersion: "2025-06-18",
            capabilities: {}, clientInfo: { name: "codex", version: "1" } } });
        expect(known.reply.result).toEqual({
            protocolVersion: "2025-06-18",
            capabilities: { tools: { listChanged: false }, resources: {} },
            serverInfo: info,
            instructions: expect.any(String),
        });
        const unknown = await send({ jsonrpc: "2.0", id: 2, method: "initialize",
            params: { protocolVersion: "2023-01-01", capabilities: {}, clientInfo: { name: "x", version: "1" } } }, "c2");
        expect(unknown.reply.result.protocolVersion).toBe("2025-11-25");
        expect(unknown.reply.result.resultType).toBeUndefined();
    });

    test("the client named in initialize runs the connection's calls", async () => {
        const { ha1, mcp, send } = server({ "documents.list": { documents: [] } });
        await send({ jsonrpc: "2.0", id: 1, method: "initialize", params: { protocolVersion: "2025-11-25",
            capabilities: {}, clientInfo: { name: "codex", version: "1" } } });
        expect((await send({ jsonrpc: "2.0", method: "notifications/initialized" })).status).toBe(202);
        await send({ jsonrpc: "2.0", id: 2, method: "tools/call", params: { name: "documents_list" } });
        mcp.closed("c1");
        await send({ jsonrpc: "2.0", id: 3, method: "tools/call", params: { name: "documents_list" } });
        expect(ha1.calls.map((c) => c.client)).toEqual(["codex", undefined]);
    });

    test("server/discover needs _meta", async () => {
        const { send } = server();
        const { status, reply } = await send({ jsonrpc: "2.0", id: 1, method: "server/discover" });
        expect(status).toBe(400);
        expect(reply.error.code).toBe(McpError.invalidParams);
    });
});

describe("McpServer messages", () => {
    test("malformed messages, batches and unknown methods get JSON-RPC errors with their HTTP status", async () => {
        const { send } = server();
        expect(await send("{")).toEqual({ status: 400, reply: { jsonrpc: "2.0", id: null,
            error: { code: McpError.parse, message: "Parse error" } } });
        expect((await send([request(1, "ping")])).reply.error.code).toBe(McpError.invalidRequest);
        expect((await send({ id: 1, method: "ping" })).status).toBe(400);
        expect((await send({ jsonrpc: "2.0", id: true, method: "ping" })).reply.error.code).toBe(McpError.invalidRequest);
        const unknown = await send(request(1, "prompts/list"));
        expect(unknown.status).toBe(404);
        expect(unknown.reply.error.code).toBe(McpError.methodNotFound);
        expect(await send({ jsonrpc: "2.0", id: 9, result: {} })).toEqual({ status: 202, reply: undefined });
        expect((await send(request(2, "tools/list", { cursor: "x" }))).reply.error.code).toBe(McpError.invalidParams);
        expect((await send(request(3, "ping"))).reply.result.resultType).toBe("complete");
    });

    test("a cancelled request gets no reply", async () => {
        let release!: (value: unknown) => void;
        const { send } = server({ "documents.list": () => new Promise((resolve) => { release = resolve; }) });
        const pending = send({ jsonrpc: "2.0", id: 5, method: "tools/call", params: { name: "documents_list" } });
        await new Promise((resolve) => setTimeout(resolve, 0));
        expect((await send({ jsonrpc: "2.0", method: "notifications/cancelled", params: { requestId: 5 } })).status)
            .toBe(202);
        release({ documents: [] });
        expect(await pending).toEqual({ status: 202, reply: undefined });
    });
});

describe("McpServer resources", () => {
    const documents = { documents: [{ document: 1, title: "ep01.ass", revision: 7 }, { document: 4, title: "ep02.ass" }] };

    test("lists each open Document's state and the templates for it", async () => {
        const { send } = server({ "documents.list": documents });
        const { reply } = await send(request(1, "resources/list"));
        const uris = reply.result.resources.map((r: { uri: string }) => r.uri);
        expect(uris).toEqual(["hikari://documents", "hikari://documents/1", "hikari://documents/1/lines",
            "hikari://documents/1/styles", "hikari://documents/1/info", "hikari://documents/4",
            "hikari://documents/4/lines", "hikari://documents/4/styles", "hikari://documents/4/info"]);
        expect(reply.result.resources[2]).toMatchObject({ name: "document-1-lines", title: "ep01.ass: Lines",
            mimeType: "application/json" });
        const templates = await send(request(2, "resources/templates/list"));
        expect(templates.reply.result.resourceTemplates.map((t: { uriTemplate: string }) => t.uriTemplate)).toContain(
            "hikari://documents/{document}/lines");
    });

    test("reads every Line of a Document page by page at one revision", async () => {
        const all = Array.from({ length: 7001 }, (_, i) => ({ id: i + 1, row: i }));
        const { ha1, send } = server({ "lines.list": (p: { offset?: number; limit: number }) => ({ revision: 7,
            total: all.length, lines: all.slice(p.offset ?? 0, (p.offset ?? 0) + p.limit) }) });
        const { reply } = await send(request(1, "resources/read", { uri: "hikari://documents/1/lines" }), "http",
            http("resources/read", "hikari://documents/1/lines"));
        const contents = reply.result.contents[0];
        expect(contents.uri).toBe("hikari://documents/1/lines");
        expect(contents.mimeType).toBe("application/json");
        const read = JSON.parse(contents.text);
        expect(read.revision).toBe(7);
        expect(read.lines.length).toBe(7001);
        expect(ha1.calls.map((c) => c.params)).toEqual([{ document: 1, limit: 5000 },
            { document: 1, offset: 5000, limit: 5000 }]);
    });

    test("a missing Document or an unknown uri is not found", async () => {
        const { send } = server({ "styles.list": new RpcError(ErrorCode.hikariApi, "No open Document has id 9.",
            { code: "not_found", data: { document: 9 } }) });
        for (const uri of ["hikari://documents/9/styles", "hikari://documents/1/fonts", "file:///etc/passwd"]) {
            const { reply } = await send(request(1, "resources/read", { uri }));
            expect(reply.error).toEqual({ code: McpError.invalidParams, message: "Resource not found", data: { uri } });
        }
    });
});
