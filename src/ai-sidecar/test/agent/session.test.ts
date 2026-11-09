import { describe, expect, test } from "bun:test";
import { AgentSessions, kMaxTurnRequests, type Complete } from "../../src/agent/session";
import { ErrorCode, Method, type SessionUpdate, type SessionUpdateParams } from "../../src/protocol";
import { ProviderError } from "../../src/providers/errors";
import type { CompleteParams, Finished, Message, ToolCall, Usage } from "../../src/providers/types";
import { RpcError } from "../../src/rpc";
import type { OperationName, Operations } from "../../src/tools/api.gen";
import { ToolSet, type HikariCalls } from "../../src/tools/toolset";

const kUsage: Usage = { inputTokens: 10, outputTokens: 2, cachedInputTokens: 0, cacheWriteTokens: 0, cacheWriteHourTokens: 0 };

class FakeHa1 implements HikariCalls {
    calls: { operation: string; params: unknown }[] = [];
    /** Holds every call until `release` runs. */
    gate?: Promise<void>;

    async call<O extends OperationName>(operation: O, params?: Operations[O]["params"]): Promise<Operations[O]["result"]> {
        this.calls.push({ operation, params });
        await this.gate;
        if (operation === "lines.find")
            return { lines: [{ id: 3, text: "kept" }] } as never;
        return { revision: 2, inserted: [], changed: true } as never;
    }
}

type Step = { text?: string; calls?: [string, unknown][]; reason?: Finished["reason"]; fail?: ProviderError; hang?: boolean };

/** A model that answers each request with the next step and keeps every request it got. */
class ScriptedModel {
    requests: CompleteParams[] = [];
    constructor(private readonly steps: Step[], private readonly repeatLast = false) {}

    readonly complete: Complete = async (params, signal, emit) => {
        this.requests.push(structuredClone(params));
        const step = this.steps[Math.min(this.requests.length - 1, this.steps.length - 1)]!;
        if (!this.repeatLast && this.requests.length > this.steps.length)
            throw new Error("the script ran out");
        if (step.hang)
            await new Promise((_, reject) => signal.addEventListener("abort", () => reject(new ProviderError("cancelled"))));
        if (step.fail)
            throw step.fail;
        for (const piece of (step.text ?? "").match(/.{1,3}/gs) ?? [])
            emit(params.stream, { kind: "text", text: piece });
        const toolCalls: ToolCall[] = (step.calls ?? []).map(([name, args], i) =>
            ({ type: "toolCall", id: `c${this.requests.length}-${i}`, name, argumentsJson: JSON.stringify(args) }));
        return { reason: step.reason ?? (toolCalls.length ? "toolUse" : "end"), text: step.text ?? "", toolCalls, usage: kUsage };
    };
}

function harness(steps: Step[], repeatLast = false) {
    const ha1 = new FakeHa1();
    const model = new ScriptedModel(steps, repeatLast);
    const updates: SessionUpdate[] = [];
    const sessions = new AgentSessions(() => new ToolSet(ha1), model.complete, (method, params: SessionUpdateParams) => {
        expect(method).toBe(Method.sessionUpdate);
        expect(params.sessionId).toBe("t1");
        updates.push(params.update);
    });
    sessions.open({ sessionId: "t1", model: { providerId: "p", model: "m" }, system: "Be brief.", cacheKey: "t1" });
    const prompt = (text: string) => sessions.prompt({ sessionId: "t1", prompt: [{ type: "text", text }] });
    const of = <K extends SessionUpdate["sessionUpdate"]>(kind: K) =>
        updates.filter((u): u is Extract<SessionUpdate, { sessionUpdate: K }> => u.sessionUpdate === kind);
    return { ha1, model, updates, sessions, prompt, of };
}

const history = (updates: SessionUpdate[]): Message[] =>
    updates.flatMap((u) => u.sessionUpdate === "history" ? [u.message] : []);

