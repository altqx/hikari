import type { SessionModel } from "../protocol";
import type { Finished, Request, Usage } from "../providers/types";
import { auditLine, type Finding } from "./audit";
import { normalized, ProjectReference, type GlossaryTerm, type Character, type ProjectFileName } from "./honya";
import { byId, coherenceSystem, jsonOf, prepassSystem, reviewerSystem, strings, translatorSystem } from "./prompts";

/** One Line as Qt sends it: its text with the override blocks as markers. */
export interface TranslateLine {
    key: number;
    text: string;
    speaker?: string;
    /** Milliseconds. */
    start: number;
    end: number;
}

export type PipelineRole = "translator" | "reviewer";

export interface TranslateOptions {
    language: string;
    models: Record<PipelineRole, SessionModel>;
    lines: TranslateLine[];
    /** Already translated Lines before the run, for continuity. */
    before?: string[];
    /** Reviewer rejections retried before a chunk's best attempt is kept as needing review. */
    retries?: number;
    caching?: boolean;
    cacheKey?: string;
    chunkTokens?: number;
}

export interface TranslatedLine {
    key: number;
    text: string;
    review: "approved" | "needsReview";
    /** What the last review or audit found, for a Line that needs review. */
    issues: string[];
}

export type Stage = "prepass" | "translate" | "coherence";

export interface PipelineHooks {
    complete(model: SessionModel, request: Request, signal: AbortSignal): Promise<Finished>;
    usage?(role: PipelineRole, usage: Usage): void;
    progress?(stage: Stage, done: number, total: number): void;
}

export interface PipelineResult {
    lines: TranslatedLine[];
    /** The project files the run's approved discoveries change. */
    files: { name: ProjectFileName; data: string }[];
}

export const kChunkTokens = 1000;
export const kChunkCapTokens = 1200;
const kSceneGapMs = 5000;
const kPrevious = 5;
const kMaxTerms = 80;
const kMaxCharacters = 40;
const kCoherenceTokens = 8000;
const kPrepassSampleTokens = 3000;

/** A rough count that errs high for scripts with no spaces, so chunks stay under the cap. */
export function tokensOf(text: string): number {
    return Math.ceil(text.length / 2) + 4;
}

/** Lines in order, packed to the budget, never splitting a Line; a long gap past half the budget starts a new chunk. */
export function chunked(lines: TranslateLine[], budget = kChunkTokens): TranslateLine[][] {
    const chunks: TranslateLine[][] = [];
    let current: TranslateLine[] = [];
    let tokens = 0;
    for (const line of lines) {
        const cost = tokensOf(line.text);
        const previous = current.at(-1);
        const scene = previous !== undefined && line.start - previous.end >= kSceneGapMs && tokens >= budget / 2;
        if (current.length > 0 && (tokens + cost > budget || scene)) {
            chunks.push(current);
            current = [];
            tokens = 0;
        }
        current.push(line);
        tokens += cost;
    }
    if (current.length > 0)
        chunks.push(current);
    return chunks;
}

function mentions(text: string, name: string | undefined | null): boolean {
    const n = name ? normalized(name) : "";
    return n.length > 0 && normalized(text).includes(n);
}

/** Only the terms and characters the chunk mentions, as Honya scopes each chunk's reference. */
function scoped(reference: ProjectReference, chunk: TranslateLine[]) {
    const text = chunk.map((l) => l.text).join("\n");
    const speakers = chunk.map((l) => l.speaker ?? "").join("\n");
    const terms = reference.terms.filter((t) => mentions(text, t.jp_term)).slice(0, kMaxTerms);
    const characters = reference.characters.filter((c) => [c.jp_name, c.translated_name, ...(c.aliases ?? [])]
        .some((name) => mentions(text, name) || mentions(speakers, name))).slice(0, kMaxCharacters);
    return {
        glossary: terms.map((t: GlossaryTerm) => ({ source: t.jp_term, target: t.translated_term, policy: t.policy ??
            (t.protected ? "hard_locked" : "preferred"), ...(t.do_not_translate ? { keep_as_is: true } : {}),
            ...(t.forbidden_translations?.length ? { forbidden: t.forbidden_translations } : {}),
            ...(t.context_rule ? { rule: t.context_rule } : {}) })),
        characters: characters.map((c: Character) => ({ source: c.jp_name, target: c.translated_name,
            ...(c.honorific ? { honorific: c.honorific } : {}), ...(c.speech_style ? { speech: c.speech_style } : {}),
            ...(c.also_called?.length ? { also_called: c.also_called } : {}) })),
    };
}

