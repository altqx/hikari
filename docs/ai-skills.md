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
| **Project** | `.hikari/skills/` in the [Project](adr/0030-open-a-folder-as-a-project.md) folder | Shared with everyone who opens the Project ([AI14b #941](https://github.com/altqx/hikari/issues/941)). |

**Precedence:** Project, then yours, then built in. A skill hides any skill of the same name below it, as Project catalogues do (ADR 0030): a skill of yours replaces a built-in one, so a built-in skill can be changed by copying it into your folder, and a Project skill hides yours. A broken skill hides nothing.

**Which Project.** A thread keeps the Project of the window it was started in, saved with the thread, so its skills don't change when it is opened in another window. A thread started in a window without a Project sees no Project skills. Project skills are offered in an untrusted Project too: like its `AGENTS.md`, they are text and give no permission. A `.hikari` or `.hikari/skills` folder that is a symbolic link gives no Project skills.

The model is told which skills are the Project's and which are yours. Its instructions say that those skills are guidance for the work and give no permission, and `read_skill` repeats that above each of them, naming the folder, since a skill may come from anywhere.

The folders are read when a thread starts its first turn, so a new or changed skill shows in the next thread. In each folder at most 100 skills are read, and at most 100 files in each skill, 4 folders deep. Folders are walked in name order, and `SKILL.md` is always among the files kept, so which files are kept doesn't depend on the file system ([#969](https://github.com/altqx/hikari/issues/969)). Hidden files and folders (starting with `.`) and symbolic links are skipped, so a skill reaches no file outside its folder.

### In Settings

**Settings › AI › Skills** lists the skills a window's threads get ([#941](https://github.com/altqx/hikari/issues/941), owner Q349). It's on the Yours tab, and sets nothing.

- The groups are **This project** (in a Project window, `.hikari/skills`), **Mine** (`ai/skills`) and **Built in**. Each row has a Project, Mine or Built in chip.
- A skill another hides stays in the list, struck through: "Hidden by this project's" or "Replaced by yours".
- A folder that can't be offered stays with a warning and why: a folder name that isn't a skill name, no `SKILL.md`, a `SKILL.md` that isn't UTF-8 text or is over 256 KiB, no frontmatter, a `name` other than the folder's, or no description. A skill past the file limit says how many of its files are offered.
- **Open folder** makes `ai/skills` when it isn't there and opens it. **Open project folder**, in a Project window, does the same for `.hikari/skills`; only you write there, never the AI. **Read the folders again** refreshes the list, which is read when the page opens.

HikariSub checks the skills itself (`src/api/skill_check.cpp`), with the sidecar's rules; `tests/api/scripts/skill_texts.jsonl` holds the cases both must answer the same way. The built-in skills' names and descriptions come from HikariSub's own copy of `src/ai-sidecar/skills`, compiled in. A released HikariSub refuses a sidecar bundle with files it doesn't know, so the bundle can't carry the list. If a sidecar update adds a built-in skill, Settings lists it after the next HikariSub update; the model always gets the sidecar's set.

### The built-in skills

| Skill | For | Extended by |
| --- | --- | --- |
| `typesetting` | Placing signs with override tags and checking them on the frame; a tag reference. | AI9 [#437](https://github.com/altqx/hikari/issues/437) |
| `translation` | Translating Lines: meaning, consistency, tags, reading speed, Translation mode. | AI10 [#438](https://github.com/altqx/hikari/issues/438) |
| `karaoke-templater` | Writing and fixing karaoke templates; the template language. | AI11 [#439](https://github.com/altqx/hikari/issues/439) |

To add one, put its folder under `src/ai-sidecar/skills/` and its files in `kBundledSkills` (`src/ai-sidecar/src/skills/bundled.ts`). A test fails when a file under `skills/` isn't bundled, when a skill breaks the format, or when a link in a `SKILL.md` names no file of the skill. The built-in catalogue must stay within 600 tokens.

## Progressive disclosure

1. **The catalogue.** On a thread's first turn the sidecar adds a `## Skills` section to the system prompt: a line per skill with its name, `(project)` for the Project's, `(yours)` for yours, and its description, sorted by name. Past 20,000 characters, the rest of the skills are named without their descriptions. The section stays the same for the whole thread, so the prompt cache keeps it.
2. **`read_skill {name}`** gives the skill's `SKILL.md` without its frontmatter, followed by the names of its other files.
3. **`read_skill {name, file}`** gives one of those files.

`read_skill` is always offered, like `update_plan`, and asks no permission: it only reads. Errors come back as text for the model: an unknown skill lists the skills, and an unknown file lists the skill's files.

## Outside Hikari AI's own threads

[AI14c #944](https://github.com/altqx/hikari/issues/944):

- **External agent threads.** A Claude, Codex or ACP agent thread in the Agent panel ([AI15](ai-external-agents.md)) gets the same catalogue, with its thread's Project: after Claude's system prompt, in Codex's developer instructions, and for an ACP agent in the instructions its first prompt opens with. `read_skill` is one of the tools on its private channel, so Claude sees it as `mcp__hikari__read_skill`.
- **MCP clients.** Agents outside HikariSub get skills only with **Settings › AI › MCP › Let them read your skills** (`ai.mcp.skills`), off by default (owner Q403). With it on, an MCP connection may call `skills.*`, and sees the Project of the active window at each call (owner Q404). `tools/list` then offers `read_skill`, with the catalogue in its description, since an MCP client has no system prompt for the server to add to. Each read carries the same preface. With it off, `read_skill` isn't listed, and calling it fails as an unknown tool. The server's instructions name `read_skill` in one line; there are no skill resources or prompts.
- Built-in skills are offered both ways, and so are an untrusted Project's, as in a thread.

**What is sent › AI (Skills)** (the Site's page; [#944](https://github.com/altqx/hikari/issues/944), owner Q405): "Skills: when an agent reads a skill, its SKILL.md and the files it asks for go to that agent's provider. That's this Project's .hikari/skills, your skills folder, or HikariSub's built-in skills. Agents outside HikariSub get skills only when Settings › AI › MCP › Let them read your skills is on."

## The Hikari API

The sidecar reads no files itself ([ADR 0026](adr/0026-hikari-ai-runs-in-a-typescript-sidecar-on-bun.md)), so the Project's skills and yours come through two [Hikari API](api.md) operations. Only a caller with the skills grant may call them: Hikari AI's threads, external agent threads and their tool calls have it, MCP clients only while `ai.mcp.skills` is on, and plugins never.

| Operation | Gives |
| --- | --- |
| `skills.list` | `{skills: [{skill, origin, files}]}`: each skill folder's name, `origin` `project` or `yours`, and its files, `/`-separated and sorted, `SKILL.md` among them. A Project skill hides yours of the same name. |
| `skills.read {skill, file}` | `{skill, file, size, data}`: one listed file as UTF-8 text, from the skill `skills.list` gives. |

A file that isn't listed gives `not_found`, one over 256 KiB `refused` with `too_large`, and one that isn't UTF-8 `refused` with `not_text`. The caller's Project is `Caller::project`; an agent thread's caller carries the thread's Project. The host port is `SkillHost` (`hikari/api/host.h`). `SkillTiers` implements it over `<user folder>/ai/skills` and the caller's `<Project>/.hikari/skills`, each read by a `FolderSkills`.

## Where it lives

| Path | Holds |
| --- | --- |
| `src/api/skill_operations.cpp` | `skills.list` and `skills.read`. |
| `src/api/folder_skills.cpp` | `FolderSkills`: the folder walk and its limits. `SkillTiers`: the Project over yours. |
| `src/api/skill_check.cpp` | The format's rules in C++, for Settings. |
| `src/app/skills_settings.cpp` | Settings › AI › Skills: the groups, Open folder, and the built-in skills compiled in. |
| `src/ai-sidecar/src/skills/skill.ts` | The frontmatter reader and the format's rules. |
| `src/ai-sidecar/src/skills/skills.ts` | `Skills`: the catalogue and `read_skill` for one thread, and the MCP form of `read_skill`. |
| `src/ai-sidecar/src/mcp/server.ts` | `read_skill` over MCP, for callers that may read skills. |
| `src/backends/ai_mcp_server.cpp`, `src/app/ai_mcp.cpp` | The `ai.mcp.skills` grant on MCP connections, with the active window's Project. |
| `src/ai-sidecar/src/skills/bundled.ts` | The built-in skills, compiled into the bundle. |
| `src/ai-sidecar/skills/` | The built-in skills' files. |

## Tests

- `hikari_api_tests` (`tests/api/skill_tests.cpp`, `scripts/skills.jsonl`): listing and reading the fixture skills, the grant, names, hidden files, symbolic links, paths outside the folder, the depth, file and skill limits, the file cap's order, the size limit, and the Project over yours (origins, hiding, a symlinked `.hikari`).
- `hikari_api_tests` also runs `scripts/skill_texts.jsonl` through the C++ check, and the folder report's reasons.
- `hikari_ui_project_windows_tests`: the Settings groups in a window with and without a Project, the built-in skills matching `src/ai-sidecar/skills`, and Open project folder.
- `hikari_backends_ai_mcp_tests` and `hikari_ui_ai_features_tests`: an MCP connection reads skills only with the grant, from the active window's Project.
- `hikari_backends_ai_agent_tests`: a thread keeps the Project it started in, saves it, and lists that Project's skills; a thread without one doesn't.
- `hikari_ai_sidecar_bun_tests` (`src/ai-sidecar/test/skills`): the frontmatter, the built-in skills against the format, the catalogue with the Project's and your skills, replaced and broken skills, the Project's preface, HA1 refusing, one lookup per thread, the catalogue's room, and `read_skill`'s answers and errors. `test/agent` checks the system prompt carries the catalogue, `test/agents` that Claude's system prompt, Codex's developer instructions and an ACP agent's first prompt carry it and that each reads a skill over its channel, `test/mcp/server.test.ts` that `read_skill` is listed and reads only while HA1 lets the connection read skills, and `test/tools/budget.test.ts` counts `read_skill` in the up-front tools and the catalogue's tokens.
