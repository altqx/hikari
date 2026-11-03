import { describe, expect, test } from "bun:test";
import { ErrorCode, kProtocolVersion, Method } from "../src/protocol";
import { Peer, RpcError } from "../src/rpc";
import { Sidecar } from "../src/sidecar";

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
    const build = { buildId: "0.1.0+test", hikariApiVersion: 3 };

    test("initialize answers with the protocol, API and build versions", async () => {
        const { peer, sent, feed } = link();
        new Sidecar(peer, build);
        feed({ jsonrpc: "2.0", id: 1, method: Method.initialize,
            params: { protocolVersion: kProtocolVersion, hikariApiVersion: 3, buildId: "0.1.0+test" } });
        await settle();
        expect(sent).toEqual([{ jsonrpc: "2.0", id: 1, result: {
            protocolVersion: kProtocolVersion, hikariApiVersion: 3, buildId: "0.1.0+test", capabilities: {} } }]);
    });

    test("declared methods refuse before initialize and say they aren't implemented after it", async () => {
        const { peer, sent, feed } = link();
        new Sidecar(peer, build);
        feed({ jsonrpc: "2.0", id: 1, method: Method.sessionNew, params: {} });
        await settle();
        feed({ jsonrpc: "2.0", id: 2, method: Method.initialize, params: { protocolVersion: kProtocolVersion } });
        feed({ jsonrpc: "2.0", id: 3, method: Method.sessionNew, params: {} });
        await settle();
        expect(sent.map((m) => (m.error as { code?: number } | undefined)?.code ?? "ok")).toEqual(
            [ErrorCode.notInitialized, "ok", ErrorCode.methodNotFound]);
    });

    test("_hikari/api.call round-trips an HA1 read and a write", async () => {
        const { peer, sent, feed } = link();
        const sidecar = new Sidecar(peer, build);
        const read = sidecar.api.call("documents.get");
        const write = sidecar.api.call("document.apply", { revision: "r1", name: "Fix",
            edits: [{ op: "set_line", line: 1, fields: { text: "Hi" } }] });
        expect(sent).toEqual([
            { jsonrpc: "2.0", id: 1, method: Method.apiCall, params: { operation: "documents.get" } },
            { jsonrpc: "2.0", id: 2, method: Method.apiCall, params: { operation: "document.apply", params: {
                revision: "r1", name: "Fix", edits: [{ op: "set_line", line: 1, fields: { text: "Hi" } }] } } },
        ]);
        feed({ jsonrpc: "2.0", id: 2, result: { inserted: [], changed: true } });
        feed({ jsonrpc: "2.0", id: 1, result: { document: 1, revision: "r1" } });
        expect(await read).toEqual({ document: 1, revision: "r1" });
        expect(await write).toEqual({ inserted: [], changed: true });
    });

    test("an HA1 error arrives as an RpcError carrying HA1's code", async () => {
        const { peer, feed } = link();
        const call = new Sidecar(peer, build).api.call("document.apply", { revision: "old", edits: [] });
        feed({ jsonrpc: "2.0", id: 1, error: { code: -32000, message: "stale", data: { code: "stale_revision" } } });
        expect(call).rejects.toMatchObject({ code: -32000, data: { code: "stale_revision" } });
    });
});
