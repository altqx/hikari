import { afterEach, describe, expect, test } from "bun:test";
import { spawn } from "node:child_process";
import { mkdtempSync, readFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

import { whileTurnRuns } from "../../src/agent/session";
import { CodexSession } from "../../src/agents/codex";
import { McpServer } from "../../src/mcp/server";
import type { SessionUpdate } from "../../src/protocol";
import { HikariApi, sessionCalls } from "../../src/sidecar";
import type { OperationName } from "../../src/tools/api.gen";
import { qtLink } from "./qt_link";

const kFake = join(import.meta.dir, "..", "fixtures", "fake-codex.ts");
const kToken = "t".repeat(64);
const servers: { stop(force?: boolean): unknown }[] = [];

afterEach(() => {
    for (const server of servers.splice(0))
        server.stop(true);
});

/** Qt's side: a tools channel for the session, and `codex app-server` pointed at it. */
function harness(answers: Partial<Record<OperationName, unknown>> = {}, child?: ReturnType<typeof qtLink>) {
    const log = join(mkdtempSync(join(tmpdir(), "fake-codex-")), "log");
    const calls: { operation: string; params: unknown }[] = [];
    const updates: SessionUpdate[] = [];
    const spawned: string[] = [];
    let session: CodexSession | undefined;
    const params = { sessionId: "t1", agent: "codex" as const, model: { providerId: "", model: "" }, system: "Be brief.",
        ...(child ? { task: { parent: "p1", taskId: "k1", scope: { document: 1, lines: [1] } } } : {}) };
    const mcp = new McpServer(() => whileTurnRuns(child ? sessionCalls(new HikariApi(child.peer, undefined, "t1"), params) : {
        call: (operation, p) => {
            calls.push({ operation, params: p });
            const answer = answers[operation];
            return Promise.resolve((typeof answer === "function" ? answer() : answer) as never);
        },
    }, () => session), { name: "hikarisub", title: "HikariSub", version: "test" });
    const channel = Bun.serve({ port: 0, hostname: "127.0.0.1", fetch: async (request) => {
        if (request.headers.get("authorization") !== `Bearer ${kToken}`)
            return new Response("Unauthorized", { status: 401 });
        const answer = await mcp.message("channel:t1", await request.text());
        return new Response(answer.reply ?? null, { status: answer.status });
    } });
    servers.push(channel);
    const codex = session = new CodexSession("t1", params, {
        spawn: (id) => {
            spawned.push(id);
            return spawn(process.execPath, [kFake, "app-server", "-c", `mcp_servers.hikari.url="${channel.url}mcp"`,
                "-c", 'mcp_servers.hikari.bearer_token_env_var="HIKARI_MCP_TOKEN"'],
            { env: { ...process.env, HIKARI_MCP_TOKEN: kToken, FAKE_CODEX_LOG: log }, stdio: ["pipe", "pipe", "inherit"] });
        },
        kindOf: (tool) => (tool === "document_apply" ? "edit" : "read"),
        send: (update) => updates.push(update),
        version: "test",
    });
    const script = (s: unknown) => [{ type: "text" as const, text: typeof s === "string" ? s : JSON.stringify(s) }];
    const logged = (method: string) => readFileSync(log, "utf8").split("\n").filter((l) => l.startsWith(`${method} `))
        .map((l) => JSON.parse(l.slice(method.length + 1)) as unknown);
    return { session: codex, calls, updates, spawned, script, logged };
}

describe("Codex through its app-server", () => {
    test("an edit task runs on Hikari's tools over the session's channel", async () => {
        const h = harness({ "lines.find": { revision: 3, total: 1, lines: [{ id: 1, text: "Helo" }] },
            "document.apply": { revision: 4 } });
        const result = await h.session.prompt({ sessionId: "t1", prompt: h.script({ reply: "Fixed.", calls: [
            { tool: "lines_find", arguments: { text: "Helo" } },
            { tool: "document_apply", arguments: { revision: 3, edits: [{ line: 1, text: "Hello" }] } },
        ] }) });
        h.session.close();

        expect(result.stopReason).toBe("end_turn");
        expect(h.spawned).toEqual(["t1"]);
        expect(h.calls.map((c) => c.operation)).toEqual(["lines.find", "document.apply"]);
        expect(h.logged("thread/start")).toEqual([{ developerInstructions: "Be brief." }]);
        const tools = h.updates.filter((u) => u.sessionUpdate === "tool_call");
        expect(tools.map((u) => u.sessionUpdate === "tool_call" && [u.title, u.kind]))
            .toEqual([["lines_find", "read"], ["document_apply", "edit"]]);
        const finished = h.updates.filter((u) => u.sessionUpdate === "tool_call_update");
        expect(finished.map((u) => u.sessionUpdate === "tool_call_update" && u.status)).toEqual(["completed", "completed"]);
        expect(h.updates).toContainEqual({ sessionUpdate: "agent_message_chunk", content: { type: "text", text: "Fixed." } });
        expect(h.updates).toContainEqual({ sessionUpdate: "history",
            message: { author: "assistant", parts: [{ type: "text", text: "Fixed." }] } });
        expect(h.updates.find((u) => u.sessionUpdate === "usage")).toMatchObject(
            { usage: { inputTokens: 10, outputTokens: 2, cachedInputTokens: 4 } });
    }, 30_000);

    test("one app-server serves every turn, and usage is each turn's own", async () => {
        const h = harness({ "lines.find": { revision: 1, total: 0, lines: [] } });
        for (const reply of ["one", "two"])
            expect((await h.session.prompt({ sessionId: "t1", prompt: h.script({ reply,
                calls: [{ tool: "lines_find", arguments: { text: reply } }] }) })).stopReason).toBe("end_turn");
        h.session.close();
        expect(h.spawned).toHaveLength(1);
        expect(h.logged("thread/start")).toHaveLength(1);
        expect(h.calls.map((c) => c.params)).toEqual([{ text: "one" }, { text: "two" }]);
        const usage = h.updates.filter((u) => u.sessionUpdate === "usage");
        expect(usage.map((u) => u.sessionUpdate === "usage" && u.usage.inputTokens)).toEqual([10, 10]);
    }, 30_000);

    test("Stop interrupts the turn, which ends cancelled without the rest of its calls", async () => {
        let stop = () => {};
        const h = harness({ "lines.find": () => {
            stop();
            return { revision: 1, total: 0, lines: [] };
        } });
        stop = () => h.session.cancel();
        const result = await h.session.prompt({ sessionId: "t1", prompt: h.script({ calls: [
            { tool: "lines_find", arguments: { text: "a" } },
            { tool: "document_apply", arguments: { revision: 1, edits: [] } },
        ] }) });
        h.session.close();
        expect(result.stopReason).toBe("cancelled");
        expect(h.calls.map((c) => c.operation)).toEqual(["lines.find"]);
        expect(h.logged("turn/interrupt")).toEqual([{ threadId: "thread-1", turnId: "turn-1" }]);
    }, 30_000);

    test("when Codex exits the turn fails, and the next turn resumes the thread in a new app-server", async () => {
        const h = harness({ "lines.find": { revision: 1, total: 0, lines: [] } });
        await expect(h.session.prompt({ sessionId: "t1", prompt: h.script("exit") })).rejects.toThrow("Codex ended");
        expect((await h.session.prompt({ sessionId: "t1", prompt: h.script({ reply: "back" }) })).stopReason).toBe("end_turn");
        h.session.close();
        expect(h.spawned).toHaveLength(2);
        expect(h.logged("thread/resume")).toEqual([{ threadId: "thread-1" }]);
    }, 30_000);

    test("as a sub-agent it completes an edit through _hikari/api.call, as its own session", async () => {
        const qt = qtLink({
            "lines.find": [{ revision: 3, total: 1, lines: [{ id: 1, row: 1, text: "Helo" }] }],
            "document.apply": [{ revision: 4, inserted: [], changed: true }],
        });
        const h = harness({}, qt);
        const result = await h.session.prompt({ sessionId: "t1", prompt: h.script({ reply: "Fixed.", calls: [
            { tool: "lines_find", arguments: { document: 1, text: "Helo" } },
            { tool: "document_apply", arguments: { document: 1, revision: 3, name: "Fix",
                edits: [{ op: "set_line", line: 1, fields: { text: "Hello" } }] } },
        ] }) });
        h.session.close();
        expect(result.stopReason).toBe("end_turn");
        expect(qt.calls.map((c) => [c.operation, c.session])).toEqual([["lines.find", "t1"], ["document.apply", "t1"]]);
        expect(h.calls).toEqual([]);
    }, 30_000);
});
