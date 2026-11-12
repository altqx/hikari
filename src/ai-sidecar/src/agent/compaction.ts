import type { CompactedUpdate } from "../protocol";
import type { CompleteParams, Message, Part, ToolDefinition } from "../providers/types";

/** The context window assumed when the catalogue doesn't say. */
export const kDefaultContextTokens = 128_000;
/** Automatic compaction starts at this share of the context window. */
export const kCompactAt = 0.9;
/** Stage 1 must bring the conversation under this share, or stage 2 runs. */
export const kClearedTarget = 0.6;
/** The longest summary stage 2 asks for. */
export const kSummaryTokens = 8192;
export const kCharsPerToken = 4;
/** What an image counts for, since its base64 says little about its tokens. */
const kImageTokens = 1600;
/** How much of one tool call or result the summary request shows. */
const kTranscriptPartChars = 2000;

export const kSummaryHeading = "Summary of the conversation so far";

const kSummarySystem = [
    "You compact a conversation between a user and an assistant that edits subtitles in Hikari.",
    "The assistant continues from your summary and no longer sees the transcript, so keep every fact it needs.",
    "Answer with these Markdown sections, in this order, and nothing else:",
    "## Goal", "## Decisions", "## Files and Lines touched", "## Open work", "## User preferences",
    "Under each, write short bullet points. Name files, Line numbers, styles and tool results exactly.",
    `If the transcript holds an earlier "${kSummaryHeading}", fold it in: keep what still holds and drop what changed.`,
].join("\n");

function partChars(part: Part): number {
    switch (part.type) {
    case "text": return part.text.length;
    case "image": return kImageTokens * kCharsPerToken;
    case "toolCall": return part.name.length + part.argumentsJson.length;
    case "toolResult": return part.content.length;
    case "providerState": return part.json.length;
    }
}

export function messageChars(messages: readonly Message[]): number {
    return messages.reduce((sum, m) => sum + m.parts.reduce((s, p) => s + partChars(p), 0), 0);
}

export function prefixChars(system: string | undefined, tools: readonly ToolDefinition[]): number {
    return (system?.length ?? 0) + tools.reduce((sum, t) => sum + t.name.length + t.description.length + t.parametersJson.length, 0);
}

/**
 * The last response's real input tokens, plus about four characters per token for what came since. Before any response
 * it is all estimated.
 */
export class TokenCount {
    private measured?: { tokens: number; chars: number };

    measure(tokens: number, chars: number): void {
        this.measured = { tokens, chars };
    }

    estimate(chars: number): number {
        if (!this.measured)
            return Math.ceil(chars / kCharsPerToken);
        return Math.max(0, Math.ceil(this.measured.tokens + (chars - this.measured.chars) / kCharsPerToken));
    }
}

function isPrompt(message: Message): boolean {
    return message.author === "user" && !message.parts.some((p) => p.type === "toolResult");
}

/** The heading of the user message in which Qt hands finished sub-agents' results back. */
export const kSubagentResultsHeading = "Results from your sub-agents:";
const kEnded = new Set(["done", "failed", "stopped", "interrupted"]);
const kTaskResults = new Set(["agent_delegate", "agent_task_status", "agent_task_cancel"]);

interface TaskResult {
    taskId: string;
    status?: string;
    summary?: string;
}

function taskResult(content: string): TaskResult | undefined {
    try {
        const value = JSON.parse(content) as Partial<TaskResult> | null;
        return value && typeof value.taskId === "string" ? value as TaskResult : undefined;
    } catch {
        return undefined;
    }
}

