## Agent skills

### Issue tracker

Issues are tracked in GitHub Issues for altqx/hikari (via the `gh` CLI). See `docs/agents/issue-tracker.md`.

### Triage labels

Default vocabulary: `needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`, plus `legacy` for issues about HikariSub legacy. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: one `CONTEXT.md` and `docs/adr/` at the repo root. See `docs/agents/domain.md`.

## Comments

Write as few comments as possible. Make the code say it instead: a better name, a smaller function, a type, a test. Inspired by Comment Sicko ([rubric](https://github.com/cursor/plugins/blob/de2dba6ffa001bc3c97acf1f58bc7ab29d5fdc7a/pstack/agents/comment-sicko.md)); the full audit was CM1 #463.

A comment may stay only if it is one of these:
- a licence header;
- non-obvious behaviour forced by an external dependency, platform, vendor or protocol we can't change;
- a doc comment that defines a public API contract;
- an issue or RFC link for a constraint the code can't express.

Everything else goes: narration, comments that restate the code, section banners, commented-out code, `} // namespace` closers, legacy file:line citations, card or ledger ids, and long justifications. If you aren't sure a comment fits the list, delete it.

- **Our own surprises:** don't explain them. Put one line, `// MUST KILL: <what is surprising>` (`# MUST KILL:` in scripts), on the exact symbol, so a later rename or refactor can make the behaviour obvious.
- **Kept comments are short,** usually one line. Never shorten a justification into a smaller excuse; reshape the code or flag it.
- **Leave these alone:** translator comments that lupdate extracts (`//:`, `//~`, `/*: */`, `//%`), tool directives (`NOLINT`, `clazy`, `clang-format`, `#pragma`, `qmllint`), shebangs and vendored third-party files. A lint suppression that hides a real correctness or safety problem gets a `MUST KILL` flag.
- **Same rules for new code:** a PR doesn't add comments outside the list above.
