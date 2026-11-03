import type { Finished, StopReason, ToolCall, Update, Usage } from "./types";

/** Gathers one completion's text, tool calls and usage, sending each piece to Qt as it arrives. */
export class Collector {
    usage: Usage = { inputTokens: 0, outputTokens: 0, cachedInputTokens: 0, cacheWriteTokens: 0, cacheWriteHourTokens: 0 };
    private text = "";
    private readonly calls = new Map<number, ToolCall>();

    constructor(private readonly emit: (update: Update) => void) {}

    get callCount(): number {
        return this.calls.size;
    }

    addText(text: string | undefined): void {
        if (!text)
            return;
        this.text += text;
        this.emit({ kind: "text", text });
    }

    startCall(index: number, id: string, name: string): void {
        const call = this.calls.get(index);
        if (call) {
            call.id ||= id;
            call.name ||= name;
            return;
        }
        this.calls.set(index, { type: "toolCall", id, name, argumentsJson: "" });
        this.emit({ kind: "toolCall", index, id, name });
    }

    addArguments(index: number, delta: string | undefined): void {
        const call = this.calls.get(index);
        if (!call || !delta)
            return;
        call.argumentsJson += delta;
        this.emit({ kind: "toolArguments", index, delta });
    }

    setSignature(index: number, signature: string | undefined): void {
        const call = this.calls.get(index);
        if (call && signature)
            call.signature = signature;
    }

    finish(reason: StopReason): Finished {
        this.emit({ kind: "usage", usage: this.usage });
        const toolCalls = [...this.calls.entries()].sort(([a], [b]) => a - b).map(([, call]) => call);
        return { reason, text: this.text, toolCalls, usage: this.usage };
    }
}

/** A request's JSON Schema text as an object; an empty or broken one becomes `{type: "object"}`. */
export function schemaObject(json: string): Record<string, unknown> {
    try {
        const parsed: unknown = JSON.parse(json);
        if (typeof parsed === "object" && parsed !== null && !Array.isArray(parsed) && Object.keys(parsed).length > 0)
            return parsed as Record<string, unknown>;
    } catch {}
    return { type: "object" };
}

export function argumentsObject(json: string): Record<string, unknown> {
    try {
        const parsed: unknown = JSON.parse(json || "{}");
        if (typeof parsed === "object" && parsed !== null && !Array.isArray(parsed))
            return parsed as Record<string, unknown>;
    } catch {}
    return {};
}