function sourceLines(chunk: TranslateLine[]) {
    return chunk.map((l) => ({ id: l.key, ...(l.speaker ? { speaker: l.speaker } : {}), text: l.text }));
}

interface Attempt {
    texts: Map<number, string>;
    findings: Finding[];
    feedback: Finding[];
    approved: boolean;
    newTerms: ReturnType<typeof strings<"source" | "target" | "category" | "note">>;
    newCharacters: ReturnType<typeof strings<"source" | "target" | "honorific" | "speech" | "note">>;
}

export class TranslationPipeline {
    private readonly translated = new Map<number, TranslatedLine>();
    private readonly previous: string[];
    private done = 0;

    constructor(private readonly options: TranslateOptions, readonly reference: ProjectReference,
        private readonly hooks: PipelineHooks, private readonly signal: AbortSignal) {
        this.previous = [...(options.before ?? [])].slice(-kPrevious);
    }

    async run(): Promise<PipelineResult> {
        const lines = this.options.lines;
        const chunks = chunked(lines, Math.min(this.options.chunkTokens ?? kChunkTokens, kChunkCapTokens));
        if (this.reference.empty && chunks.length > 1)
            await this.prepass();
        for (const chunk of chunks) {
            await this.chunk(chunk);
            this.done += chunk.length;
            this.hooks.progress?.("translate", this.done, lines.length);
        }
        if (chunks.length > 1)
            await this.coherence();
        return {
            lines: lines.flatMap((l) => this.translated.get(l.key) ?? []),
            files: this.reference.changedFiles(),
        };
    }

    private async ask(role: PipelineRole, system: string, payload: Record<string, unknown>): Promise<Record<string, unknown> | undefined> {
        this.signal.throwIfAborted();
        const request: Request = {
            model: this.options.models[role].model,
            system,
            messages: [{ author: "user", parts: [{ type: "text", text: JSON.stringify(payload, null, 1) }] }],
            ...(this.options.caching === undefined ? {} : { caching: this.options.caching }),
            ...(this.options.cacheKey ? { cacheKey: `${this.options.cacheKey}:${role}` } : {}),
        };
        const finished = await this.hooks.complete(this.options.models[role], request, this.signal);
        this.hooks.usage?.(role, finished.usage);
        return jsonOf(finished.text);
    }

    private async prepass(): Promise<void> {
        this.hooks.progress?.("prepass", 0, this.options.lines.length);
        const lines = this.options.lines;
        const step = Math.max(1, Math.ceil(lines.reduce((n, l) => n + tokensOf(l.text), 0) / kPrepassSampleTokens));
        const sample = lines.filter((_, i) => i % step === 0);
        const speakers = [...new Set(lines.flatMap((l) => (l.speaker ? [l.speaker] : [])))];
        const answer = await this.ask("translator", prepassSystem(this.options.language),
            { SYNOPSIS: this.reference.synopsis || undefined, SPEAKERS: speakers, LINES: sourceLines(sample) });
        if (!answer)
            return;
        for (const c of strings(answer.characters, ["source", "target", "honorific", "speech"] as const))
            if (c.source && c.target)
                this.reference.addCharacter({ source: c.source, target: c.target, honorific: c.honorific, speech: c.speech });
        for (const t of strings(answer.terms, ["source", "target", "category"] as const))
            if (t.source && t.target && sample.some((l) => mentions(l.text, t.source)))
                this.reference.addTerm({ source: t.source, target: t.target, category: t.category });
        this.examples = strings(answer.examples, ["source", "target", "note"] as const)
            .filter((e) => e.source && e.target).slice(0, 4);
    }

    private examples: Partial<Record<"source" | "target" | "note", string>>[] = [];

    private styleExamples() {
        const kept = this.reference.examples.map((e) => ({ source: e.jp, target: e.translated_text }));
        return [...kept, ...this.examples].slice(0, 4);
    }

