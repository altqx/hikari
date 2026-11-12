import { describe, expect, test } from "bun:test";
import {
    applyCompaction, kCharsPerToken, kClearedTarget, keptRanges, kSummaryHeading, kSummaryTokens, messageChars, prefixChars,
} from "../../src/agent/compaction";
import { AgentSessions, type Complete } from "../../src/agent/session";
import { ErrorCode, type CompactedUpdate, type SessionUpdate, type SessionUpdateParams } from "../../src/protocol";
import { ProviderError } from "../../src/providers/errors";
import type { CompleteParams, Message, ToolCall } from "../../src/providers/types";
import type { OperationName, Operations } from "../../src/tools/api.gen";
import { ToolSet, type HikariCalls } from "../../src/tools/toolset";

class BigReads implements HikariCalls {
    size = 10;

    async call<O extends OperationName>(operation: O): Promise<Operations[O]["result"]> {
        if (operation === "lines.find")
            return { lines: [{ id: 3, text: "r".repeat(this.size) }] } as never;
        return { revision: 2, inserted: [], changed: true } as never;
    }
}

type Step = { text?: string; calls?: [string, unknown][] };
type Summary = { text?: string; fail?: ProviderError };

const isSummary = (params: CompleteParams) => params.request.system?.startsWith("You compact") === true;

/** Answers turns from `steps` and summary requests from `summaries`, and reports the input tokens a real model would. */
class ScriptedModel {
    requests: CompleteParams[] = [];
    summaries: CompleteParams[] = [];
    constructor(readonly steps: Step[], private readonly summaryAnswers: Summary[] = []) {}

    readonly complete: Complete = async (params) => {
        const usage = (input: number) =>
            ({ inputTokens: input, outputTokens: 5, cachedInputTokens: 0, cacheWriteTokens: 0, cacheWriteHourTokens: 0 });
        if (isSummary(params)) {
            this.summaries.push(structuredClone(params));
            const answer = this.summaryAnswers.shift();
            if (!answer || answer.fail)
                throw answer?.fail ?? new Error("no summary scripted");
            return { reason: "end", text: answer.text ?? "", toolCalls: [], usage: usage(10) };
        }
        this.requests.push(structuredClone(params));
        const step = this.steps.shift();
        if (!step)
            throw new Error("the script ran out");
        const toolCalls: ToolCall[] = (step.calls ?? []).map(([name, args], i) =>
            ({ type: "toolCall", id: `c${this.requests.length}-${i}`, name, argumentsJson: JSON.stringify(args) }));
        const chars = prefixChars(params.request.system,
            params.request.tools ?? []) + messageChars(params.request.messages);
        return { reason: toolCalls.length ? "toolUse" : "end", text: step.text ?? "", toolCalls,
            usage: usage(Math.ceil(chars / kCharsPerToken)) };
    };
}

function harness(steps: Step[], summaries: Summary[] = [], autoCompact = true) {
    const model = new ScriptedModel(steps, summaries);
    const reads = new BigReads();
    const updates: SessionUpdate[] = [];
    const sessions = new AgentSessions(() => new ToolSet(reads), model.complete,
        (_method, params: SessionUpdateParams) => updates.push(params.update));
    sessions.open({ sessionId: "t1", model: { providerId: "p", model: "m" }, system: "Be brief.", autoCompact });
    let window = 1_000_000;
    const prompt = (text: string) =>
        sessions.prompt({ sessionId: "t1", prompt: [{ type: "text", text }], contextTokens: window });
    const setWindow = (tokens: number) => { window = tokens; };
    const prefixTokens = () => {
        const first = model.requests[0]!;
        return prefixChars(first.request.system, first.request.tools ?? []) / kCharsPerToken;
    };
    const compacted = () => updates.filter((u): u is CompactedUpdate => u.sessionUpdate === "compacted");
    const lastMessages = () => model.requests.at(-1)!.request.messages;
    const history = () => (sessions.get("t1") as unknown as { history: Message[] }).history;
    /** After one short turn: `big` tokens per later answer or read, as `times` prefixes, and a window that `count` of them fill to `share`. */
    const fill = async (times: number, count: number, share: number) => {
        await prompt("first");
        const big = Math.ceil(times * prefixTokens());
        reads.size = big * kCharsPerToken;
        const window = Math.floor((prefixTokens() + count * big) / share);
        setWindow(window);
        return { big, window, text: "w".repeat(big * kCharsPerToken) };
    };
    return { model, updates, sessions, prompt, setWindow, prefixTokens, compacted, lastMessages, history, fill };
}

