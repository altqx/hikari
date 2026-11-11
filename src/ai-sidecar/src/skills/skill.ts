/** The Agent Skills limits (https://agentskills.io/specification). */
export const kMaxNameLength = 64;
export const kMaxDescriptionLength = 1024;

export const kSkillFile = "SKILL.md";

export function isSkillName(name: string): boolean {
    return name.length <= kMaxNameLength && /^[a-z0-9]+(-[a-z0-9]+)*$/.test(name);
}

export interface SkillText {
    /** The frontmatter's top-level scalar keys; nested maps such as `metadata` are left out. */
    fields: Record<string, string>;
    body: string;
}

function unquoted(value: string): string {
    if (value.length >= 2 && value.startsWith("\"") && value.endsWith("\"")) {
        try {
            return JSON.parse(value) as string;
        } catch {
            return value.slice(1, -1);
        }
    }
    if (value.length >= 2 && value.startsWith("'") && value.endsWith("'"))
        return value.slice(1, -1).replaceAll("''", "'");
    return value;
}

/** `lines` of a `>` block folded into one paragraph per run of lines, or of a `|` block kept as they are. */
function block(style: string, lines: string[]): string {
    const kept = lines.map((line) => line.trim()).join("\n").trim();
    return style.startsWith("|") ? kept : kept.split(/\n{2,}/).map((paragraph) => paragraph.replaceAll("\n", " ")).join("\n");
}

/**
 * A SKILL.md split into its YAML frontmatter and body. The frontmatter is read as far as skills use it: `key: value`
 * lines with plain, quoted or block (`>`, `|`) values. Undefined when the file has no frontmatter.
 */
export function parseSkill(text: string): SkillText | undefined {
    const lines = text.replace(/^﻿/, "").split(/\r?\n/);
    if (lines[0]?.trim() !== "---")
        return undefined;
    const end = lines.findIndex((line, i) => i > 0 && line.trim() === "---");
    if (end < 0)
        return undefined;
    const fields: Record<string, string> = {};
    for (let i = 1; i < end; ++i) {
        const match = /^([A-Za-z][\w-]*):(?:\s+(.*))?$/.exec(lines[i]!);
        if (!match)
            continue;
        const value = (match[2] ?? "").trim();
        if (/^[>|][+-]?$/.test(value)) {
            const continued: string[] = [];
            while (i + 1 < end && (/^\s/.test(lines[i + 1]!) || lines[i + 1]!.trim() === ""))
                continued.push(lines[++i]!);
            fields[match[1]!] = block(value, continued);
        } else if (value !== "") {
            fields[match[1]!] = unquoted(value);
        }
    }
    return { fields, body: lines.slice(end + 1).join("\n").trim() };
}

export interface SkillEntry {
    name: string;
    description: string;
}

/** The catalogue entry of the skill in `folder`, or why it has none. */
export function skillEntry(folder: string, text: string): SkillEntry | string {
    const parsed = parseSkill(text);
    if (!parsed)
        return "its SKILL.md has no frontmatter";
    const name = parsed.fields.name ?? folder;
    if (name !== folder)
        return `its name "${name}" isn't its folder's`;
    const description = parsed.fields.description?.replace(/\s+/g, " ").trim();
    if (!description)
        return "it has no description";
    return { name, description: description.slice(0, kMaxDescriptionLength) };
}
