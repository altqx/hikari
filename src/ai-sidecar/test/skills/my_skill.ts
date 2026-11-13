import type { OperationName } from "../../src/tools/api.gen";

/** One skill in the user's folder, as HA1's skills.* answer it. */
export const kMySkill = { name: "my-notes", description: "House style for signs.", body: "Keep signs short." };

export const mySkillAnswers: Partial<Record<OperationName, unknown>> = {
    "skills.list": { skills: [{ skill: kMySkill.name, origin: "yours", files: ["SKILL.md"] }] },
    "skills.read": { data: `---\nname: ${kMySkill.name}\ndescription: ${kMySkill.description}\n---\n\n${kMySkill.body}\n` },
};
