# Harper (GR1 #364)

harper-ls is pinned in `cmake/HikariHarper.cmake`. When the pin moves to a new release:

1. Update the version and the SHA-256 values there: the archives, the binaries in them, and `LICENSE` at the tag.
2. Regenerate the rule groups from that tag's `harper-core/default_config.json` and its `harper-cli config`:
   `python3 -I tools/harper/rule_table.py default_config.json cli-config.json > src/core/grammar_rules.inc`
3. Regenerate the crates' notices from a checkout of the tag with cargo-about (built with `--features cli`):
   `cargo about generate -c tools/harper/about.toml -m <harper>/harper-ls/Cargo.toml --locked tools/harper/about.hbs -o packaging/licenses/harper/THIRD-PARTY.txt`
   Put the new version in `about.hbs` first.
