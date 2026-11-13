# AI translation pipeline

The Translate inline action runs Honya's pipeline in the sidecar ([AI10 #438](https://github.com/altqx/hikari/issues/438), [ADR 0024](adr/0024-hikari-ai.md) "Translation", [ADR 0026](adr/0026-hikari-ai-runs-in-a-typescript-sidecar-on-bun.md)). It is the only translate path: **Translate to <language>** and **Translate to…** in the [inline actions](ai-inline-actions.md) both use it, on one Line or many.

## Where it lives

| Path | Holds |
| --- | --- |
| `src/ai-sidecar/src/translate/` | The pipeline (`pipeline.ts`), its prompts, the deterministic audit, Honya's file format (`honya.ts`) and `_hikari/translate` (`run.ts`). |
| `src/backends/ai_translation_run.{h,cpp}` | `TranslationRun`: one `_hikari/translate` request for the whole run, with Stop, the daily cap and the cost. |
| `src/app/inline_ai_controller.cpp` | Picks the Lines, writes the results through the review, and writes the project files after them. |
| `src/api/translation_operations.cpp` | HA1's `translation.project` and `translation.project_write`. |
| `src/application/translation_project.{h,cpp}` | Where a Document's project folder is. |

## The pipeline

1. **Chunks.** The Lines, in Grid order, are packed into chunks of about 1,000 tokens (1,200 at most). A Line is never split, and a gap of 5 s or more starts a new chunk once a chunk is half full.
2. **Pre-pass.** When the project has no glossary and no characters and the run has more than one chunk, the translator lists the recurring characters and terms from a sample of the Lines. It may use only what the Lines show.
3. **Translator.** Each chunk goes with only the glossary terms and characters it mentions (at most 80 and 40), the style notes, up to 4 style examples, and the last 5 translated Lines. The answer is JSON: a text per Line id, plus the new terms and characters it used.
4. **Audit.** Deterministic gates: every Line is answered, every `⟦n⟧` marker comes back once and in order, there are no braces, and the glossary's controlled terms hold (`hard_locked` is used, `forbidden` and the forbidden renderings aren't, "keep as is" stays untranslated). A finding rejects the chunk without asking the reviewer.
5. **Reviewer.** It approves the chunk, or rejects it with one item per problem. A rejection goes back to the translator as feedback, up to 3 times. When the tries run out, the attempt with the fewest audit findings is kept, and its Lines need review.
6. **Discoveries.** Only an approved chunk adds terms and characters, and only ones not there yet. An existing entry is never rewritten, protected or not.
7. **Coherence.** With more than one chunk, the reviewer reads the whole run, about 8,000 tokens at a time, for drift: names, honorifics and terms written two ways, and pronoun or register changes. A fix is used only if it passes the audit.

Tags never leave the app: Qt sends each Line with its override blocks as markers, as the other inline actions do, and writes the tags back from the markers.

## Roles and models

The translator role runs the pre-pass and the translator. The reviewer role runs the reviewer and the coherence sweep, and falls back to the translator's model.

A Project can choose the models with `[translation] translator` and `reviewer` in `.hikari/project.toml`, each `"<provider id>/<model>"` (owner, Q347, 2026-11-12):

- The provider must be one you already added; otherwise your role's model runs.
- They apply only in a trusted Project. In an untrusted one your role models run, and the run's notice says so.
- **A Project never carries a credential**, because it may be in git. A `[translation]` entry whose name looks like a key, token, secret, password, auth, cookie, session, URL, endpoint or base, and a model value holding an address, whitespace or a key prefix such as `sk-`, is never read. The Log warns about it and says keys go in this Project's credentials file (`.hikari/credentials.toml`) or this machine's environment ([PJ10 #982](https://github.com/altqx/hikari/issues/982)). HikariSub never writes such a value into project.toml. Keys stay in the OS keychain and providers in Settings › AI.

## The result

- **Translation mode on:** the run writes the Translated text, and the Original text stays as it is. A Line the reviewer never approved is marked Unconfirmed; other Lines keep their flag.
- **Translation mode off:** the run replaces the text, as the other actions do. A Line that needs review is only listed in the notice.
- In Manual mode each result shows what the reviewer didn't approve.
- The project files change only after the Lines are written: at the end of an Auto or Full access run, or on **Apply** in Manual.

## The project folder

The glossary, the characters and the style live in Honya's project files, so Honya and HikariSub share them: `PROJECT.md`, `GLOSSARY.md`, `CHARACTERS.md` and `STYLE.md`. Each file is Markdown over a `<!-- honya:data … honya:data -->` JSON block, and the block is the truth. HikariSub keeps every field it doesn't know, and writes the table above the block again, as Honya does.

The folder is:

- in a Project, its root, or the subfolder `translation.glossary` names in `.hikari/project.toml` (it can't leave the Project);
- outside a Project, the folder the Script Info key `Honya Project` names, relative to the subtitle's folder (owner, Q348);
- otherwise none: the run works without a glossary, and its discoveries aren't saved.

The sidecar reads the folder with `translation.project`. It is a read tool in the agent's `translate` group. `translation.project_write` writes one of the four files, and only a translation run's own write uses it; no agent tool offers it.

## What is sent

What the other inline actions send, for each Line, without the context Lines, plus the speaker (Actor). It also sends the project files' glossary terms and characters that the chunk mentions, the style notes and examples, the synopsis from `PROJECT.md`, and the translations of the 5 Lines before the run in Translation mode. When Settings › Project › Translation names a source language, the run also sends it; otherwise the models detect the source language from the Lines.

## Cost

The estimate before a run counts three passes over the Lines: translator, reviewer and coherence. Retries cost more. Each model call's usage counts toward the daily cap as it comes, and reaching the cap stops the run.

## Tests

- `bun test test/translate`: the fixture episode end to end against a mock provider, covering locked terms, markers, feedback retries, Lines that need review, discoveries, the pre-pass, coherence, progress, and the prompts with and without a source language.
- `hikari_api_tests` (`Translation.*`): the two operations.
- `hikari_application_project_tests` (`HonyaFolder.*`): where the folder is.
- `hikari_ui_project_file_tests`: the model keys are read, and key-like entries are ignored and never written.
- `hikari_backends_ai_inline_run_tests`: `TranslationRun`'s request with and without a source language, results, Stop and no model.
- `hikari_ui_inline_ai_tests`: Translation mode writes the Translated text, marks Unconfirmed, and writes the glossary; the source language is sent only when one is set.
