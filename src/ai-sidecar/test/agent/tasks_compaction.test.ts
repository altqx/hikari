import { describe, expect, test } from "bun:test";

import { clearedResults, keptRanges, kSubagentResultsHeading, openTaskCalls } from "../../src/agent/compaction";
import { kTaskTools } from "../../src/agent/tasks";
import type { Message } from "../../src/providers/types";

const prompt = (text: string): Message => ({ author: "user", parts: [{ type: "text", text }] });
const answer = (text: string): Message => ({ author: "assistant", parts: [{ type: "text", text }] });
const call = (id: string, name: string, args: unknown = {}): Message =>
    ({ author: "assistant", parts: [{ type: "toolCall", id, name, argumentsJson: JSON.stringify(args) }] });
const result = (callId: string, value: unknown): Message =>
    ({ author: "user", parts: [{ type: "toolResult", callId, content: JSON.stringify(value) }] });
const task = (taskId: string, status: string, summary?: string) =>
    ({ taskId, childSessionId: `c-${taskId}`, title: taskId, status, ...(summary === undefined ? {} : { summary }) });
const isRead = (tool: string) => tool === "lines_find";

function history(after: Message[] = []): Message[] {
    return [
        prompt("one"), call("r1", "lines_find"), result("r1", { lines: [] }), answer("read"),
        prompt("two"), call("d1", "agent_delegate", { task: "Translate 1–150" }), result("d1", task("t1", "running")),
        answer("started"),
        prompt("three"), call("r2", "lines_find"), result("r2", { lines: [] }), answer("read again"),
        ...after,
        prompt("four"), answer("4"),
        prompt("five"), answer("5"),
    ];
}

describe("compaction with sub-agents", () => {
    test("no agent tool is read-only, so none is cleared as a read", () => {
        expect(kTaskTools.map((t) => t.annotations.readOnlyHint)).toEqual([false, false, false, false]);
    });

    test("an open task's call and result stay out of the summarised middle", () => {
        const h = history();
        expect([...openTaskCalls(h)]).toEqual(["d1"]);
        const { headEnd, tailStart } = keptRanges(h);
        expect(headEnd).toBe(1);
        expect(tailStart).toBe(4);
        expect(clearedResults(h, tailStart, isRead).map((s) => s.callId)).toEqual(["r1"]);
    });

    test("a task counts as ended once a status, a cancel or a wait shows it ended", () => {
        for (const [name, status] of [["agent_task_status", "done"], ["agent_task_cancel", "stopped"],
            ["agent_delegate", "failed"], ["agent_task_status", "interrupted"]] as const) {
            const h = history([call("s1", name, { taskId: "t1" }), result("s1", task("t1", status, "partial"))]);
            expect(openTaskCalls(h).size).toBe(0);
            expect(keptRanges(h).tailStart).toBe(h.findIndex((m) => m === h.at(-4)));
        }
        const stillRunning = history([call("s1", "agent_task_status", { taskId: "t1" }), result("s1", task("t1", "running", "so far"))]);
        expect([...openTaskCalls(stillRunning)]).toEqual(["d1"]);
    });

    test("Qt's results message ends the tasks it names", () => {
        const h = history([prompt(`${kSubagentResultsHeading}\n\nTask t1 (Part 1) done:\nTranslated 150 Lines.`), answer("ok")]);
        expect(openTaskCalls(h).size).toBe(0);
        const other = history([prompt(`${kSubagentResultsHeading}\n\nTask t2 (Part 2) done:\nx`), answer("ok")]);
        expect([...openTaskCalls(other)]).toEqual(["d1"]);
        const quoted = history([prompt("Task t1 (Part 1) done: says the user"), answer("ok")]);
        expect([...openTaskCalls(quoted)]).toEqual(["d1"]);
    });

    test("an ended task's older results with a summary are cleared like reads, an open one's never", () => {
        const waited = [prompt("two"), call("d1", "agent_delegate", { wait: true }), result("d1", task("t1", "done", "All 150 Lines.")),
            answer("done"), prompt("three"), call("s2", "agent_task_status"), result("s2", task("t2", "running", "half")),
            call("d2", "agent_delegate"), result("d2", task("t2", "running")), answer("ok")];
        const h = [prompt("one"), answer("1"), ...waited, prompt("four"), answer("4"), prompt("five"), answer("5")];
        const stubs = clearedResults(h, h.length - 4, isRead);
        expect(stubs.map((s) => s.callId)).toEqual(["d1"]);
        expect(stubs[0]!.content).toStartWith("[Cleared to save context: agent_delegate returned");
    });
});
