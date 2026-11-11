import karaokeLanguage from "../../skills/karaoke-templater/references/language.md" with { type: "text" };
import karaoke from "../../skills/karaoke-templater/SKILL.md" with { type: "text" };
import translation from "../../skills/translation/SKILL.md" with { type: "text" };
import typesettingTags from "../../skills/typesetting/references/tags.md" with { type: "text" };
import typesetting from "../../skills/typesetting/SKILL.md" with { type: "text" };

export interface BundledSkill {
    folder: string;
    /** By path in the folder, SKILL.md among them. */
    files: Readonly<Record<string, string>>;
}

/** Every file under skills/; a test fails when one is left out. */
export const kBundledSkills: readonly BundledSkill[] = [
    { folder: "karaoke-templater", files: { "SKILL.md": karaoke, "references/language.md": karaokeLanguage } },
    { folder: "translation", files: { "SKILL.md": translation } },
    { folder: "typesetting", files: { "SKILL.md": typesetting, "references/tags.md": typesettingTags } },
];
