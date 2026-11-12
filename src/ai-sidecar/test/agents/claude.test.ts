import { describe, expect, test } from "bun:test";
import { spawn } from "node:child_process";
import { mkdtempSync, readFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

import { ClaudeSession } from "../../src/agents/claude";
import { AgentRelay } from "../../src/agents/relay";
import { McpServer } from "../../src/mcp/server";
import { ErrorCode, Method, type SessionUpdate } from "../../src/protocol";
import { Peer } from "../../src/rpc";
import { HikariApi, sessionCalls } from "../../src/sidecar";
import type { OperationName } from "../../src/tools/api.gen";
import type { HikariCalls } from "../../src/tools/toolset";
import { kStale, qtLink } from "./qt_link";

const kFake = join(import.meta.dir, "..", "fixtures", "fake-claude.ts");

const kJoined = "You are Hikari AI.\n\nInstructions from the user's AGENTS.md:\n\nUse Oxford commas.\n\n" +
    "Instructions from this Project's AGENTS.md:\n\nKeep honorifics.";

function harness(answers: Partial<Record<OperationName, unknown>> = {}, env: Record<string, string> = {}, system?: string,
    child?: ReturnType<typeof qtLink>) {
    const log = join(mkdtempSync(join(tmpdir(), "fake-claude-")), "log");
    const calls: { operation: string; params: unknown }[] = [];
    const updates: SessionUpdate[] = [];
    const params = { sessionId: "t1", agent: "claude" as const, model: { providerId: "", model: "" },
        ...(system ? { system } : {}), ...(child ? { task: { parent: "p1", taskId: "k1", scope: { document: 1, lines: [1] } } } : {}) };
    const fake: HikariCalls = {
        call: (operation, p) => {
            calls.push({ operation, params: p });
            const answer = answers[operation];
            return Promise.resolve((typeof answer === "function" ? answer() : answer) as never);
        },
    };
    const mcp = new McpServer(() => (child ? sessionCalls(new HikariApi(child.peer, undefined, "t1"), params) : fake),
        { name: "hikarisub", title: "HikariSub", version: "test" });
    const session = new ClaudeSession("t1", params, {
        sdk: () => import("@anthropic-ai/claude-agent-sdk"),
        spawn: (o) => spawn(process.execPath, [kFake, ...o.args],
            { env: { ...o.env, FAKE_CLAUDE_LOG: log, ...env }, stdio: ["pipe", "pipe", "pipe"] }) as never,
        mcp,
        kindOf: (tool) => (tool === "document_apply" ? "edit" : "read"),
        send: (update) => updates.push(update),
    });
    const script = (s: unknown) => [{ type: "text" as const, text: JSON.stringify(s) }];
    return { session, calls, updates, script, log: () => readFileSync(log, "utf8") };
}

describe("Claude through the Agent SDK", () => {
    test("an edit task runs on Hikari's tools only, with Claude's own tools, settings and permissions off", async () => {
        const h = harness({ "lines.find": { revision: 3, total: 1, lines: [{ id: 1, text: "Helo" }] },
            "document.apply": { revision: 4 } });
        const result = await h.session.prompt({ sessionId: "t1", prompt: h.script({ reply: "Fixed.", calls: [
            { name: "mcp__hikari__lines_find", arguments: { text: "Helo" } },
            { name: "Bash", arguments: { command: "rm -rf ~" } },
            { name: "mcp__hikari__document_apply", arguments: { revision: 3, edits: [{ line: 1, text: "Hello" }] } },
        ] }) });
        h.session.close();

        expect(result.stopReason).toBe("end_turn");
        expect(h.calls.map((c) => c.operation)).toEqual(["lines.find", "document.apply"]);
        expect(h.log()).toContain("denied Bash");
        expect(h.log()).not.toContain("ran built-in");
        const args = JSON.parse(h.log().split("\n")[0]!.slice("args ".length)) as string[];
        expect(args).toContain("--tools=");
        expect(args).toContain("--strict-mcp-config");
        expect(args).toContain("--setting-sources=");
        expect(args).toContain("--permission-mode=dontAsk");
        expect(args).not.toContain("--bare");
        expect(args.some((a) => a.startsWith("--dangerously"))).toBe(false);
        expect(args.find((a) => a.startsWith("--disallowedTools"))).toContain("Agent");

        const tools = h.updates.filter((u) => u.sessionUpdate === "tool_call");
        expect(tools.map((u) => u.sessionUpdate === "tool_call" && [u.title, u.kind]))
            .toEqual([["lines_find", "read"], ["document_apply", "edit"]]);
        const finished = h.updates.filter((u) => u.sessionUpdate === "tool_call_update");
        expect(finished.map((u) => u.sessionUpdate === "tool_call_update" && u.status)).toEqual(["completed", "completed"]);
        expect(h.updates).toContainEqual({ sessionUpdate: "agent_message_chunk", content: { type: "text", text: "Fixed." } });
        expect(h.updates.find((u) => u.sessionUpdate === "usage")).toMatchObject(
            { usage: { inputTokens: 14, outputTokens: 2, cachedInputTokens: 4, reportedCost: 0.001 } });
    }, 30_000);

    test("Claude gets the joined AGENTS.md text, again on resume, and its built-in file tools are denied", async () => {
        const h = harness({}, {}, kJoined);
        const result = await h.session.prompt({ sessionId: "t1", prompt: h.script({ reply: "No.", calls: [
            { name: "Read", arguments: { file_path: "/show/.env" } },
            { name: "Edit", arguments: { file_path: "/show/ep01.ass", old_string: "a", new_string: "b" } },
            { name: "Write", arguments: { file_path: "/show/notes.md", content: "x" } },
        ] }) });
        expect(result.stopReason).toBe("end_turn");
        expect(h.log()).toContain("denied Read");
        expect(h.log()).toContain("denied Edit");
        expect(h.log()).toContain("denied Write");
        expect(h.log()).not.toContain("ran built-in");
        expect(h.calls).toEqual([]);

        await expect(h.session.prompt({ sessionId: "t1", prompt: h.script({ fail: "gone" }) })).rejects.toThrow();
        expect((await h.session.prompt({ sessionId: "t1", prompt: h.script({ reply: "back" }) })).stopReason).toBe("end_turn");
        h.session.close();
        const lines = h.log().split("\n");
        expect(lines.filter((l) => l.startsWith("system ")).map((l) => JSON.parse(l.slice("system ".length))))
            .toEqual([[kJoined], [kJoined]]);
        const runs = lines.filter((l) => l.startsWith("args ")).map((l) => JSON.parse(l.slice("args ".length)) as string[]);
        expect(runs).toHaveLength(2);
        expect(runs[0]!.some((a) => a.startsWith("--resume"))).toBe(false);
        expect(runs[1]).toContain("--resume=fake-session");
    }, 30_000);

    test("one query serves every turn of the thread", async () => {
        const h = harness({ "lines.find": { revision: 1, total: 0, lines: [] } });
        for (const reply of ["one", "two"])
            expect((await h.session.prompt({ sessionId: "t1", prompt: h.script({ reply,
                calls: [{ name: "mcp__hikari__lines_find", arguments: { text: reply } }] }) })).stopReason).toBe("end_turn");
        h.session.close();
        expect(h.log().split("\n").filter((l) => l.startsWith("args"))).toHaveLength(1);
        expect(h.calls.map((c) => c.params)).toEqual([{ text: "one" }, { text: "two" }]);
    }, 30_000);

    test("Stop interrupts the turn, which ends cancelled without the rest of its calls", async () => {
        let stop = () => {};
        const h = harness({ "lines.find": () => {
            stop();
            return { revision: 1, total: 0, lines: [] };
        } });
        stop = () => h.session.cancel();
        const result = await h.session.prompt({ sessionId: "t1", prompt: h.script({ calls: [
            { name: "mcp__hikari__lines_find", arguments: { text: "a" } },
            { name: "mcp__hikari__document_apply", arguments: { revision: 1, edits: [] } },
        ] }) });
        h.session.close();
        expect(result.stopReason).toBe("cancelled");
        expect(h.calls.map((c) => c.operation)).toEqual(["lines.find"]);
    }, 30_000);

    test("when Claude can't start, the turn fails at once with what it wrote on stderr", async () => {
        const h = harness({}, { FAKE_CLAUDE_FAIL_AT_START: "mise ERROR Permission denied (os error 13)" });
        await expect(h.session.prompt({ sessionId: "t1", prompt: h.script({ reply: "never" }) }))
            .rejects.toThrow("Claude said: mise ERROR Permission denied (os error 13)");
        h.session.close();
    }, 10_000);

    test("when Qt's agent exits at start, the relayed turn fails at once with what it wrote on stderr", async () => {
        let peer: Peer;
        const reply = (message: unknown) => peer.feed(new TextEncoder().encode(JSON.stringify(message) + "\n"));
        peer = new Peer((line) => {
            const message = JSON.parse(line) as { id?: number; method?: string; params?: { process: number } };
            queueMicrotask(() => {
                if (message.method === Method.agentStart) {
                    reply({ jsonrpc: "2.0", id: message.id, result: {} });
                    const process = message.params!.process;
                    reply({ jsonrpc: "2.0", method: Method.agentOutput, params: { process, stream: "stderr",
                        data: Buffer.from("mise ERROR Permission denied (os error 13)\n").toString("base64") } });
                    reply({ jsonrpc: "2.0", method: Method.agentExit, params: { process, code: 1 } });
                } else if (message.id !== undefined && message.method) {
                    reply({ jsonrpc: "2.0", id: message.id, error: { code: ErrorCode.invalidParams, message: "that process isn't running" } });
                }
            });
        }, () => {});
        const relay = new AgentRelay(peer);
        const session = new ClaudeSession("t1", { sessionId: "t1", agent: "claude", model: { providerId: "", model: "" } }, {
            sdk: () => import("@anthropic-ai/claude-agent-sdk"),
            spawn: (o) => relay.start("claude", { args: o.args, env: o.env, signal: o.signal }),
            mcp: new McpServer(() => ({ call: () => Promise.reject(new Error("no")) }), { name: "hikarisub", title: "HikariSub", version: "test" }),
            kindOf: () => "read",
            send: () => {},
        });
        await expect(session.prompt({ sessionId: "t1", prompt: [{ type: "text", text: "hi" }] }))
            .rejects.toThrow("Claude said: mise ERROR Permission denied (os error 13)");
        session.close();
    }, 10_000);

    test("when Claude fails, the turn's error ends with what it wrote on stderr", async () => {
        const h = harness();
        const failed = h.session.prompt({ sessionId: "t1", prompt: h.script({ fail: "Invalid API key · Please run /login" }) });
        await expect(failed).rejects.toThrow("Claude said: Invalid API key · Please run /login");
        expect(h.session.stderrTail).toContain("Please run /login");
        h.session.close();
    }, 30_000);

    test("as a sub-agent it edits through _hikari/api.call, and a stale write in its scope runs again once", async () => {
        const qt = qtLink({
            "lines.find": [{ revision: 3, total: 1, lines: [{ id: 1, row: 1, text: "Helo" }] }],
            "document.apply": [kStale, { revision: 10, inserted: [], changed: true }],
            "lines.get": [{ revision: 9, lines: [{ id: 1, row: 2, text: "Helo" }] }],
        });
        const h = harness({}, {}, undefined, qt);
        const result = await h.session.prompt({ sessionId: "t1", prompt: h.script({ reply: "Fixed.", calls: [
            { name: "mcp__hikari__lines_find", arguments: { document: 1, text: "Helo" } },
            { name: "mcp__hikari__document_apply", arguments: { document: 1, revision: 3, name: "Fix",
                edits: [{ op: "set_line", line: 1, fields: { text: "Hello" } }] } },
        ] }) });
        h.session.close();
        expect(result.stopReason).toBe("end_turn");
        expect(qt.calls.map((c) => [c.operation, c.session])).toEqual([["lines.find", "t1"], ["document.apply", "t1"],
            ["lines.get", "t1"], ["document.apply", "t1"]]);
        expect(qt.calls[2]!.params).toEqual({ ids: [1], fields: ["text"], document: 1 });
        expect((qt.calls[3]!.params as { revision: number }).revision).toBe(9);
    }, 30_000);
});
