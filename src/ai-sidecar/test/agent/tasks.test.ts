import { describe, expect, test } from "bun:test";

import { retryingStaleInScope } from "../../src/agent/scope";
import { kDefaultWaitMs, kMaxWaitMs, TaskTools } from "../../src/agent/tasks";
import { ErrorCode, kProtocolVersion, Method, type TaskState } from "../../src/protocol";
import { Peer, RpcError } from "../../src/rpc";
import { Sidecar } from "../../src/sidecar";
import type { HikariCalls } from "../../src/tools/toolset";

const build = { buildId: "test", hikariApiVersion: 1, version: "0.0.0-test", packages: {} };
const big = { maxOutstanding: 64, maxOutstandingBytes: 1 << 22, maxMessageBytes: 1 << 22 };
const settle = () => new Promise((resolve) => setTimeout(resolve, 0));

const running: TaskState = { taskId: "k1", childSessionId: "c1", title: "Part 1", status: "running" };
const done: TaskState = { ...running, status: "done", summary: "Translated 150 Lines.", cost: 0.2 };

function recorder(answers: Record<string, unknown[]>) {
    const sent: { method: string; params: unknown }[] = [];
    const request = (method: string, params: unknown) => {
        sent.push({ method, params });
        const answer = answers[method]?.shift();
        return answer instanceof Error ? Promise.reject(answer) : Promise.resolve(answer);
    };
    return { sent, request };
}

function textOf(result: { content: { type: string; text?: string }[] }): string {
    return (result.content[0] as { text: string }).text;
}

describe("sub-agent tools", () => {
    test("each tool asks Qt with the session's id", async () => {
        const r = recorder({ [Method.taskCapabilities]: [{ targets: [], running: 0, limit: 4, mode: "auto" }],
            [Method.taskRequest]: [running], [Method.taskStatus]: [running], [Method.taskCancel]: [{ ...running, status: "stopped" }] });
        const tools = new TaskTools("t1", r.request);
        await tools.call("agent_capabilities", {});
        const started = await tools.call("agent_delegate", { task: "Translate Lines 1–150.", title: "Part 1",
            scope: { document: 2, lines: [1, 2] }, target: { providerId: "g", model: "m" }, mode: "manual" });
        await tools.call("agent_task_status", { taskId: "k1" });
        const stopped = await tools.call("agent_task_cancel", { taskId: "k1" });
        expect(r.sent).toEqual([
            { method: Method.taskCapabilities, params: { sessionId: "t1" } },
            { method: Method.taskRequest, params: { sessionId: "t1", task: "Translate Lines 1–150.", title: "Part 1",
                scope: { document: 2, lines: [1, 2] }, target: { providerId: "g", model: "m" }, mode: "manual" } },
            { method: Method.taskStatus, params: { sessionId: "t1", taskId: "k1" } },
            { method: Method.taskCancel, params: { sessionId: "t1", taskId: "k1" } },
        ]);
        expect(JSON.parse(textOf(started)).next).toContain("don't poll it");
        expect(JSON.parse(textOf(stopped)).next).toBeUndefined();
    });

    test("with wait, the delegation waits for the result, ten minutes by default and thirty at most", async () => {
        const r = recorder({ [Method.taskRequest]: [running, running], [Method.taskWait]: [done, { ...running, waitTimedOut: true }] });
        const tools = new TaskTools("t1", r.request);
        const finished = await tools.call("agent_delegate", { task: "a", scope: { document: 1 }, wait: true });
        const late = await tools.call("agent_delegate", { task: "b", scope: { document: 1 }, wait: true, timeoutMs: 99_000_000 });
        expect(r.sent.filter((s) => s.method === Method.taskWait).map((s) => s.params)).toEqual([
            { sessionId: "t1", taskId: "k1", timeoutMs: kDefaultWaitMs },
            { sessionId: "t1", taskId: "k1", timeoutMs: kMaxWaitMs },
        ]);
        expect(JSON.parse(textOf(finished))).toEqual(done);
        expect(JSON.parse(textOf(late))).toMatchObject({ waitTimedOut: true, next: expect.stringContaining("arrives") });
        expect(r.sent.find((s) => s.method === Method.taskRequest)!.params).not.toHaveProperty("wait");
    });

    test("a wait Qt ends because the parent stopped gives the stopped task back", async () => {
        const r = recorder({ [Method.taskRequest]: [running], [Method.taskWait]: [{ ...running, status: "stopped", summary: "half" }] });
        const result = await new TaskTools("t1", r.request).call("agent_delegate", { task: "a", scope: { document: 1 }, wait: true });
        expect(result.isError).toBeUndefined();
        expect(JSON.parse(textOf(result))).toMatchObject({ status: "stopped", summary: "half" });
    });

    test("Qt's refusal reaches the model as a failed call with Qt's words", async () => {
        const r = recorder({ [Method.taskRequest]: [new RpcError(ErrorCode.taskRefused,
            "Four sub-agents are already running in this thread.", { code: "too_many_children" })] });
        const result = await new TaskTools("t1", r.request).call("agent_delegate", { task: "a", scope: { document: 1 } });
        expect(result).toEqual({ content: [{ type: "text", text: "Four sub-agents are already running in this thread." }], isError: true });
    });

    test("a delegation without a task or a scope never reaches Qt", async () => {
        const r = recorder({});
        const tools = new TaskTools("t1", r.request);
        expect((await tools.call("agent_delegate", { scope: { document: 1 } })).isError).toBe(true);
        expect((await tools.call("agent_delegate", { task: "a" })).isError).toBe(true);
        expect((await tools.call("agent_task_status", {})).isError).toBe(true);
        expect(r.sent).toEqual([]);
    });
});

