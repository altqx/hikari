import { describe, expect, test } from "bun:test";
import { ErrorCode, kProtocolVersion, kRequiredApiVersion, Method } from "../src/protocol";
import { Peer, RpcError } from "../src/rpc";
import { HikariApi, Sidecar } from "../src/sidecar";
import { kOwnReference } from "../src/tools/catalogue";
import { kDefaultImageBudget, kImageBudgets } from "../src/tools/images";

const limits = { maxOutstanding: 4, maxOutstandingBytes: 1024, maxMessageBytes: 512 };

function link(peerLimits = limits) {
    const sent: Record<string, unknown>[] = [];
    const failures: string[] = [];
    const peer = new Peer((line) => {
        expect(line.endsWith("\n")).toBe(true);
        expect(line.indexOf("\n")).toBe(line.length - 1);
        sent.push(JSON.parse(line));
    }, (reason) => failures.push(reason), peerLimits);
    const feed = (message: unknown) => peer.feed(new TextEncoder().encode(JSON.stringify(message) + "\n"));
    return { peer, sent, failures, feed };
}

const settle = () => new Promise((resolve) => setTimeout(resolve, 0));

describe("Peer", () => {
    test("a cancelled request gets exactly one response, with the cancel code", async () => {
        const { peer, sent, feed } = link();
        let finish!: () => void;
        let aborted = false;
        peer.onRequest("slow", (_params, signal) => new Promise<string>((resolve) => {
            signal.addEventListener("abort", () => { aborted = true; });
            finish = () => resolve("late");
        }));
        feed({ jsonrpc: "2.0", id: 7, method: "slow" });
        await settle();
        feed({ jsonrpc: "2.0", method: Method.cancelRequest, params: { requestId: 7 } });
        feed({ jsonrpc: "2.0", method: Method.cancelRequest, params: { requestId: 7 } });
        finish();
        await settle();
        expect(aborted).toBe(true);
        expect(sent).toEqual([{ jsonrpc: "2.0", id: 7, error: { code: ErrorCode.requestCancelled, message: "cancelled" } }]);
        expect(peer.outstandingIncoming).toBe(0);
    });

    test("a request that finishes before its cancel is answered once, with its result", async () => {
        const { peer, sent, feed } = link();
        peer.onRequest("fast", () => 42);
        feed({ jsonrpc: "2.0", id: 1, method: "fast" });
        await settle();
        feed({ jsonrpc: "2.0", method: Method.cancelRequest, params: { requestId: 1 } });
        await settle();
        expect(sent).toEqual([{ jsonrpc: "2.0", id: 1, result: 42 }]);
    });

    test("going over the outstanding-request limit fails the session", async () => {
        const { peer, failures, feed } = link();
        peer.onRequest("hang", () => new Promise(() => {}));
        for (let id = 1; id <= limits.maxOutstanding; ++id)
            feed({ jsonrpc: "2.0", id, method: "hang" });
        await settle();
        expect(failures).toEqual([]);
        feed({ jsonrpc: "2.0", id: 99, method: "hang" });
        expect(failures).toHaveLength(1);
    });

    test("going over the outstanding-bytes limit fails the session", async () => {
        const { peer, failures, feed } = link();
        peer.onRequest("hang", () => new Promise(() => {}));
        const params = "x".repeat(400);
        feed({ jsonrpc: "2.0", id: 1, method: "hang", params });
        feed({ jsonrpc: "2.0", id: 2, method: "hang", params });
        expect(failures).toEqual([]);
        feed({ jsonrpc: "2.0", id: 3, method: "hang", params });
        expect(failures).toHaveLength(1);
    });

    test("a message over the size limit fails the session before its newline arrives", () => {
        const { peer, failures } = link();
        peer.feed(new TextEncoder().encode("{" + " ".repeat(limits.maxMessageBytes)));
        expect(failures).toHaveLength(1);
    });

    test("a line that isn't JSON-RPC fails the session and rejects what Qt still owes", async () => {
        const { peer, failures } = link();
        const pending = peer.request("anything");
        peer.feed(new TextEncoder().encode("hello\n"));
        expect(failures).toEqual(["a line isn't JSON"]);
        expect(pending).rejects.toBeInstanceOf(RpcError);
    });

    test("a line split across chunks is one message", async () => {
        const { peer, sent } = link();
        peer.onRequest("echo", (params) => params);
        const bytes = new TextEncoder().encode(JSON.stringify({ jsonrpc: "2.0", id: 3, method: "echo", params: "ü" }) + "\n");
        for (const byte of bytes)
            peer.feed(Uint8Array.of(byte));
        await settle();
        expect(sent).toEqual([{ jsonrpc: "2.0", id: 3, result: "ü" }]);
    });

    test("outgoing requests over the limit are refused without being sent", async () => {
        const { peer, sent } = link();
        for (let i = 0; i < limits.maxOutstanding; ++i)
            peer.request("hang");
        expect(peer.request("one-more")).rejects.toMatchObject({ code: ErrorCode.busy });
        expect(sent).toHaveLength(limits.maxOutstanding);
    });
});

