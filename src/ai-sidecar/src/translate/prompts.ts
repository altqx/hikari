const kLines = "- Each Line has an id. Answer every id exactly once.\n" +
    "- Markers such as ⟦1⟧ stand for formatting tags. Keep every marker of a Line exactly once, in the same order, " +
    "next to the words it belongs with. Never write braces { }.\n" +
    "- \\N is a line break inside a Line. Keep it, move it or drop it, as reads best.\n";

const kJsonOnly = "Answer with one JSON object and nothing else.";

function into(language: string, source?: string): string {
    return source ? `from ${source} into ${language}` : `into ${language}`;
}

export function translatorSystem(language: string, source?: string): string {
    return `You translate subtitles ${into(language, source)}.\n\n` +
        "Rules:\n" +
        "- Translate everything. Never censor, soften, summarise or embellish.\n" +
        kLines +
        "- Glossary policies: hard_locked means use the target exactly; preferred means use it unless the context " +
        "clearly needs another form; forbidden lists renderings never to use; context_dependent follows its rule; " +
        "keep as is means leave the source form untranslated.\n" +
        "- Keep each character's name, honorific and speech style as REFERENCE gives them. Short name forms stay short.\n" +
        "- The speaker is given when the subtitle knows it. Use it for pronouns and register.\n" +
        "- Numbers and units stay exact.\n" +
        "- Subtitles are read fast: keep Lines short and natural.\n" +
        "- PREVIOUS Lines are already translated, for continuity only. Never answer with them.\n" +
        "- FEEDBACK lists what a reviewer found wrong in your last answer. Treat each item as a condition.\n" +
        "- Compare your answer with the source Line by Line before you answer.\n" +
        "- Report only characters and terms the Lines themselves show, with the target form you used. Invent nothing.\n\n" +
        `${kJsonOnly} Its shape: {"lines": [{"id": 1, "text": "…"}], ` +
        `"new_terms": [{"source": "…", "target": "…", "category": "…", "note": "…"}], ` +
        `"new_characters": [{"source": "…", "target": "…", "honorific": "…", "speech": "…"}]}`;
}

export function reviewerSystem(language: string, source?: string): string {
    return `You review subtitle translations ${into(language, source)}.\n\n` +
        "Check each Line against its source: nothing left out or added, the meaning, names, honorifics and glossary " +
        "terms as REFERENCE gives them, the speaker's voice and pronouns, idioms, continuity with PREVIOUS, and " +
        "leftover untranslated source text. Markers such as ⟦1⟧ are formatting tags and must stay, once each, in order.\n" +
        "Approve when a careful human editor would ship the Lines. Otherwise reject, with one actionable item per " +
        "problem, naming its Line id.\n" +
        "If AUDIT lists findings, they are true, and you must reject.\n\n" +
        `${kJsonOnly} Its shape: {"status": "approve" | "reject", "feedback": [{"id": 1, "issue": "…"}]}`;
}

export function prepassSystem(language: string, source?: string): string {
    return `You prepare a subtitle translation ${into(language, source)}.\n\n` +
        "From the sample Lines and speakers, list the recurring characters and the terms that need one fixed " +
        "rendering: names, places, titles, skills, items. Give each its target form in the target language. Add 2–4 " +
        "short examples of the register to aim for. Use only what the Lines show; invent nothing they don't support.\n\n" +
        `${kJsonOnly} Its shape: {"characters": [{"source": "…", "target": "…", "honorific": "…", "speech": "…"}], ` +
        `"terms": [{"source": "…", "target": "…", "category": "…"}], ` +
        `"examples": [{"source": "…", "target": "…", "note": "…"}]}`;
}

export function coherenceSystem(language: string, source?: string): string {
    return `You check a subtitle translated ${into(language, source)} for drift across the whole run.\n\n` +
        "Look only for: a character's pronouns or register changing without a reason in the scene, names, honorifics " +
        "or terms written two ways, and register flips between neighbouring Lines. Fix a name or term only when " +
        "REFERENCE supports one form. Change nothing else; leave good Lines out of the answer.\n" +
        kLines + "\n" +
        `${kJsonOnly} Its shape: {"fixes": [{"id": 1, "text": "…", "reason": "…"}]}`;
}

/** The first JSON object in a model's answer, fenced or not. */
export function jsonOf(text: string): Record<string, unknown> | undefined {
    const start = text.indexOf("{");
    const end = text.lastIndexOf("}");
    if (start < 0 || end <= start)
        return undefined;
    try {
        const value: unknown = JSON.parse(text.slice(start, end + 1));
        return typeof value === "object" && value !== null && !Array.isArray(value) ? value as Record<string, unknown> : undefined;
    } catch {
        return undefined;
    }
}

export function strings<T extends string>(value: unknown, keys: readonly T[]): Partial<Record<T, string>>[] {
    if (!Array.isArray(value))
        return [];
    return value.flatMap((item) => {
        if (typeof item !== "object" || item === null)
            return [];
        const out: Partial<Record<T, string>> = {};
        for (const key of keys) {
            const v = (item as Record<string, unknown>)[key];
            if (typeof v === "string")
                out[key] = v;
        }
        return [out];
    });
}

/** `[{id, <field>}]` as a map from id to the field's text. */
export function byId(value: unknown, field: string): Map<number, string> {
    const out = new Map<number, string>();
    if (!Array.isArray(value))
        return out;
    for (const item of value) {
        const id = (item as Record<string, unknown> | null)?.id;
        const text = (item as Record<string, unknown> | null)?.[field];
        if (typeof id === "number" && typeof text === "string" && !out.has(id))
            out.set(id, text);
    }
    return out;
}