function link() {
    const sent: Record<string, unknown>[] = [];
    const peer = new Peer((line) => sent.push(JSON.parse(line)), () => {}, big);
    const feed = (message: unknown) => peer.feed(new TextEncoder().encode(JSON.stringify(message) + "\n"));
    return { peer, sent, feed };
}

describe("which sessions delegate", () => {
    test("only a session opened with delegate offers the agent tools, from the start", () => {
        const { peer } = link();
        const sidecar = new Sidecar(peer, build);
        type Tools = (id: string, on: () => boolean, params: unknown) => { list(all?: boolean): { name: string }[] };
        const sessionTools = (sidecar.sessions as unknown as { tools: Tools }).tools;
        const names = (params: unknown) => sessionTools("t1", () => false, params).list().map((t) => t.name);
        const base = { sessionId: "t1", model: { providerId: "p", model: "m" } };
        expect(names({ ...base, delegate: true })).toEqual(expect.arrayContaining(
            ["agent_capabilities", "agent_delegate", "agent_task_status", "agent_task_cancel"]));
        expect(names(base).filter((n) => n.startsWith("agent_"))).toEqual([]);
        expect(names({ ...base, task: { parent: "p", taskId: "k", scope: { document: 1 } } })
            .filter((n) => n.startsWith("agent_"))).toEqual([]);
    });

    test("an agent's tools channel lists the agent tools only for a session that delegates", async () => {
        const { peer, sent, feed } = link();
        new Sidecar(peer, build);
        feed({ jsonrpc: "2.0", id: 1, method: Method.initialize, params: { protocolVersion: kProtocolVersion } });
        const list = JSON.stringify({ jsonrpc: "2.0", id: 1, method: "tools/list", params: {} });
        const listed = async (session: string, id: number) => {
            feed({ jsonrpc: "2.0", id, method: Method.mcpMessage, params: { connection: `channel:${session}`, text: list } });
            await settle();
            const reply = sent.find((m) => m.id === id && !m.method)!.result as { reply: string };
            return (JSON.parse(reply.reply).result.tools as { name: string }[]).map((t) => t.name);
        };
        feed({ jsonrpc: "2.0", id: 2, method: Method.sessionNew,
            params: { sessionId: "root", agent: "codex", model: { providerId: "", model: "" }, delegate: true } });
        feed({ jsonrpc: "2.0", id: 3, method: Method.sessionNew, params: { sessionId: "kid", agent: "codex",
            model: { providerId: "", model: "" }, task: { parent: "root", taskId: "k1", scope: { document: 1 } } } });
        await settle();
        expect(await listed("root", 4)).toContain("agent_delegate");
        expect((await listed("kid", 5)).filter((n) => n.startsWith("agent_"))).toEqual([]);
    });

    test("session/new refuses a task without a scope's document, and a delegate that isn't a flag", async () => {
        const { peer, sent, feed } = link();
        new Sidecar(peer, build);
        feed({ jsonrpc: "2.0", id: 1, method: Method.initialize, params: { protocolVersion: kProtocolVersion } });
        const model = { providerId: "p", model: "m" };
        feed({ jsonrpc: "2.0", id: 2, method: Method.sessionNew, params: { sessionId: "a", model, task: { parent: "p", taskId: "k" } } });
        feed({ jsonrpc: "2.0", id: 3, method: Method.sessionNew, params: { sessionId: "b", model, delegate: "yes" } });
        await settle();
        expect([2, 3].map((id) => (sent.find((m) => m.id === id)!.error as { code: number }).code))
            .toEqual([ErrorCode.invalidParams, ErrorCode.invalidParams]);
    });
});