describe("Sidecar", () => {
    const build = { buildId: "1.4.0+test", hikariApiVersion: 3, version: "1.4.0", packages: { openai: "7.45.0" } };

    test("initialize answers with the protocol, API, version and package versions", async () => {
        const { peer, sent, feed } = link();
        new Sidecar(peer, build);
        feed({ jsonrpc: "2.0", id: 1, method: Method.initialize,
            params: { protocolVersion: kProtocolVersion, hikariApiVersion: 3, buildId: "0.1.0+app" } });
        await settle();
        expect(sent).toEqual([{ jsonrpc: "2.0", id: 1, result: {
            protocolVersion: kProtocolVersion, hikariApiVersion: 3, requiredApiVersion: kRequiredApiVersion,
            version: "1.4.0", buildId: "1.4.0+test", versions: { bun: Bun.version, openai: "7.45.0" }, capabilities: {} } }]);
    });

    test("the tools are those of the app's reference: an operation it lacks is never offered", async () => {
        const { peer, sent, feed } = link({ ...limits, maxOutstandingBytes: 1 << 20, maxMessageBytes: 1 << 20 });
        new Sidecar(peer, build);
        const older = { ...kOwnReference, operations: kOwnReference.operations.filter((op) =>
            op.name !== "audio.peaks" && op.name !== "commands.list") };
        feed({ jsonrpc: "2.0", id: 1, method: Method.initialize, params: { protocolVersion: kProtocolVersion, hikariApi: older } });
        feed({ jsonrpc: "2.0", id: 2, method: Method.toolsList, params: { all: true } });
        feed({ jsonrpc: "2.0", id: 3, method: Method.toolsCall, params: { name: "audio_peaks", arguments: {} } });
        await settle();
        const names = ((sent.find((m) => m.id === 2)!.result as { tools: { name: string }[] }).tools).map((t) => t.name);
        expect(names).toContain("lines_find");
        expect(names).not.toContain("audio_peaks");
        expect(names).not.toContain("search_commands");
        expect(sent.find((m) => m.method === Method.apiCall)).toBeUndefined();
        expect((sent.find((m) => m.id === 3)!.result as { isError: boolean }).isError).toBe(true);
    });

    test("initialize refuses a hikariApi that isn't a reference", async () => {
        const { peer, sent, feed } = link();
        new Sidecar(peer, build);
        feed({ jsonrpc: "2.0", id: 1, method: Method.initialize, params: { protocolVersion: kProtocolVersion, hikariApi: { operations: 3 } } });
        await settle();
        expect((sent[0]!.error as { code: number }).code).toBe(ErrorCode.invalidParams);
    });

    test("sessions refuse before initialize, and after it without a session id and a model", async () => {
        const { peer, sent, feed } = link({ maxOutstanding: 16, maxOutstandingBytes: 1 << 20, maxMessageBytes: 1 << 20 });
        new Sidecar(peer, build);
        feed({ jsonrpc: "2.0", id: 1, method: Method.sessionNew, params: {} });
        await settle();
        feed({ jsonrpc: "2.0", id: 2, method: Method.initialize, params: { protocolVersion: kProtocolVersion } });
        feed({ jsonrpc: "2.0", id: 3, method: Method.sessionNew, params: { sessionId: "t1" } });
        feed({ jsonrpc: "2.0", id: 4, method: Method.sessionNew, params: { sessionId: "t1", model: { providerId: "p", model: "m" } } });
        feed({ jsonrpc: "2.0", id: 5, method: Method.sessionPrompt, params: { sessionId: "t2", prompt: [{ type: "text", text: "a" }] } });
        feed({ jsonrpc: "2.0", id: 6, method: Method.sessionPrompt, params: { sessionId: "t1", prompt: [] } });
        await settle();
        expect(sent.map((m) => (m.error as { code?: number } | undefined)?.code ?? "ok")).toEqual(
            [ErrorCode.notInitialized, "ok", ErrorCode.invalidParams, "ok", ErrorCode.invalidParams, ErrorCode.invalidParams]);
    });

    test("a session's tools include web search once a search service is set, while the session searches", async () => {
        const { peer, feed } = link({ maxOutstanding: 16, maxOutstandingBytes: 1 << 20, maxMessageBytes: 1 << 20 });
        const sidecar = new Sidecar(peer, build);
        type Tools = (id: string, on: () => boolean) => { list(all: boolean): { name: string }[] };
        const sessionTools = (sidecar.sessions as unknown as { tools: Tools }).tools;
        const names = (on: boolean) => sessionTools("t1", () => on).list(true).map((t) => t.name);
        expect(names(true)).not.toContain("web_search");
        feed({ jsonrpc: "2.0", id: 1, method: Method.initialize, params: { protocolVersion: kProtocolVersion } });
        feed({ jsonrpc: "2.0", id: 2, method: Method.providersSet, params: { providers: [], searchService: "brave" } });
        await settle();
        expect(names(true)).toContain("web_search");
        expect(names(false)).not.toContain("web_search");
    });

    test("a session's image budget is its provider's wire's", async () => {
        const { peer, feed } = link();
        const sidecar = new Sidecar(peer, build);
        feed({ jsonrpc: "2.0", id: 1, method: Method.initialize, params: { protocolVersion: kProtocolVersion } });
        feed({ jsonrpc: "2.0", id: 2, method: Method.providersSet,
            params: { providers: [{ id: "g", preset: "gemini", wire: "gemini", baseUrl: "https://example.invalid" }] } });
        await settle();
        expect(sidecar.providers.imageBudget("g")).toEqual(kImageBudgets.gemini);
        expect(sidecar.providers.imageBudget("gone")).toEqual(kDefaultImageBudget);
    });

    test("a session's tool calls name the session, so Qt checks them against its thread", async () => {
        const { peer, sent } = link();
        void new HikariApi(peer, undefined, "t1").call("documents.get");
        expect(sent).toEqual([{ jsonrpc: "2.0", id: 1, method: Method.apiCall, params: { operation: "documents.get", session: "t1" } }]);
    });

    test("_hikari/api.call round-trips an HA1 read and a write", async () => {
        const { peer, sent, feed } = link();
        const sidecar = new Sidecar(peer, build);
        const read = sidecar.api.call("documents.get");
        const write = sidecar.api.call("document.apply", { revision: 4, name: "Fix",
            edits: [{ op: "set_line", line: 1, fields: { text: "Hi" } }] });
        expect(sent).toEqual([
            { jsonrpc: "2.0", id: 1, method: Method.apiCall, params: { operation: "documents.get" } },
            { jsonrpc: "2.0", id: 2, method: Method.apiCall, params: { operation: "document.apply", params: {
                revision: 4, name: "Fix", edits: [{ op: "set_line", line: 1, fields: { text: "Hi" } }] } } },
        ]);
        feed({ jsonrpc: "2.0", id: 2, result: { revision: 5, inserted: [], changed: true } });
        feed({ jsonrpc: "2.0", id: 1, result: { document: 1, revision: 4 } });
        expect(await read).toEqual({ document: 1, revision: 4 });
        expect(await write).toEqual({ revision: 5, inserted: [], changed: true });
    });

    test("an HA1 error arrives as an RpcError carrying HA1's code", async () => {
        const { peer, feed } = link();
        const call = new Sidecar(peer, build).api.call("document.apply", { revision: 1, name: "Fix", edits: [] });
        feed({ jsonrpc: "2.0", id: 1, error: { code: -32000, message: "stale", data: { code: "stale_revision" } } });
        expect(call).rejects.toMatchObject({ code: -32000, data: { code: "stale_revision" } });
    });

    test("_hikari/tools.list offers the core tools, and every tool when asked", async () => {
        const { peer, sent, feed } = link();
        new Sidecar(peer, build);
        feed({ jsonrpc: "2.0", id: 1, method: Method.initialize, params: { protocolVersion: kProtocolVersion } });
        feed({ jsonrpc: "2.0", id: 2, method: Method.toolsList });
        feed({ jsonrpc: "2.0", id: 3, method: Method.toolsList, params: { all: true } });
        await settle();
        const names = (id: number) => ((sent.find((m) => m.id === id)!.result as { tools: { name: string }[] }).tools)
            .map((t) => t.name);
        expect(names(2)).toContain("lines_find");
        expect(names(2)).not.toContain("audio_peaks");
        expect(names(3)).toContain("audio_peaks");
    });

    test("_hikari/tools.call runs a tool through _hikari/api.call and answers MCP's result", async () => {
        const { peer, sent, feed } = link();
        new Sidecar(peer, build);
        feed({ jsonrpc: "2.0", id: 1, method: Method.initialize, params: { protocolVersion: kProtocolVersion } });
        feed({ jsonrpc: "2.0", id: 2, method: Method.toolsCall, params: { name: "documents_get", arguments: { document: 2 } } });
        feed({ jsonrpc: "2.0", id: 3, method: Method.toolsCall, params: { arguments: {} } });
        await settle();
        const relayed = sent.find((m) => m.method === Method.apiCall)!;
        expect(relayed.params).toEqual({ operation: "documents.get", params: { document: 2 } });
        feed({ jsonrpc: "2.0", id: relayed.id, error: { code: -32000, message: "No open Document has id 2.",
            data: { code: "not_found", data: { document: 2 } } } });
        await settle();
        expect(sent.find((m) => m.id === 2 && !m.method)!.result).toEqual({ isError: true, content: [{ type: "text",
            text: "No open Document has id 2. Look the id up again, with documents_list, lines_find or lines_list." }] });
        expect((sent.find((m) => m.id === 3)!.error as { code: number }).code).toBe(ErrorCode.invalidParams);
    });

    test("_hikari/mcp.message answers an MCP client, with its calls marked as that connection's", async () => {
        const { peer, sent, feed } = link({ ...limits, maxOutstandingBytes: 1 << 20, maxMessageBytes: 1 << 20 });
        new Sidecar(peer, build);
        feed({ jsonrpc: "2.0", id: 1, method: Method.initialize, params: { protocolVersion: kProtocolVersion } });
        const initialize = { jsonrpc: "2.0", id: 1, method: "initialize", params: { protocolVersion: "2025-11-25",
            capabilities: {}, clientInfo: { name: "claude-code", title: "Claude Code", version: "3" } } };
        feed({ jsonrpc: "2.0", id: 2, method: Method.mcpMessage, params: { connection: "s1", text: JSON.stringify(initialize) } });
        const call = { jsonrpc: "2.0", id: 2, method: "tools/call", params: { name: "documents_get" } };
        feed({ jsonrpc: "2.0", id: 3, method: Method.mcpMessage, params: { connection: "s1", text: JSON.stringify(call) } });
        await settle();
        const relayed = sent.find((m) => m.method === Method.apiCall)!;
        expect(relayed.params).toEqual({ operation: "documents.get", params: {},
            mcp: { connection: "s1", client: "Claude Code" } });
        feed({ jsonrpc: "2.0", id: relayed.id, result: { document: 1, revision: 2 } });
        await settle();
        const answers = [2, 3].map((id) => sent.find((m) => m.id === id && !m.method)!.result as
            { reply: string; status: number });
        expect(JSON.parse(answers[0]!.reply).result.serverInfo.name).toBe("hikarisub");
        expect(answers[1]!.status).toBe(200);
        expect(JSON.parse(answers[1]!.reply).result.content[0].text).toBe(JSON.stringify({ document: 1, revision: 2 }));

        feed({ jsonrpc: "2.0", method: Method.mcpClosed, params: { connection: "s1" } });
        feed({ jsonrpc: "2.0", id: 4, method: Method.mcpMessage, params: { connection: "s1" } });
        await settle();
        expect((sent.find((m) => m.id === 4)!.error as { code: number }).code).toBe(ErrorCode.invalidParams);
    });

    test("a session's tools channel makes its calls as that session, and only for an open session", async () => {
        const { peer, sent, feed } = link({ ...limits, maxOutstandingBytes: 1 << 20, maxMessageBytes: 1 << 20 });
        new Sidecar(peer, build);
        feed({ jsonrpc: "2.0", id: 1, method: Method.initialize, params: { protocolVersion: kProtocolVersion } });
        const call = JSON.stringify({ jsonrpc: "2.0", id: 1, method: "tools/call", params: { name: "documents_get" } });
        feed({ jsonrpc: "2.0", id: 2, method: Method.mcpMessage, params: { connection: "channel:t1", text: call } });
        await settle();
        expect((sent.find((m) => m.id === 2)!.error as { code: number }).code).toBe(ErrorCode.invalidParams);

        feed({ jsonrpc: "2.0", id: 3, method: Method.sessionNew,
            params: { sessionId: "t1", agent: "codex", model: { providerId: "", model: "" } } });
        feed({ jsonrpc: "2.0", id: 4, method: Method.mcpMessage, params: { connection: "channel:t1", text: call } });
        await settle();
        const relayed = sent.find((m) => m.method === Method.apiCall)!;
        expect(relayed.params).toEqual({ operation: "documents.get", params: {}, session: "t1" });
    });
});
