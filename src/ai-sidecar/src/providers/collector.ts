import type { Citation, Finished, ProviderState, StopReason, ToolCall, Update, Usage, Wire } from "./types";

/** Gathers one completion's text, tool calls and usage, sending each piece to Qt as it arrives. */
export class Collector {
    usage: Usage = { inputTokens: 0, outputTokens: 0, cachedInputTokens: 0, cacheWriteTokens: 0, cacheWriteHourTokens: 0 };
    private text = "";
    private readonly calls = new Map<number, ToolCall>();
    private readonly citations = new Map<string, Citation>();
    private searchSuggestions: string | undefined;
    private providerState: ProviderState | undefined;

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

    /** A source the answer cites; the same URL cited again keeps its first number and quote. */
    addCitation(url: string, title: string, quote?: string): void {
        if (!url || this.citations.has(url))
            return;
        const citation: Citation = { index: this.citations.size + 1, url, title: title || url, ...(quote ? { quote } : {}) };
        this.citations.set(url, citation);
        this.emit({ kind: "citation", citation });
    }

    setSearchSuggestions(html: string | undefined): void {
        if (html)
            this.searchSuggestions = html;
    }

    keepProviderState(wire: Wire, parts: unknown[]): void {
        this.providerState = { type: "providerState", wire, json: JSON.stringify(parts) };
    }

    finish(reason: StopReason): Finished {
        this.emit({ kind: "usage", usage: this.usage });
        const toolCalls = [...this.calls.entries()].sort(([a], [b]) => a - b).map(([, call]) => call);
        return {
            reason, text: this.text, toolCalls, usage: this.usage,
            ...(this.citations.size ? { citations: [...this.citations.values()] } : {}),
            ...(this.searchSuggestions ? { searchSuggestions: this.searchSuggestions } : {}),
            ...(this.providerState ? { providerState: this.providerState } : {}),
        };
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
