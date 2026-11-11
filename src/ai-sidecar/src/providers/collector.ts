import type { Citation, Finished, ProviderState, StopReason, ToolCall, Update, Usage, WebAction, Wire } from "./types";

/**
 * Where a provider says a citation's text is, in its own units, from `base` (UTF-16, into the collected text). The
 * collector maps it onto the final text, and drops it when it doesn't land on whole characters or on `text`.
 */
export interface Placement {
    unit: "utf16" | "codePoints" | "utf8";
    base: number;
    start: number;
    end: number;
    text?: string;
}

/** `offset` in `unit` from the start of `text` as UTF-16, or undefined when it is past the end or inside a character. */
function utf16At(text: string, offset: number, unit: Placement["unit"]): number | undefined {
    if (!Number.isInteger(offset) || offset < 0)
        return undefined;
    if (unit === "utf16")
        return offset <= text.length && !(offset > 0 && offset < text.length && isLowSurrogate(text, offset))
            ? offset : undefined;
    let i = 0;
    for (let counted = 0; counted < offset; ) {
        if (i >= text.length)
            return undefined;
        const code = text.codePointAt(i)!;
        counted += unit === "codePoints" ? 1 : code < 0x80 ? 1 : code < 0x800 ? 2 : code < 0x10000 ? 3 : 4;
        if (counted > offset)
            return undefined;
        i += code > 0xffff ? 2 : 1;
    }
    return i;
}

function isLowSurrogate(text: string, at: number): boolean {
    const unit = text.charCodeAt(at);
    return unit >= 0xdc00 && unit <= 0xdfff;
}

/** Gathers one completion's text, tool calls and usage, sending each piece to Qt as it arrives. */
export class Collector {
    usage: Usage = { inputTokens: 0, outputTokens: 0, cachedInputTokens: 0, cacheWriteTokens: 0, cacheWriteHourTokens: 0 };
    private text = "";
    private readonly calls = new Map<number, ToolCall>();
    private readonly citations = new Map<string, Citation>();
    private readonly placements = new Map<string, Placement>();
    private searchSuggestions: string | undefined;
    private readonly searches: WebAction[] = [];
    private providerState: ProviderState | undefined;

    constructor(private readonly emit: (update: Update) => void) {}

    /** The text so far in UTF-16 code units, where the next text starts. */
    get textLength(): number {
        return this.text.length;
    }

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

    /** A source the answer cites; the same URL cited again keeps its first number, quote and placement. */
    addCitation(url: string, title: string, quote?: string, placement?: Placement): void {
        if (!url)
            return;
        if (placement && !this.placements.has(url))
            this.placements.set(url, placement);
        if (this.citations.has(url))
            return;
        const citation: Citation = { index: this.citations.size + 1, url, title: title || url, ...(quote ? { quote } : {}) };
        this.citations.set(url, citation);
        this.emit({ kind: "citation", citation });
    }

    private placed(citation: Citation): Citation {
        const p = this.placements.get(citation.url);
        if (!p)
            return citation;
        const from = utf16At(this.text, p.base, "utf16");
        const part = from === undefined ? "" : this.text.slice(from);
        const start = from === undefined ? undefined : utf16At(part, p.start, p.unit);
        const end = from === undefined ? undefined : utf16At(part, p.end, p.unit);
        if (start === undefined || end === undefined || start >= end ||
            (p.text !== undefined && part.slice(start, end) !== p.text))
            return citation;
        return { ...citation, start: from! + start, end: from! + end };
    }

    setSearchSuggestions(html: string | undefined): void {
        if (html)
            this.searchSuggestions = html;
    }

    addSearch(action: WebAction): void {
        if (!action.query && !action.url && !action.urls?.length)
            return;
        if (!this.searches.some((a) => a.kind === action.kind && a.query === action.query && a.url === action.url))
            this.searches.push(action);
    }

    /** The pages the last search found, for providers that send them after the query. */
    addFound(urls: string[]): void {
        const last = this.searches.findLast((a) => a.kind === "search");
        if (last && urls.length)
            last.urls = [...(last.urls ?? []), ...urls.filter((u) => u && !last.urls?.includes(u))];
    }

    keepProviderState(wire: Wire, parts: unknown[]): void {
        this.providerState = { type: "providerState", wire, json: JSON.stringify(parts) };
    }

    finish(reason: StopReason): Finished {
        this.emit({ kind: "usage", usage: this.usage });
        const toolCalls = [...this.calls.entries()].sort(([a], [b]) => a - b).map(([, call]) => call);
        return {
            reason, text: this.text, toolCalls, usage: this.usage,
            ...(this.citations.size ? { citations: [...this.citations.values()].map((c) => this.placed(c)) } : {}),
            ...(this.searchSuggestions ? { searchSuggestions: this.searchSuggestions } : {}),
            ...(this.searches.length ? { searches: this.searches } : {}),
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
