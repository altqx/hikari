# GR1 #364 Grammar check with Harper: mockup

- `gen.py [TREE] [OUTDIR]` writes one HTML sheet per theme (`gr1-<theme>.html`), plus one page per section
  (`gr1-<theme>-<n>.html`). It is built on PF1's Settings mockup (evidence/w6-pf1-mockup): DG1's frame, PK1's
  controls, menu and toast, and the theme roles of src/ui/theme.cpp.
  - The Line editor, the Spellchecker panel (PU6) and the editor's context menu are drawn from today's QML
    (Main.qml, SpellCheckerPanel.qml).
  - Spelling and the syntax colours come from theme.cpp content(); the `grammar` underline colour is new.
  - The 15 rule groups are Harper's own curated grouping (harper-core/default_config.json at v3.0.0).
- `shoot.sh` renders every page with headless Chromium into `out/`. Run it under vx11.
- `research/lsp_probe.py EXE TEXT [CONFIG_JSON]` talks to harper-ls over stdio. It opens TEXT as plaintext,
  answers `workspace/configuration` with CONFIG_JSON, and prints the diagnostics, the code actions on the first
  one, and the RSS. `research/harper-config-v3.0.0.json` is the output of `harper-cli config` (936 rules).
