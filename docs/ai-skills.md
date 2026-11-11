# Hikari AI skills

A **skill** is a folder of instructions for one kind of work, such as typesetting a sign or writing a karaoke template ([AI14 #442](https://github.com/altqx/hikari/issues/442)). Hikari AI sees each skill's name and description in every thread, reads a skill's instructions when the work matches it, and reads the skill's other files only when it needs them. Skills follow the [Agent Skills format](https://agentskills.io/specification), so a skill written for another agent works here, and one written here works elsewhere.

Skills run nothing. A skill is text for the model: it can't call a tool, run a script or give a permission. Every action the model takes after reading one is checked like any other ([AI agent](ai-agent.md#modes)). With AI off nothing reads the skills.

## The format

```
typesetting/
  SKILL.md
  references/
    tags.md
```

`SKILL.md` starts with YAML frontmatter, then the instructions in Markdown:

```markdown
---
name: typesetting
description: Typesetting signs in ASS subtitles. Use it to place, size, rotate, colour or clip text over on-screen signs …
---

# Typesetting

1. **Find the sign.** …
```

| Field | Rule |
| --- | --- |
| `name` | 1–64 of `a`–`z` and `0`–`9`, in words joined by single hyphens. It must be the folder's name; without it, the folder's name is used. |
| `description` | Required. What the skill is for and when to use it, in at most 1,024 characters; longer ones are cut. The model picks skills by their descriptions alone, so name the work and the words a user would say. |
| others | `license`, `compatibility`, `metadata` and `allowed-tools` may be there and are ignored. A skill never widens what the model may do. |

The frontmatter is read as far as skills use it: one `key: value` per line, values plain, in single or double quotes, or as a `>` or `|` block. A file without frontmatter, a `name` other than the folder's, or no description leaves the skill out.

**The body** tells the model how to do the work: steps, rules and the tools to use, named as the [tools](ai-tools.md) are (`video_frame`, `lines_find`). Keep it under 500 lines, and put reference material in other files that the body links to by their path in the folder, such as `[tag reference](references/tags.md)`. The model reads those with `read_skill` when it needs them.

**Other files** may be anything the model should read: references, examples, templates. They are read as UTF-8 text, 256 KiB each at most. Scripts in a skill are text the model may read; nothing runs them.

## Where skills come from

| Origin | Folder | Ships |
| --- | --- | --- |
| **Built in** | `src/ai-sidecar/skills/` | In the sidecar bundle, so they update with it ([ADR 0031](adr/0031-the-ai-sidecar-updates-on-its-own.md)). |
| **Yours** | `ai/skills/` in the [user folder](../CONTEXT.md) | Each subfolder with a `SKILL.md`. |

A skill of yours with a built-in skill's name replaces it, so a built-in skill can be changed by copying it into your folder. The model is told which skills are yours. Its instructions say that your skills are guidance for the work and give no permission, and `read_skill` repeats that above each of them, since a skill may come from anywhere.

Your folder is read when a thread starts its first turn, so a new or changed skill shows in the next thread. At most 100 skills are read, and at most 100 files in each, 4 folders deep. Hidden files and folders (starting with `.`) and symbolic links are skipped, so a skill reaches no file outside its folder.

### The built-in skills

| Skill | For | Extended by |
| --- | --- | --- |
| `typesetting` | Placing signs with override tags and checking them on the frame; a tag reference. | AI9 [#437](https://github.com/altqx/hikari/issues/437) |
| `translation` | Translating Lines: meaning, consistency, tags, reading speed, Translation mode. | AI10 [#438](https://github.com/altqx/hikari/issues/438) |
| `karaoke-templater` | Writing and fixing karaoke templates; the template language. | AI11 [#439](https://github.com/altqx/hikari/issues/439) |

To add one, put its folder under `src/ai-sidecar/skills/` and its files in `kBundledSkills` (`src/ai-sidecar/src/skills/bundled.ts`). A test fails when a file under `skills/` isn't bundled, when a skill breaks the format, or when a link in a `SKILL.md` names no file of the skill. The built-in catalogue must stay within 600 tokens.

## Progressive disclosure

1. **The catalogue.** On a thread's first turn the sidecar adds a `## Skills` section to the system prompt: a line per skill with its name, `(yours)` for yours, and its description, sorted by name. Past 20,000 characters, the rest of the skills are named without their descriptions. The section stays the same for the whole thread, so the prompt cache keeps it.
2. **`read_skill {name}`** gives the skill's `SKILL.md` without its frontmatter, followed by the names of its other files.
3. **`read_skill {name, file}`** gives one of those files.

`read_skill` is always offered, like `update_plan`, and asks no permission: it only reads. Errors come back as text for the model: an unknown skill lists the skills, and an unknown file lists the skill's files.

External agents over [MCP](ai-mcp.md) don't get `read_skill` or the catalogue; they have their own skills.

## The Hikari API

The sidecar reads no files itself ([ADR 0026](adr/0026-hikari-ai-runs-in-a-typescript-sidecar-on-bun.md)), so your skills come through two [Hikari API](api.md) operations. Only a caller with the skills grant may call them: Hikari AI's threads and its tool calls have it; plugins and MCP clients don't.

| Operation | Gives |
| --- | --- |
| `skills.list` | `{skills: [{skill, files}]}`: each skill folder's name and its files, `/`-separated and sorted, `SKILL.md` among them. |
| `skills.read {skill, file}` | `{skill, file, size, data}`: one listed file as UTF-8 text. |

A file that isn't listed gives `not_found`, one over 256 KiB `refused` with `too_large`, and one that isn't UTF-8 `refused` with `not_text`. The host port is `SkillHost` (`hikari/api/host.h`); `FolderSkills` implements it over a folder, and the application gives it `<user folder>/ai/skills`.

## Where it lives

| Path | Holds |
| --- | --- |
| `src/api/skill_operations.cpp` | `skills.list` and `skills.read`. |
| `src/api/folder_skills.cpp` | `FolderSkills`: the folder walk and its limits. |
| `src/ai-sidecar/src/skills/skill.ts` | The frontmatter reader and the format's rules. |
| `src/ai-sidecar/src/skills/skills.ts` | `Skills`: the catalogue and `read_skill` for one thread. |
| `src/ai-sidecar/src/skills/bundled.ts` | The built-in skills, compiled into the bundle. |
| `src/ai-sidecar/skills/` | The built-in skills' files. |

## Tests

- `hikari_api_tests` (`tests/api/skill_tests.cpp`, `scripts/skills.jsonl`): listing and reading the fixture skills, the grant, names, hidden files, symbolic links, paths outside the folder, the depth, file and skill limits, and the size limit.
- `hikari_ai_sidecar_bun_tests` (`src/ai-sidecar/test/skills`): the frontmatter, the built-in skills against the format, the catalogue with your skills, replaced and broken skills, HA1 refusing, one lookup per thread, the catalogue's room, and `read_skill`'s answers and errors. `test/agent` checks the system prompt carries the catalogue, and `test/tools/budget.test.ts` counts `read_skill` in the up-front tools and the catalogue's tokens.
