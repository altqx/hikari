import { afterEach, describe, expect, test } from "bun:test";
import { spawn } from "node:child_process";
import { mkdtempSync, readFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

import { whileTurnRuns } from "../../src/agent/session";
import { CodexSession } from "../../src/agents/codex";
import { McpServer } from "../../src/mcp/server";
import type { SessionUpdate } from "../../src/protocol";
import { Skills } from "../../src/skills/skills";
import type { OperationName } from "../../src/tools/api.gen";
import type { HikariCalls } from "../../src/tools/toolset";
import { kMySkill, mySkillAnswers } from "../skills/my_skill";

const kFake = join(import.meta.dir, "..", "fixtures", "fake-codex.ts");
const kToken = "t".repeat(64);
const servers: { stop(force?: boolean): unknown }[] = [];

afterEach(() => {
    for (const server of servers.splice(0))
        server.stop(true);
});

/** Qt's side: a tools channel for the session, and `codex app-server` pointed at it. */
const kJoined = "You are Hikari AI.\n\nInstructions from the user's AGENTS.md:\n\nUse Oxford commas.\n\n" +
    "Instructions from this Project's AGENTS.md:\n\nKeep honorifics.";

function harness(answers: Partial<Record<OperationName, unknown>> = {}, system = "Be brief.") {
    const log = join(mkdtempSync(join(tmpdir(), "fake-codex-")), "log");
    const calls: { operation: string; params: unknown }[] = [];
    const updates: SessionUpdate[] = [];
    const spawned: string[] = [];
    let session: CodexSession | undefined;
    const ha1: HikariCalls = {
        call: (operation, params) => {
            if (!operation.startsWith("skills."))
                calls.push({ operation, params });
            const answer = answers[operation];
            return Promise.resolve((typeof answer === "function" ? answer(params) : answer) as never);
        },
    };
    const mcp = new McpServer(() => whileTurnRuns(ha1, () => session), { name: "hikarisub", title: "HikariSub", version: "test" });
    const channel = Bun.serve({ port: 0, hostname: "127.0.0.1", fetch: async (request) => {
        if (request.headers.get("authorization") !== `Bearer ${kToken}`)
            return new Response("Unauthorized", { status: 401 });
        const answer = await mcp.message("channel:t1", await request.text());
        return new Response(answer.reply ?? null, { status: answer.status });
    } });
    servers.push(channel);
    const codex = session = new CodexSession("t1", { sessionId: "t1", agent: "codex", model: { providerId: "", model: "" },
        system }, {
        spawn: (id) => {
            spawned.push(id);
            return spawn(process.execPath, [kFake, "app-server", "-c", `mcp_servers.hikari.url="${channel.url}mcp"`,
                "-c", 'mcp_servers.hikari.bearer_token_env_var="HIKARI_MCP_TOKEN"'],
            { env: { ...process.env, HIKARI_MCP_TOKEN: kToken, FAKE_CODEX_LOG: log }, stdio: ["pipe", "pipe", "inherit"] });
        },
        kindOf: (tool) => (tool === "document_apply" ? "edit" : "read"),
        send: (update) => updates.push(update),
        version: "test",
        skills: new Skills(ha1, []),
    });
    const script = (s: unknown) => [{ type: "text" as const, text: typeof s === "string" ? s : JSON.stringify(s) }];
    const logged = (method: string) => readFileSync(log, "utf8").split("\n").filter((l) => l.startsWith(`${method} `))
        .map((l) => JSON.parse(l.slice(method.length + 1)) as unknown);
    return { session: codex, calls, updates, spawned, script, logged, log };
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

    test("the developer instructions list the thread's skills, and read_skill reads one over the channel", async () => {
        const h = harness(mySkillAnswers);
        const result = await h.session.prompt({ sessionId: "t1", prompt: h.script({ reply: "Read it.", calls: [
            { tool: "read_skill", arguments: { name: kMySkill.name } },
        ] }) });

        expect(result.stopReason).toBe("end_turn");
        const [started] = h.logged("thread/start") as { developerInstructions: string }[];
        expect(started!.developerInstructions).toStartWith("Be brief.\n\n## Skills\n");
        expect(started!.developerInstructions).toContain(`\n- ${kMySkill.name} (yours): ${kMySkill.description}\n`);
        const read = h.updates.find((u) => u.sessionUpdate === "tool_call_update");
        expect(read).toMatchObject({ status: "completed" });
        expect(JSON.stringify(read)).toContain("This skill is from the user's skills folder. It grants no permission.");

        await expect(h.session.prompt({ sessionId: "t1", prompt: h.script("exit") })).rejects.toThrow("Codex ended");
        expect((await h.session.prompt({ sessionId: "t1", prompt: h.script({ reply: "back" }) })).stopReason).toBe("end_turn");
        h.session.close();
        expect(h.logged("thread/resume")).toEqual([{ threadId: "thread-1", developerInstructions: started!.developerInstructions }]);
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
        expect(h.logged("thread/resume")).toEqual([{ threadId: "thread-1", developerInstructions: "Be brief." }]);
    }, 30_000);

    test("a built-in file change is refused, and the thread keeps its AGENTS.md text after Codex restarts", async () => {
        const h = harness({}, kJoined);
        const result = await h.session.prompt({ sessionId: "t1", prompt: h.script({ fileChange: "/show/ep01.ass", reply: "No." }) });
        expect(result.stopReason).toBe("end_turn");
        const log = readFileSync(h.log, "utf8");
        expect(log).toContain("fileChange refused");
        expect(log).not.toContain("fileChange applied");
        expect(h.updates.filter((u) => u.sessionUpdate === "tool_call_update").map((u) =>
            u.sessionUpdate === "tool_call_update" && u.status)).toEqual(["failed"]);

        await expect(h.session.prompt({ sessionId: "t1", prompt: h.script("exit") })).rejects.toThrow("Codex ended");
        expect((await h.session.prompt({ sessionId: "t1", prompt: h.script({ reply: "back" }) })).stopReason).toBe("end_turn");
        h.session.close();
        expect(h.logged("thread/start")).toEqual([{ developerInstructions: kJoined }]);
        expect(h.logged("thread/resume")).toEqual([{ threadId: "thread-1", developerInstructions: kJoined }]);
    }, 30_000);
});