const kStale = new RpcError(ErrorCode.hikariApi, "The Document changed.", { code: "stale_revision", data: { current: 9 } });

function ha1(answers: Record<string, unknown[]>) {
    const calls: { operation: string; params: unknown }[] = [];
    const api: HikariCalls = {
        call: (operation, params) => {
            calls.push({ operation, params });
            const answer = answers[operation]?.shift();
            return (answer instanceof Error ? Promise.reject(answer) : Promise.resolve(answer)) as never;
        },
    };
    return { api, calls };
}

const apply = (lines: number[]) => ({ document: 1, revision: 3, name: "Fix",
    edits: lines.map((line) => ({ op: "set_line" as const, line, fields: { text: "x" } })) });

describe("a sub-agent's stale write", () => {
    test("runs once more on the new revision when its scoped Lines read as before", async () => {
        const h = ha1({ "lines.list": [{ revision: 3, lines: [{ id: 1, row: 1, text: "a", style: "Default" }] }],
            "document.apply": [kStale, { revision: 10, inserted: [], changed: true }],
            "lines.get": [{ revision: 9, lines: [{ id: 1, row: 4, text: "a", style: "Default" }] }] });
        const calls = retryingStaleInScope(h.api, { document: 1, lines: [1, 2] });
        await calls.call("lines.list", { document: 1 });
        expect(await calls.call("document.apply", apply([1]))).toMatchObject({ revision: 10 });
        expect(h.calls.map((c) => c.operation)).toEqual(["lines.list", "document.apply", "lines.get", "document.apply"]);
        expect(h.calls[3]!.params).toEqual({ ...apply([1]), revision: 9 });
    });

    test("fails as before when one of its Lines changed, or it never read one, or the Line is outside its scope", async () => {
        const read = { revision: 3, lines: [{ id: 1, row: 1, text: "a" }, { id: 5, row: 5, text: "e" }] };
        const changed = ha1({ "lines.find": [read], "document.apply": [kStale],
            "lines.get": [{ revision: 9, lines: [{ id: 1, row: 1, text: "b" }] }] });
        const c1 = retryingStaleInScope(changed.api, { document: 1, lines: [1, 2, 5] });
        await c1.call("lines.find", { document: 1, text: "a" });
        await expect(c1.call("document.apply", apply([1]))).rejects.toBe(kStale);
        expect(changed.calls.filter((c) => c.operation === "document.apply")).toHaveLength(1);

        const unread = ha1({ "lines.find": [read], "document.apply": [kStale] });
        const c2 = retryingStaleInScope(unread.api, { document: 1, lines: [1, 2] });
        await c2.call("lines.find", { document: 1, text: "a" });
        await expect(c2.call("document.apply", apply([2]))).rejects.toBe(kStale);

        const outside = ha1({ "lines.find": [read], "document.apply": [kStale] });
        const c3 = retryingStaleInScope(outside.api, { document: 1, lines: [1] });
        await c3.call("lines.find", { document: 1, text: "a" });
        await expect(c3.call("document.apply", apply([5]))).rejects.toBe(kStale);
        expect([...unread.calls, ...outside.calls].some((c) => c.operation === "lines.get")).toBe(false);
    });

    test("a whole-Document scope never retries", async () => {
        const h = ha1({ "lines.list": [{ revision: 3, lines: [{ id: 1, row: 1, text: "a" }] }], "document.apply": [kStale] });
        const calls = retryingStaleInScope(h.api, { document: 1 });
        await calls.call("lines.list", { document: 1 });
        await expect(calls.call("document.apply", apply([1]))).rejects.toBe(kStale);
        expect(h.calls).toHaveLength(2);
    });
});