/** Each sub-agent task the history started, by the call that started it, and whether it has ended. */
function tasks(history: readonly Message[]): { started: Map<string, string>; ended: Set<string> } {
    const names = new Map<string, string>();
    const started = new Map<string, string>();
    const ended = new Set<string>();
    for (const message of history)
        for (const part of message.parts) {
            if (part.type === "toolCall")
                names.set(part.id, part.name);
            else if (part.type === "toolResult" && !part.isError && kTaskResults.has(names.get(part.callId) ?? "")) {
                const result = taskResult(part.content);
                if (!result)
                    continue;
                if (names.get(part.callId) === "agent_delegate")
                    started.set(part.callId, result.taskId);
                if (result.status !== undefined && kEnded.has(result.status))
                    ended.add(result.taskId);
            } else if (part.type === "text" && message.author === "user" && part.text.startsWith(kSubagentResultsHeading)) {
                for (const match of part.text.matchAll(/^Task (\S+) \(/gm))
                    ended.add(match[1]!);
            }
        }
    return { started, ended };
}

/** The calls that started a task still open: they and their results stay word for word. */
export function openTaskCalls(history: readonly Message[]): Set<string> {
    const { started, ended } = tasks(history);
    return new Set([...started].filter(([, taskId]) => !ended.has(taskId)).map(([callId]) => callId));
}

/**
 * The kept head ends after the first user message; the kept tail starts at the second-to-last one, or earlier, at the
 * user message before the first call that started a task still open.
 */
export function keptRanges(history: readonly Message[]): { headEnd: number; tailStart: number } {
    const prompts = history.flatMap((m, i) => isPrompt(m) ? [i] : []);
    if (prompts.length === 0)
        return { headEnd: history.length, tailStart: history.length };
    const headEnd = prompts[0]! + 1;
    let tailStart = prompts[Math.max(0, prompts.length - 2)]!;
    const open = openTaskCalls(history);
    const firstOpen = history.findIndex((m) => m.parts.some((p) => p.type === "toolCall" && open.has(p.id)));
    if (firstOpen >= 0 && firstOpen < tailStart)
        tailStart = prompts.filter((i) => i <= firstOpen).at(-1) ?? tailStart;
    return { headEnd, tailStart: Math.max(headEnd, tailStart) };
}

function stubOf(name: string, content: string): string {
    return `[Cleared to save context: ${name} returned ${content.length} characters. Call it again if you need them.]`;
}

function isStub(content: string): boolean {
    return content.startsWith("[Cleared to save context: ");
}

/** Stage 1: each old read-tool result, and each ended sub-agent's result, becomes a one-line stub. */
export function clearedResults(history: readonly Message[], end: number, isRead: (tool: string) => boolean)
    : { callId: string; content: string }[] {
    const { ended } = tasks(history);
    const names = new Map<string, string>();
    const stubs: { callId: string; content: string }[] = [];
    const endedResult = (name: string, content: string) => {
        if (name !== "agent_delegate" && name !== "agent_task_status")
            return false;
        const result = taskResult(content);
        return result?.summary !== undefined && ended.has(result.taskId);
    };
    for (const message of history.slice(0, end))
        for (const part of message.parts) {
            if (part.type === "toolCall")
                names.set(part.id, part.name);
            else if (part.type === "toolResult" && !isStub(part.content)) {
                const name = names.get(part.callId);
                if (name !== undefined && (isRead(name) || (!part.isError && endedResult(name, part.content))))
                    stubs.push({ callId: part.callId, content: stubOf(name, part.content) });
            }
        }
    return stubs;
}

export function withStubs(history: readonly Message[], stubs: readonly { callId: string; content: string }[]): Message[] {
    if (stubs.length === 0)
        return [...history];
    const byCall = new Map(stubs.map((s) => [s.callId, s.content]));
    return history.map((message) => message.parts.some((p) => p.type === "toolResult" && byCall.has(p.callId))
        ? { ...message, parts: message.parts.map((p) => p.type === "toolResult" && byCall.has(p.callId)
            ? { ...p, content: byCall.get(p.callId)! } : p) }
        : message);
}

function clip(text: string): string {
    return text.length <= kTranscriptPartChars ? text : `${text.slice(0, kTranscriptPartChars)}…`;
}

function transcriptOf(messages: readonly Message[]): string {
    const names = new Map<string, string>();
    const lines: string[] = [];
    for (const message of messages) {
        const said: string[] = [];
        for (const part of message.parts) {
            if (part.type === "text")
                said.push(part.text);
            else if (part.type === "image")
                said.push("[an image]");
            else if (part.type === "toolCall") {
                names.set(part.id, part.name);
                said.push(`[called ${part.name} with ${clip(part.argumentsJson)}]`);
            } else if (part.type === "toolResult")
                said.push(`[${names.get(part.callId) ?? "a tool"} ${part.isError ? "failed" : "returned"}: ${clip(part.content)}]`);
        }
        if (said.length > 0)
            lines.push(`${message.author === "user" ? "User" : "Assistant"}: ${said.join("\n")}`);
    }
    return lines.join("\n\n");
}

export function summaryRequest(messages: readonly Message[], instruction: string | undefined,
    model: { providerId: string; model: string }, stream: number): CompleteParams {
    const asked = instruction?.trim() ? `\n\nThe user asks for this summary: ${instruction.trim()}` : "";
    return {
        stream,
        providerId: model.providerId,
        request: {
            model: model.model,
            system: kSummarySystem,
            messages: [{ author: "user", parts: [{ type: "text", text: `<transcript>\n${transcriptOf(messages)}\n</transcript>${asked}` }] }],
            maxOutputTokens: kSummaryTokens,
        },
    };
}

export function summaryMessage(summary: string): Message {
    return { author: "assistant", parts: [{ type: "text", text: `${kSummaryHeading}\n\n${summary.trim()}` }] };
}

/** Applies a `compacted` update to a history, the same way Qt applies it to its copy. */
export function applyCompaction(history: readonly Message[], update: Omit<CompactedUpdate, "sessionUpdate">): Message[] {
    const stubbed = withStubs(history, update.stubs);
    if (update.message === undefined || update.headEnd === undefined || update.keptFrom === undefined)
        return stubbed;
    return [...stubbed.slice(0, update.headEnd), update.message, ...stubbed.slice(update.keptFrom)];
}
