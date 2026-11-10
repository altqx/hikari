import { describe, expect, test } from "bun:test";
import { spawn } from "node:child_process";
import { mkdtempSync, readFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

import { ClaudeSession } from "../../src/agents/claude";
import { McpServer } from "../../src/mcp/server";
import type { SessionUpdate } from "../../src/protocol";
import type { OperationName } from "../../src/tools/api.gen";

const kFake = join(import.meta.dir, "..", "fixtures", "fake-claude.ts");

function harness(answers: Partial<Record<OperationName, unknown>> = {}) {
    const log = join(mkdtempSync(join(tmpdir(), "fake-claude-")), "log");
    const calls: { operation: string; params: unknown }[] = [];
    const updates: SessionUpdate[] = [];
    const mcp = new McpServer(() => ({
        call: (operation, params) => {
            calls.push({ operation, params });
            return Promise.resolve(answers[operation] as never);
        },
    }), { name: "hikarisub", title: "HikariSub", version: "test" });
    const session = new ClaudeSession("t1", { sessionId: "t1", agent: "claude", model: { providerId: "", model: "" } }, {
        sdk: () => import("@anthropic-ai/claude-agent-sdk"),
        spawn: (o) => spawn(process.execPath, [kFake, ...o.args],
            { env: { ...o.env, FAKE_CLAUDE_LOG: log }, stdio: ["pipe", "pipe", "pipe"] }) as never,
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

        const tools = h.updates.filter((u) => u.sessionUpdate === "tool_call");
        expect(tools.map((u) => u.sessionUpdate === "tool_call" && [u.title, u.kind]))
            .toEqual([["lines_find", "read"], ["document_apply", "edit"]]);
        const finished = h.updates.filter((u) => u.sessionUpdate === "tool_call_update");
        expect(finished.map((u) => u.sessionUpdate === "tool_call_update" && u.status)).toEqual(["completed", "completed"]);
        expect(h.updates).toContainEqual({ sessionUpdate: "agent_message_chunk", content: { type: "text", text: "Fixed." } });
        expect(h.updates.find((u) => u.sessionUpdate === "usage")).toMatchObject(
            { usage: { inputTokens: 14, outputTokens: 2, cachedInputTokens: 4, reportedCost: 0.001 } });
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
});
