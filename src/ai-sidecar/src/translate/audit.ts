import { normalized, policyOf, type ProjectReference } from "./honya";

/** Qt's tag markers: ⟦1⟧, ⟦2⟧… stand for the Line's override blocks, which never leave the app. */
const kMarker = /⟦(\d+)⟧/g;

export function markersOf(text: string): number[] {
    return [...text.matchAll(kMarker)].map((m) => Number(m[1]));
}

export interface Finding {
    key: number;
    issue: string;
}

/** Whether `needle` occurs in `text`, ignoring case and width; markers don't count as text. */
function contains(text: string, needle: string): boolean {
    const n = normalized(needle);
    return n.length > 0 && normalized(text.replace(kMarker, "")).includes(n);
}

/**
 * The deterministic gates between the translator and the reviewer: every marker once and in order, no stray brace,
 * and the glossary's controlled terms. A finding is always a reason to reject.
 */
export function auditLine(key: number, source: string, translation: string | undefined, reference: ProjectReference): Finding[] {
    if (translation === undefined)
        return [{ key, issue: "The Line is missing from the answer." }];
    const findings: Finding[] = [];
    const want = markersOf(source);
    const got = markersOf(translation);
    if (want.join(",") !== got.join(","))
        findings.push({ key, issue: `Keep the markers ${want.map((n) => `⟦${n}⟧`).join(" ") || "(none)"} exactly once each, ` +
            `in that order; the answer has ${got.map((n) => `⟦${n}⟧`).join(" ") || "none"}.` });
    if (/[{}]/.test(translation))
        findings.push({ key, issue: "Remove the braces { }: tags are written by the app, never by the translation." });
    if (translation.replace(kMarker, "").trim().length === 0 && source.replace(kMarker, "").trim().length > 0)
        findings.push({ key, issue: "The translation is empty." });
    for (const term of reference.terms) {
        if (!contains(source, term.jp_term))
            continue;
        const policy = policyOf(term);
        if (term.do_not_translate && !contains(translation, term.jp_term))
            findings.push({ key, issue: `Keep "${term.jp_term}" as it is, untranslated.` });
        else if (policy === "hard_locked" && !term.do_not_translate && !contains(translation, term.translated_term))
            findings.push({ key, issue: `"${term.jp_term}" is locked: write it as "${term.translated_term}".` });
        else if (policy === "forbidden" && contains(translation, term.translated_term))
            findings.push({ key, issue: `"${term.translated_term}" is forbidden for "${term.jp_term}".` });
        for (const forbidden of term.forbidden_translations ?? [])
            if (contains(translation, forbidden))
                findings.push({ key, issue: `"${forbidden}" is forbidden for "${term.jp_term}".` });
    }
    return findings;
}