describe("AgentSession", () => {
    test("a turn streams its text in chunks and keeps the user's message and the answer", async () => {
        const h = harness([{ text: "Hello there, friend." }]);
        expect(await h.prompt("Hi")).toEqual({ stopReason: "end_turn" });
        expect(h.of("agent_message_chunk").map((u) => u.content.text).join("")).toBe("Hello there, friend.");
        expect(h.of("agent_message_chunk").length).toBeLessThan(7);
        expect(history(h.updates)).toEqual([
            { author: "user", parts: [{ type: "text", text: "Hi" }] },
            { author: "assistant", parts: [{ type: "text", text: "Hello there, friend." }] },
        ]);
        expect(h.of("usage")).toEqual([{ sessionUpdate: "usage", usage: kUsage }]);
        const request = h.model.requests[0]!.request;
        expect(request.system).toBe("Be brief.");
        expect(request.cacheKey).toBe("t1");
        expect(request.tools!.map((t) => t.name)).toContain("update_plan");
        expect(request.tools!.map((t) => t.name)).toContain("document_apply");
    });

    test("tool calls run through Hikari's tools and their results go back to the model", async () => {
        const h = harness([
            { text: "Looking.", calls: [["lines_find", { query: "kept" }]] },
            { calls: [["document_apply", { revision: 1, name: "Fix", edits: [] }]] },
            { text: "Done." },
        ]);
        expect(await h.prompt("Fix it")).toEqual({ stopReason: "end_turn" });
        expect(h.ha1.calls.map((c) => c.operation)).toEqual(["lines.find", "document.apply"]);
        expect(h.of("tool_call").map((u) => [u.title, u.kind])).toEqual([["lines_find", "read"], ["document_apply", "edit"]]);
        expect(h.of("tool_call_update").map((u) => u.status)).toEqual(["completed", "completed"]);
        const second = h.model.requests[1]!.request.messages;
        expect(second.at(-1)).toEqual({ author: "user", parts: [{ type: "toolResult", callId: "c1-0",
            content: JSON.stringify({ lines: [{ id: 3, text: "kept" }] }) }] });
        expect(history(h.updates).map((m) => m.author)).toEqual(["user", "assistant", "user", "assistant", "user", "assistant"]);
    });

    test("a call with broken arguments or an unknown tool fails for the model, not the turn", async () => {
        const h = harness([{ calls: [["no_such_tool", {}]] }, { text: "Sorry." }]);
        expect(await h.prompt("Go")).toEqual({ stopReason: "end_turn" });
        expect(h.of("tool_call_update")[0]!.status).toBe("failed");
        const result = h.model.requests[1]!.request.messages.at(-1)!.parts[0];
        expect(result).toMatchObject({ type: "toolResult", isError: true });
    });

    test("update_plan shows the plan without reaching Hikari", async () => {
        const h = harness([
            { calls: [["update_plan", { steps: [{ step: "Read", status: "completed" }, { step: "Fix", status: "in_progress" }] }]] },
            { calls: [["update_plan", { steps: [{ step: "Read" }] }]] },
            { text: "ok" },
        ]);
        await h.prompt("Plan");
        expect(h.ha1.calls).toEqual([]);
        expect(h.of("plan")).toEqual([{ sessionUpdate: "plan", entries: [
            { content: "Read", priority: "medium", status: "completed" },
            { content: "Fix", priority: "medium", status: "in_progress" },
        ] }]);
        expect(h.of("tool_call_update").map((u) => u.status)).toEqual(["completed", "failed"]);
    });

    test("a steer joins the turn after its next tool call", async () => {
        const h = harness([{ calls: [["lines_find", { query: "a" }]] }, { text: "ok" }]);
        let release!: () => void;
        h.ha1.gate = new Promise((r) => (release = r));
        const turn = h.prompt("Go");
        await Bun.sleep(0);
        const steer = h.sessions.steer({ sessionId: "t1", prompt: [{ type: "text", text: "Only Line 3" }] });
        release();
        expect(await steer).toEqual({ taken: true });
        await turn;
        expect(h.model.requests[1]!.request.messages.at(-1)!.parts.at(-1)).toEqual({ type: "text", text: "Only Line 3" });
    });

    test("a steer that the turn ends before is handed back for the next turn", async () => {
        const h = harness([{ text: "Done.", hang: false }]);
        const turn = h.prompt("Go");
        const steer = h.sessions.steer({ sessionId: "t1", prompt: [{ type: "text", text: "Also this" }] });
        await turn;
        expect(await steer).toEqual({ taken: false });
        expect(await h.sessions.steer({ sessionId: "t1", prompt: [{ type: "text", text: "idle" }] })).toEqual({ taken: false });
    });

    test("cancel during a model request ends the turn as cancelled", async () => {
        const h = harness([{ hang: true }]);
        const turn = h.prompt("Go");
        await Bun.sleep(0);
        h.sessions.cancel("t1");
        expect(await turn).toEqual({ stopReason: "cancelled" });
        expect(history(h.updates)).toEqual([{ author: "user", parts: [{ type: "text", text: "Go" }] }]);
    });

    test("cancel during a tool call lets that call finish and answers the rest so the history stays whole", async () => {
        const h = harness([{ calls: [["lines_find", { query: "a" }], ["document_apply", { revision: 1, name: "x", edits: [] }]] }]);
        let release!: () => void;
        h.ha1.gate = new Promise((r) => (release = r));
        const turn = h.prompt("Go");
        await Bun.sleep(0);
        h.sessions.cancel("t1");
        release();
        expect(await turn).toEqual({ stopReason: "cancelled" });
        expect(h.ha1.calls.map((c) => c.operation)).toEqual(["lines.find"]);
        const results = history(h.updates).at(-1)!.parts;
        expect(results.map((p) => p.type === "toolResult" && p.callId)).toEqual(["c1-0", "c1-1"]);
        expect(results[1]).toMatchObject({ isError: true, content: "The user stopped the turn." });
    });

    test("a second prompt while a turn runs is refused", async () => {
        const h = harness([{ hang: true }]);
        const turn = h.prompt("Go");
        await Bun.sleep(0);
        const second = h.prompt("Again").catch((e: unknown) => e);
        expect(((await second) as RpcError).code).toBe(ErrorCode.busy);
        h.sessions.cancel("t1");
        await turn;
    });

    test("a turn stops after the most model requests one turn may make", async () => {
        const h = harness([{ calls: [["lines_find", { query: "a" }]] }], true);
        expect(await h.prompt("Loop")).toEqual({ stopReason: "max_turn_requests" });
        expect(h.model.requests.length).toBe(kMaxTurnRequests);
    });

    test("stop reasons follow ACP's", async () => {
        expect(await harness([{ text: "cut", reason: "maxTokens" }]).prompt("a")).toEqual({ stopReason: "max_tokens" });
        expect(await harness([{ text: "", reason: "refused" }]).prompt("a")).toEqual({ stopReason: "refusal" });
    });

    test("a provider failure fails the prompt and the session can take the next one", async () => {
        const h = harness([{ fail: new ProviderError("rateLimited", "slow down", 429) }, { text: "ok" }]);
        expect(h.prompt("a")).rejects.toBeInstanceOf(ProviderError);
        await Bun.sleep(0);
        expect(await h.prompt("b")).toEqual({ stopReason: "end_turn" });
    });

    test("a session opened with Qt's saved history continues it, and a prompt can switch the model", async () => {
        const h = harness([{ text: "again" }]);
        const saved: Message[] = [{ author: "user", parts: [{ type: "text", text: "earlier" }] },
            { author: "assistant", parts: [{ type: "text", text: "reply" }] }];
        h.sessions.open({ sessionId: "t1", model: { providerId: "p", model: "m" }, history: saved });
        await h.sessions.prompt({ sessionId: "t1", prompt: [{ type: "text", text: "now" }], model: { providerId: "q", model: "n" } });
        const sent = h.model.requests[0]!;
        expect(sent.providerId).toBe("q");
        expect(sent.request.model).toBe("n");
        expect(sent.request.messages.map((m) => m.parts[0])).toEqual([{ type: "text", text: "earlier" },
            { type: "text", text: "reply" }, { type: "text", text: "now" }]);
    });

    test("an unknown session is refused", () => {
        const h = harness([]);
        expect(() => h.sessions.prompt({ sessionId: "nope", prompt: [{ type: "text", text: "a" }] })).toThrow(RpcError);
        h.sessions.close("t1");
        expect(h.sessions.has("t1")).toBe(false);
    });
});