const read = (text: string): Step[] => [{ calls: [["lines_find", { query: "x" }]] }, { text }];
const talk = (text: string): Step[] => [{ text }];
const kept = (messages: readonly Message[]) => JSON.stringify(messages);

describe("compaction", () => {
    test("the kept head ends after the first prompt and the tail starts at the second-to-last", () => {
        const u = (text: string): Message => ({ author: "user", parts: [{ type: "text", text }] });
        const a: Message = { author: "assistant", parts: [{ type: "text", text: "ok" }] };
        const results: Message = { author: "user", parts: [{ type: "toolResult", callId: "c", content: "x" }] };
        expect(keptRanges([u("1"), a, results, a, u("2"), a, u("3"), a])).toEqual({ headEnd: 1, tailStart: 4 });
        expect(keptRanges([u("1"), a, u("2"), a])).toEqual({ headEnd: 1, tailStart: 1 });
    });

    test("stage 1 runs mid-turn past 90%, clears the old reads to stubs and keeps the rest byte for byte", async () => {
        const h = harness([{ text: "ok" }]);
        const { window } = await h.fill(2, 4, 0.92);
        h.model.steps.push(...read("one"), ...read("two"), ...read("three"), ...read("four"));
        h.model.steps.at(-1)!.text = "done";
        await h.prompt("second");
        await h.prompt("third");
        await h.prompt("fourth");
        expect(h.compacted()).toEqual([]);
        const before = structuredClone(h.history());
        await h.prompt("fifth");

        const [update] = h.compacted();
        expect(update?.stage).toBe(1);
        expect(update!.before).toBeGreaterThanOrEqual(0.9 * window);
        expect(update!.stubs.map((s) => s.callId)).toEqual(["c2-0", "c4-0"]);
        expect(update!.stubs[0]!.content).toMatch(/^\[Cleared to save context: lines_find returned \d+ characters/);
        expect(update!.after).toBeLessThan(kClearedTarget * window);
        expect(h.model.summaries).toEqual([]);

        const sent = h.lastMessages();
        const { headEnd, tailStart } = keptRanges(sent);
        expect(kept(sent.slice(0, headEnd))).toBe(kept(before.slice(0, headEnd)));
        expect(kept(sent.slice(tailStart, before.length))).toBe(kept(before.slice(tailStart)));
        expect(sent.length).toBe(before.length + 3);
        expect(h.updates.findIndex((u) => u.sessionUpdate === "compacted"))
            .toBeGreaterThan(h.updates.findLastIndex((u) => u.sessionUpdate === "tool_call_update"));
    });

    test("stage 2 runs when stage 1 leaves 60% or more, and lands under 40% with the kept parts unchanged", async () => {
        const h = harness([{ text: "ok" }], [{ text: "## Goal\n- QC Ep06" }]);
        const { window, text } = await h.fill(4, 3, 0.92);
        h.model.steps.push(...talk(text), ...talk(text), ...talk(text), ...talk("c"));
        for (const prompt of ["second", "third", "fourth"])
            await h.prompt(prompt);
        expect(h.compacted()).toEqual([]);
        const before = structuredClone(h.history());
        await h.prompt("fifth");

        const [update] = h.compacted();
        expect(update?.stage).toBe(2);
        expect(update!.before).toBeGreaterThanOrEqual(0.9 * window);
        expect(update!.after).toBeLessThan(0.4 * window);
        expect(update!.summary).toBe("## Goal\n- QC Ep06");
        expect(h.model.summaries[0]!.request.maxOutputTokens).toBe(kSummaryTokens);
        const sent = h.lastMessages();
        const { headEnd, tailStart } = keptRanges([...before, { author: "user", parts: [{ type: "text", text: "fifth" }] }]);
        expect(kept(sent.slice(0, headEnd))).toBe(kept(before.slice(0, headEnd)));
        expect(sent[headEnd]).toEqual({ author: "assistant", parts: [{ type: "text", text: `${kSummaryHeading}\n\n## Goal\n- QC Ep06` }] });
        expect(kept(sent.slice(headEnd + 1, headEnd + 1 + before.length - tailStart))).toBe(kept(before.slice(tailStart)));
        expect(kept(applyCompaction([...before, sent.at(-1)!], update!))).toBe(kept(sent));
        expect(h.model.requests[0]!.request.system).toBe(h.model.requests.at(-1)!.request.system);
        expect(JSON.stringify(h.model.requests[0]!.request.tools)).toBe(JSON.stringify(h.model.requests.at(-1)!.request.tools));
        expect(h.updates.some((u) => u.sessionUpdate === "usage" && u.compaction === true)).toBe(true);
    });

    test("/compact passes its instruction, and the next compaction folds the first summary in", async () => {
        const h = harness([...talk("1"), ...talk("2"), ...talk("3"), ...talk("4")],
            [{ text: "first summary" }, { text: "second summary" }]);
        for (const text of ["a", "b", "c"])
            await h.prompt(text);
        expect(await h.sessions.compact({ sessionId: "t1", instruction: "keep the glossary decisions" }))
            .toEqual({ compacted: true, summary: "first summary" });
        expect(JSON.stringify(h.model.summaries[0]!.request.messages)).toContain("keep the glossary decisions");
        await h.prompt("d");
        expect((await h.sessions.compact({ sessionId: "t1" })).summary).toBe("second summary");
        const transcript = JSON.stringify(h.model.summaries[1]!.request.messages);
        expect(transcript).toContain(kSummaryHeading);
        expect(transcript).toContain("first summary");
    });

    test("/compact is refused while a turn runs", async () => {
        const h = harness([{ calls: [["lines_find", { query: "x" }]] }, { text: "done" }]);
        const running = h.prompt("a");
        await expect(h.sessions.compact({ sessionId: "t1" })).rejects.toMatchObject({ code: ErrorCode.busy });
        await running;
    });

    test("a failed summary is retried once; two failures stop the turn and remove nothing", async () => {
        const fail = new ProviderError("timeout", "provider timeout");
        const retried = harness([...talk("1"), ...talk("2"), ...talk("3")], [{ fail }, { text: "kept" }]);
        for (const text of ["a", "b", "c"])
            await retried.prompt(text);
        expect((await retried.sessions.compact({ sessionId: "t1" })).summary).toBe("kept");
        expect(retried.model.summaries.length).toBe(2);

        const h = harness([{ text: "ok" }], [{ fail }, { fail }]);
        const { text } = await h.fill(4, 3, 0.92);
        h.model.steps.push(...talk(text), ...talk(text), ...talk(text));
        for (const prompt of ["second", "third", "fourth"])
            await h.prompt(prompt);
        const before = kept(h.history());
        expect(await h.prompt("fifth")).toEqual({ stopReason: "compaction_failed" });
        expect(h.updates.filter((u) => u.sessionUpdate === "compaction_failed"))
            .toEqual([{ sessionUpdate: "compaction_failed", reason: "provider timeout" }]);
        expect(h.compacted()).toEqual([]);
        expect(kept(h.history().slice(0, -1))).toBe(before);

        const failing = harness([...talk("1"), ...talk("2"), ...talk("3")], [{ fail }, { fail }]);
        for (const text of ["a", "b", "c"])
            await failing.prompt(text);
        await expect(failing.sessions.compact({ sessionId: "t1" })).rejects.toBeInstanceOf(ProviderError);
    });

    test("with automatic compaction off, nothing compacts past 90%", async () => {
        const h = harness([{ text: "ok" }], [], false);
        const { text } = await h.fill(4, 3, 0.92);
        h.model.steps.push(...talk(text), ...talk(text), ...talk(text), ...talk("end"));
        for (const prompt of ["second", "third", "fourth", "fifth"])
            await h.prompt(prompt);
        expect(h.compacted()).toEqual([]);
        expect(h.model.summaries).toEqual([]);
    });

    test("apply: false summarises the whole thread and changes nothing", async () => {
        const h = harness([...talk("1"), ...talk("2")], [{ text: "all of it" }]);
        await h.prompt("a");
        await h.prompt("b");
        const before = kept(h.history());
        expect(await h.sessions.compact({ sessionId: "t1", apply: false })).toEqual({ compacted: false, summary: "all of it" });
        expect(kept(h.history())).toBe(before);
        expect(JSON.stringify(h.model.summaries[0]!.request.messages)).toContain("User: b");
    });
});
