/**
 * Honya's project files: a Markdown view over a `<!-- honya:data … honya:data -->` JSON block, which is the truth.
 * Reads are tolerant like Honya's: a missing block or bad JSON reads as empty. Writes keep every field this code
 * doesn't know, so a file Honya wrote survives a round trip.
 */

const kOpen = "<!-- honya:data";
const kClose = "honya:data -->";

export const kProjectFiles = ["PROJECT.md", "GLOSSARY.md", "CHARACTERS.md", "STYLE.md"] as const;
export type ProjectFileName = typeof kProjectFiles[number];

export type TermPolicy = "hard_locked" | "preferred" | "forbidden" | "context_dependent";

/** Honya's GlossaryTerm; `jp_term` is the source form whatever the source language. */
export interface GlossaryTerm {
    jp_term: string;
    translated_term: string;
    romaji?: string | null;
    category?: string | null;
    gloss?: string | null;
    policy?: TermPolicy | null;
    forbidden_translations?: string[];
    context_rule?: string | null;
    protected?: boolean | null;
    do_not_translate?: boolean | null;
    [other: string]: unknown;
}

export interface Character {
    id: string;
    jp_name: string;
    translated_name: string;
    honorific?: string | null;
    speech_style?: string | null;
    gender?: string | null;
    aliases?: string[];
    also_called?: { jp: string; translated_name?: string; by?: string | null }[];
    notes?: string | null;
    [other: string]: unknown;
}

export interface StyleExample {
    jp: string;
    translated_text: string;
    note?: string | null;
}

export function dataOf(text: string | undefined): Record<string, unknown> {
    if (!text)
        return {};
    const open = text.indexOf(kOpen);
    if (open < 0)
        return {};
    const rest = text.slice(open + kOpen.length);
    const close = rest.indexOf(kClose);
    if (close < 0)
        return {};
    try {
        const value: unknown = JSON.parse(rest.slice(0, close).trim());
        return typeof value === "object" && value !== null && !Array.isArray(value) ? value as Record<string, unknown> : {};
    } catch {
        return {};
    }
}

export function bodyOf(text: string | undefined): string {
    if (!text)
        return "";
    const open = text.indexOf(kOpen);
    return (open < 0 ? text : text.slice(0, open)).trim();
}

export function withData(body: string, data: Record<string, unknown>): string {
    return `${body.trimEnd()}\n\n${kOpen}\n${JSON.stringify(data, null, 2)}\n${kClose}\n`;
}

/** Honya's rule: no policy means hard_locked when the term is protected, else preferred. */
export function policyOf(term: GlossaryTerm): TermPolicy {
    return term.policy ?? (term.protected ? "hard_locked" : "preferred");
}

/** A controlled term an automatic write never changes. */
export function isControlled(term: GlossaryTerm): boolean {
    return term.protected === true || (term.policy !== undefined && term.policy !== null && term.policy !== "preferred");
}

function isTerm(value: unknown): value is GlossaryTerm {
    const t = value as Partial<GlossaryTerm> | null;
    return typeof t === "object" && t !== null && typeof t.jp_term === "string" && typeof t.translated_term === "string";
}

function isCharacter(value: unknown): value is Character {
    const c = value as Partial<Character> | null;
    return typeof c === "object" && c !== null && typeof c.jp_name === "string" && typeof c.translated_name === "string";
}

function listOf<T>(data: Record<string, unknown>, key: string, keep: (value: unknown) => value is T): T[] {
    const list = data[key];
    return Array.isArray(list) ? list.filter(keep) : [];
}

export const normalized = (text: string) => text.normalize("NFKC").trim().toLowerCase();

function cell(text: string | null | undefined): string {
    const value = (text ?? "").replaceAll("|", "\\|").replaceAll("\n", " ").trim();
    return value.length > 0 ? value : "—";
}

function glossaryTable(terms: GlossaryTerm[]): string {
    const head = "# Glossary\n\n";
    if (terms.length === 0)
        return `${head}_No terms yet._\n`;
    const rows = terms.map((t) => `| ${cell(t.jp_term)} | ${cell(t.translated_term)} | ${cell(t.category)} | ${policyOf(t)} | ` +
        `${t.do_not_translate ? "✓" : "—"} | ${cell((t.forbidden_translations ?? []).join(", "))} | ` +
        `${cell([t.context_rule, t.gloss].filter((v) => v && v.trim()).join(" · "))} |`);
    return `${head}| Source | Target | Category | Policy | Keep as is | Forbidden | Notes |\n` +
        `|---|---|---|---|---|---|---|\n${rows.join("\n")}\n`;
}

