import { afterEach, describe, expect, test } from "bun:test";
import { spawn } from "node:child_process";
import { mkdtempSync, readFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { Writable } from "node:stream";

import { whileTurnRuns } from "../../src/agent/session";
import { AcpSession, hikariTool } from "../../src/agents/acp";
import { McpServer } from "../../src/mcp/server";
import type { SessionUpdate } from "../../src/protocol";
import { HikariApi, sessionCalls } from "../../src/sidecar";
import { Skills } from "../../src/skills/skills";
import type { OperationName } from "../../src/tools/api.gen";
import type { HikariCalls } from "../../src/tools/toolset";
import { kMySkill, mySkillAnswers } from "../skills/my_skill";
import { qtLink } from "./qt_link";

const kFake = join(import.meta.dir, "..", "fixtures", "fake-acp.ts");
const kToken = "t".repeat(64);
const servers: { stop(force?: boolean): unknown }[] = [];

afterEach(() => {
    for (const server of servers.splice(0))
        server.stop(true);
});

/** Qt's side: a tools channel for the session, and a guard that names it in `session/new`. */
function harness(answers: Partial<Record<OperationName, unknown>> = {}, env: Record<string, string> = {},
    system = "Be brief.", child?: ReturnType<typeof qtLink>) {
    const log = join(mkdtempSync(join(tmpdir(), "fake-acp-")), "log");
    const calls: { operation: string; params: unknown }[] = [];
    const updates: SessionUpdate[] = [];
    const spawned: string[] = [];
    let session: AcpSession | undefined;
    const params = { sessionId: "t1", agent: "acp:fake" as const, model: { providerId: "", model: "" }, system,
        ...(child ? { task: { parent: "p1", taskId: "k1", scope: { document: 1, lines: [1] } } } : {}) };
    const fake: HikariCalls = {
        call: (operation, p) => {
            if (!operation.startsWith("skills."))
                calls.push({ operation, params: p });
            const answer = answers[operation];
            return Promise.resolve((typeof answer === "function" ? answer(p) : answer) as never);
        },
    };
    const mcp = new McpServer(() => whileTurnRuns(child ? sessionCalls(new HikariApi(child.peer, undefined, "t1"), params) : fake,
        () => session), { name: "hikarisub", title: "HikariSub", version: "test" });
    const channel = Bun.serve({ port: 0, hostname: "127.0.0.1", fetch: async (request) => {
        if (request.headers.get("authorization") !== `Bearer ${kToken}`)
            return new Response("Unauthorized", { status: 401 });
        const answer = await mcp.message("channel:t1", await request.text());
        return new Response(answer.reply ?? null, { status: answer.status });
    } });
    servers.push(channel);
    const acp = session = new AcpSession("t1", params, {
        spawn: (id) => {
            spawned.push(id);
            const child = spawn(process.execPath, [kFake], { env: { ...process.env, FAKE_ACP_LOG: log, ...env },
                stdio: ["pipe", "pipe", "inherit"] });
            const guard = new Writable({ write: (chunk: Buffer, _e, done) => {
                const out = chunk.toString("utf8").split("\n").filter(Boolean).map((line) => {
                    const message = JSON.parse(line) as { method?: string; params?: Record<string, unknown> };
                    if (message.method === "session/new" || message.method === "session/load")
                        message.params = { ...message.params, cwd: "/work", mcpServers: [{ type: "http", name: "hikari",
                            url: `${channel.url}mcp`, headers: [{ name: "Authorization", value: `Bearer ${kToken}` }] }] };
                    return JSON.stringify(message) + "\n";
                });
                child.stdin.write(out.join(""), done);
            } });
            return { stdin: guard, stdout: child.stdout, on: child.on.bind(child) as never, kill: () => child.kill() };
        },
        kindOf: (tool) => (tool === "document_apply" ? "edit" : "read"),
        send: (update) => updates.push(update),
        version: "test",
        skills: new Skills(fake, []),
    });
    const script = (s: unknown) => [{ type: "text" as const, text: typeof s === "string" ? s : JSON.stringify(s) }];
    const logged = (method: string) => readFileSync(log, "utf8").split("\n").filter((l) => l.startsWith(`${method} `))
        .map((l) => JSON.parse(l.slice(method.length + 1)) as unknown);
    return { session: acp, calls, updates, spawned, script, logged };
}

describe("an ACP agent", () => {
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
        expect(h.logged("initialize")).toEqual([{ protocolVersion: 1,
            clientCapabilities: { fs: { readTextFile: false, writeTextFile: false }, terminal: false },
            clientInfo: { name: "hikarisub", title: "HikariSub", version: "test" } }]);
        const prompt = h.logged("session/prompt")[0] as { prompt: { text: string }[] };
        expect(prompt.prompt[0]!.text).toBe("Be brief.");
        const tools = h.updates.filter((u) => u.sessionUpdate === "tool_call");
        expect(tools.map((u) => u.sessionUpdate === "tool_call" && [u.title, u.kind]))
            .toEqual([["lines_find", "read"], ["document_apply", "edit"]]);
        const finished = h.updates.filter((u) => u.sessionUpdate === "tool_call_update");
        expect(finished.map((u) => u.sessionUpdate === "tool_call_update" && u.status)).toEqual(["completed", "completed"]);
        expect(h.updates).toContainEqual({ sessionUpdate: "agent_message_chunk", content: { type: "text", text: "Fixed." } });
        expect(h.updates).toContainEqual({ sessionUpdate: "history",
            message: { author: "assistant", parts: [{ type: "text", text: "Fixed." }] } });
    }, 30_000);

    test("the first prompt opens with the thread's skills, and read_skill reads one over the channel", async () => {
        const h = harness(mySkillAnswers);
        const result = await h.session.prompt({ sessionId: "t1", prompt: h.script({ reply: "Read it.", calls: [
            { tool: "read_skill", arguments: { name: kMySkill.name } },
        ] }) });
        h.session.close();

        expect(result.stopReason).toBe("end_turn");
        const prompt = h.logged("session/prompt")[0] as { prompt: { text: string }[] };
        expect(prompt.prompt[0]!.text).toStartWith("Be brief.\n\n## Skills\n");
        expect(prompt.prompt[0]!.text).toContain(`\n- ${kMySkill.name} (yours): ${kMySkill.description}\n`);
        expect(h.updates.find((u) => u.sessionUpdate === "tool_call_update")).toMatchObject({ status: "completed" });
    }, 30_000);

    test("one agent process and one ACP session serve every turn", async () => {
        const h = harness({ "lines.find": { revision: 1, total: 0, lines: [] } });
        for (const reply of ["one", "two"])
            expect((await h.session.prompt({ sessionId: "t1", prompt: h.script({ reply,
                calls: [{ tool: "lines_find", arguments: { text: reply } }] }) })).stopReason).toBe("end_turn");
        h.session.close();
        expect(h.spawned).toHaveLength(1);
        expect(h.logged("session/new")).toHaveLength(1);
        const prompts = h.logged("session/prompt") as { prompt: { text: string }[] }[];
        expect(prompts[1]!.prompt).toHaveLength(1);
    }, 30_000);

    test("Stop sends session/cancel, and the turn ends cancelled without the rest of its calls", async () => {
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
        expect(h.logged("session/cancel")).toEqual([{ sessionId: "session-1" }]);
    }, 30_000);

    test("Stop before the agent has opened its session ends the turn without a prompt", async () => {
        const h = harness();
        const result = h.session.prompt({ sessionId: "t1", prompt: h.script({ calls: [
            { tool: "document_apply", arguments: { revision: 1, edits: [] } },
        ] }) });
        h.session.cancel();
        expect((await result).stopReason).toBe("cancelled");
        h.session.close();
        expect(h.calls).toEqual([]);
        expect(h.logged("session/prompt")).toEqual([]);
    }, 30_000);

    test("when the agent exits the turn fails, and the next turn loads the session in a new process", async () => {
        const h = harness();
        await expect(h.session.prompt({ sessionId: "t1", prompt: h.script("exit") })).rejects.toThrow("ended");
        expect((await h.session.prompt({ sessionId: "t1", prompt: h.script({ reply: "back" }) })).stopReason).toBe("end_turn");
        h.session.close();
        expect(h.spawned).toHaveLength(2);
        expect(h.logged("session/load")).toEqual([{ sessionId: "session-1", cwd: "/work", mcpServers: [{ type: "http",
            name: "hikari", url: expect.stringContaining("/mcp"), headers: [{ name: "Authorization", value: `Bearer ${kToken}` }] }] }]);
    }, 30_000);

    test("an agent without session/load starts a new session after it exits", async () => {
        const h = harness({}, { FAKE_ACP_LOAD: "0" });
        await expect(h.session.prompt({ sessionId: "t1", prompt: h.script("exit") })).rejects.toThrow("ended");
        await h.session.prompt({ sessionId: "t1", prompt: h.script({ reply: "back" }) });
        h.session.close();
        expect(h.logged("session/load")).toHaveLength(0);
        expect(h.logged("session/new")).toHaveLength(2);
    }, 30_000);

    test("the joined AGENTS.md text comes before the first prompt of each new session, not after session/load", async () => {
        const joined = "You are Hikari AI.\n\nInstructions from this Project's AGENTS.md:\n\nKeep honorifics.";
        const intros = (h: ReturnType<typeof harness>) => (h.logged("session/prompt") as { prompt: { text?: string }[] }[])
            .map((p) => p.prompt.length > 1 && p.prompt[0]!.text === joined);

        const loads = harness({}, {}, joined);
        await loads.session.prompt({ sessionId: "t1", prompt: loads.script({ reply: "one" }) });
        await loads.session.prompt({ sessionId: "t1", prompt: loads.script({ reply: "two" }) });
        await expect(loads.session.prompt({ sessionId: "t1", prompt: loads.script("exit") })).rejects.toThrow("ended");
        await loads.session.prompt({ sessionId: "t1", prompt: loads.script({ reply: "back" }) });
        loads.session.close();
        expect(loads.logged("session/load")).toHaveLength(1);
        expect(intros(loads)).toEqual([true, false, false, false]);

        const fresh = harness({}, { FAKE_ACP_LOAD: "0" }, joined);
        await fresh.session.prompt({ sessionId: "t1", prompt: fresh.script({ reply: "one" }) });
        await expect(fresh.session.prompt({ sessionId: "t1", prompt: fresh.script("exit") })).rejects.toThrow("ended");
        await fresh.session.prompt({ sessionId: "t1", prompt: fresh.script({ reply: "back" }) });
        fresh.session.close();
        expect(fresh.logged("session/new")).toHaveLength(2);
        expect(intros(fresh)).toEqual([true, false, true]);
    }, 30_000);

    test("the sidecar grants no permission itself", async () => {
        const h = harness();
        await h.session.prompt({ sessionId: "t1", prompt: h.script({ askFor: "rm -rf /", reply: "ok" }) });
        h.session.close();
        expect(h.logged("permission")).toEqual([{ outcome: { outcome: "cancelled" } }]);
    }, 30_000);
});

describe("which tool calls are Hikari's", () => {
    test("by the agent's MCP naming or by its server in _meta", () => {
        expect(hikariTool({ title: "hikari_lines_find" })).toBe("lines_find");
        expect(hikariTool({ title: "mcp__hikari__document_apply" })).toBe("document_apply");
        expect(hikariTool({ title: "lines_find", _meta: { "gemini-cli": { mcp: { server_name: "hikari" } } } })).toBe("lines_find");
        expect(hikariTool({ title: "lines_find", _meta: { "gemini-cli": { mcp: { server_name: "other" } } } })).toBeUndefined();
        expect(hikariTool({ title: "Shell: rm -rf /" })).toBeUndefined();
        expect(hikariTool({ title: "hikari_" })).toBeUndefined();
    });

    test("as a sub-agent it completes an edit through _hikari/api.call, as its own session", async () => {
        const qt = qtLink({
            "lines.find": [{ revision: 3, total: 1, lines: [{ id: 1, row: 1, text: "Helo" }] }],
            "document.apply": [{ revision: 4, inserted: [], changed: true }],
        });
        const h = harness({}, {}, undefined, qt);
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