    private async attempt(chunk: TranslateLine[], feedback: Finding[]): Promise<Attempt> {
        const reference = scoped(this.reference, chunk);
        const answer = await this.ask("translator", translatorSystem(this.options.language), {
            REFERENCE: { ...reference, style: this.reference.styleNotes || undefined, examples: this.styleExamples() },
            PREVIOUS: this.previous,
            ...(feedback.length ? { FEEDBACK: feedback.map((f) => ({ id: f.key, issue: f.issue })) } : {}),
            LINES: sourceLines(chunk),
        });
        const texts = byId(answer?.lines, "text");
        const findings = answer
            ? chunk.flatMap((l) => auditLine(l.key, l.text, texts.get(l.key), this.reference))
            : chunk.map((l) => ({ key: l.key, issue: "The answer was not the JSON object asked for." }));
        const result: Attempt = {
            texts, findings, feedback: findings, approved: false,
            newTerms: strings(answer?.new_terms, ["source", "target", "category", "note"] as const),
            newCharacters: strings(answer?.new_characters, ["source", "target", "honorific", "speech", "note"] as const),
        };
        if (findings.length > 0)
            return result;
        const review = await this.ask("reviewer", reviewerSystem(this.options.language), {
            REFERENCE: reference,
            PREVIOUS: this.previous,
            AUDIT: [],
            LINES: chunk.map((l) => ({ id: l.key, ...(l.speaker ? { speaker: l.speaker } : {}), source: l.text,
                translation: texts.get(l.key) })),
        });
        if (review?.status === "approve") {
            result.approved = true;
            return result;
        }
        const items = Array.isArray(review?.feedback) ? review.feedback as Record<string, unknown>[] : [];
        const ids = new Set(chunk.map((l) => l.key));
        result.feedback = items.flatMap((item) => {
            const issue = typeof item?.issue === "string" ? item.issue : "";
            if (!issue)
                return [];
            return typeof item.id === "number" && ids.has(item.id)
                ? [{ key: item.id, issue }]
                : chunk.map((l) => ({ key: l.key, issue }));
        });
        if (result.feedback.length === 0)
            result.feedback = chunk.map((l) => ({ key: l.key, issue: "The reviewer rejected the Lines without saying why." }));
        return result;
    }

    private async chunk(chunk: TranslateLine[]): Promise<void> {
        const tries = 1 + (this.options.retries ?? 3);
        let best: Attempt | undefined;
        let feedback: Finding[] = [];
        for (let n = 0; n < tries; ++n) {
            const attempt = await this.attempt(chunk, feedback);
            if (!best || attempt.findings.length <= best.findings.length)
                best = attempt;
            if (attempt.approved) {
                best = attempt;
                break;
            }
            feedback = attempt.feedback;
        }
        const kept = best!;
        if (kept.approved) {
            for (const t of kept.newTerms)
                if (t.source && t.target && chunk.some((l) => mentions(l.text, t.source)))
                    this.reference.addTerm({ source: t.source, target: t.target, category: t.category, note: t.note });
            for (const c of kept.newCharacters)
                if (c.source && c.target)
                    this.reference.addCharacter({ source: c.source, target: c.target, honorific: c.honorific,
                        speech: c.speech, note: c.note });
        }
        for (const line of chunk) {
            const text = kept.texts.get(line.key);
            if (text === undefined)
                continue;
            const issues = kept.approved ? [] : kept.feedback.filter((f) => f.key === line.key).map((f) => f.issue);
            this.translated.set(line.key, { key: line.key, text, review: kept.approved ? "approved" : "needsReview", issues });
            this.previous.push(text);
        }
        this.previous.splice(0, Math.max(0, this.previous.length - kPrevious));
    }

    private async coherence(): Promise<void> {
        const lines = this.options.lines.filter((l) => this.translated.has(l.key));
        const windows: TranslateLine[][] = [];
        let current: TranslateLine[] = [];
        let tokens = 0;
        for (const line of lines) {
            const cost = tokensOf(line.text) + tokensOf(this.translated.get(line.key)!.text);
            if (current.length > 0 && tokens + cost > kCoherenceTokens) {
                windows.push(current);
                current = [];
                tokens = 0;
            }
            current.push(line);
            tokens += cost;
        }
        if (current.length > 0)
            windows.push(current);
        let checked = 0;
        for (const window of windows) {
            this.hooks.progress?.("coherence", checked, lines.length);
            const answer = await this.ask("reviewer", coherenceSystem(this.options.language), {
                REFERENCE: scoped(this.reference, window),
                LINES: window.map((l) => ({ id: l.key, ...(l.speaker ? { speaker: l.speaker } : {}), source: l.text,
                    translation: this.translated.get(l.key)!.text })),
            });
            for (const [key, text] of byId(answer?.fixes, "text")) {
                const line = window.find((l) => l.key === key);
                const current = this.translated.get(key);
                if (line && current && auditLine(key, line.text, text, this.reference).length === 0)
                    current.text = text;
            }
            checked += window.length;
        }
        this.hooks.progress?.("coherence", lines.length, lines.length);
    }
}