function charactersTable(characters: Character[]): string {
    const head = "# Characters\n\n";
    if (characters.length === 0)
        return `${head}_No characters yet._\n`;
    const rows = characters.map((c) => `| ${cell(c.id)} | ${cell(c.jp_name)} | ${cell(c.translated_name)} | ` +
        `${cell(c.honorific)} | ${cell(c.speech_style)} | ${cell(c.notes)} |`);
    return `${head}| ID | Source | Target | Honorific | Speech | Notes |\n|---|---|---|---|---|---|\n${rows.join("\n")}\n`;
}

export interface NewTerm {
    source: string;
    target: string;
    category?: string;
    note?: string;
}

export interface NewCharacter {
    source: string;
    target: string;
    honorific?: string;
    speech?: string;
    note?: string;
}

/** The project's reference material, and the additions a run makes to it. */
export class ProjectReference {
    readonly terms: GlossaryTerm[];
    readonly characters: Character[];
    readonly styleNotes: string;
    readonly synopsis: string;
    readonly examples: StyleExample[];
    private readonly glossaryData: Record<string, unknown>;
    private readonly characterData: Record<string, unknown>;
    private addedTerms = 0;
    private addedCharacters = 0;

    constructor(readonly files: Partial<Record<ProjectFileName, string>> = {}) {
        this.glossaryData = dataOf(files["GLOSSARY.md"]);
        this.characterData = dataOf(files["CHARACTERS.md"]);
        this.terms = listOf(this.glossaryData, "terms", isTerm);
        this.characters = listOf(this.characterData, "characters", isCharacter);
        this.styleNotes = bodyOf(files["STYLE.md"]);
        const project = dataOf(files["PROJECT.md"]);
        this.synopsis = typeof project.synopsis === "string" ? project.synopsis : "";
        const style = dataOf(files["STYLE.md"]);
        this.examples = listOf(style, "style_examples", (v): v is StyleExample =>
            typeof (v as StyleExample)?.jp === "string" && typeof (v as StyleExample)?.translated_text === "string");
    }

    get empty(): boolean {
        return this.terms.length === 0 && this.characters.length === 0;
    }

    term(source: string): GlossaryTerm | undefined {
        const key = normalized(source);
        return this.terms.find((t) => normalized(t.jp_term) === key);
    }

    character(source: string): Character | undefined {
        const key = normalized(source);
        return this.characters.find((c) => normalized(c.jp_name) === key || (c.aliases ?? []).some((a) => normalized(a) === key));
    }

    /** Adds only what isn't there yet; an existing entry, controlled or not, is never rewritten. */
    addTerm(term: NewTerm): boolean {
        if (!term.source.trim() || !term.target.trim() || this.term(term.source))
            return false;
        this.terms.push({ jp_term: term.source.trim(), translated_term: term.target.trim(), category: term.category ?? null,
            gloss: term.note ?? null, policy: "preferred" });
        ++this.addedTerms;
        return true;
    }

    addCharacter(character: NewCharacter): boolean {
        if (!character.source.trim() || !character.target.trim() || this.character(character.source))
            return false;
        let id = normalized(character.target).replace(/[^\p{L}\p{N}]+/gu, "-").replace(/^-|-$/g, "") || "character";
        for (let n = 2; this.characters.some((c) => c.id === id); ++n)
            id = `${id.replace(/-\d+$/, "")}-${n}`;
        this.characters.push({ id, jp_name: character.source.trim(), translated_name: character.target.trim(),
            honorific: character.honorific ?? null, speech_style: character.speech ?? null, notes: character.note ?? null });
        ++this.addedCharacters;
        return true;
    }

    /** The files the run's additions change, in Honya's format. */
    changedFiles(): { name: ProjectFileName; data: string }[] {
        const changed: { name: ProjectFileName; data: string }[] = [];
        if (this.addedTerms > 0) {
            const terms = [...this.terms].sort((a, b) => (a.category ?? "").localeCompare(b.category ?? "") ||
                a.jp_term.localeCompare(b.jp_term));
            changed.push({ name: "GLOSSARY.md", data: withData(glossaryTable(terms), { ...this.glossaryData, terms }) });
        }
        if (this.addedCharacters > 0) {
            const characters = [...this.characters].sort((a, b) => a.id.localeCompare(b.id));
            changed.push({ name: "CHARACTERS.md",
                data: withData(charactersTable(characters), { ...this.characterData, characters }) });
        }
        return changed;
    }
}
