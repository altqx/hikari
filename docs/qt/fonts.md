# Subtitle font identity and collection

**Accepted contract, 2026-09-27:** the user chose **“Verify renderer/font-file agreement”** in [Choose font enumeration and matching strategy](https://github.com/altqx/hikari/issues/35). [ADR 0007](../adr/0007-renderer-verified-subtitle-fonts.md) records the tradeoff. A later Windows experiment establishes a bounded diagnostic seam; full collection completeness and cross-platform qualification remain unproved.

## Authority and ownership

Qt supplies application typography and presents the subtitle picker. The subtitle-font service supplies provider-aware names and identities. An optional Qt-rendered sample must be labelled as such; it is not proof of ASS output. Exact ASS previews use the document's pinned libass build, selected font provider, attachments, configuration and render context.

The collector must verify the actual font bytes, collection faces, fallback and simulations used by that rendering environment. A family-name match in Qt is insufficient. On Windows, use GDI-compatible DirectWrite metadata/resolution; on Linux, align fontconfig configuration with the selected libass provider. Record the provider and version instead of assuming one from the OS. A future CoreText adapter must fit the same boundary.

Preserve authored ASS names and weight/italic requests. Localized labels, aliases, legacy/typographic family names and full/PostScript names are searchable metadata, not replacements to write into the document. Requested and resolved identities remain distinct. Equivalent behavior across platforms does not mean identical fonts or pixels on different installations. Optional CSRI/VSFilter output needs its own agreement evidence.

## Identity and collection contract

Resolved identity includes source bytes/hash, path or attachment identity, collection face index, known variation/named-instance coordinates, provider/configuration generation and fallback provenance. Do not collapse TTC/OTC faces, or pretend every variable-font instance is interchangeable. Unverified instance support must be reported explicitly; no new ASS axis syntax is implied.

Resolve all used styles and inline font/reset/weight/italic changes across the relevant original/translated text. Drawing commands do not require glyphs. A single preview trace cannot certify a whole document. Distinguish an absent requested font, a missing glyph, a known alias, synthetic style and actual fallback. Export original attachment bytes when the font has no local file.

Collection results must distinguish verified files/faces from unresolved requests. Unknown identity cannot receive a success/completeness claim. Report known fallback files and their role without presenting them as the originally requested font. Verify export/reimport in a clean font environment; a list of copied files alone is not agreement evidence.

libass's existing internal selection logs and public message callback do not establish exact selected bytes in the observed stream-backed cases. The user accepted maintaining a narrow diagnostic hook that captures actual provider bytes and opened faces, with distinct captured, missing, fallback and incomplete statuses. Pin source and regression coverage; the production callback ABI, cache/lifetime handling and cross-platform integration still need qualification.

## Document environments and lifetime

Keep attachment bytes/name/hash and document ownership independent of filesystem paths. Separate document font environments so equal attachment names cannot leak between documents. Characterize duplicate attachment/system precedence; do not invent an attachment-always-wins rule. Avoid process-wide Qt registration merely to populate picker labels.

Font installs/removals, attachment edits and provider/configuration changes create new environment generations and invalidate affected resolutions, previews and collection reports. Provide explicit refresh as well as debounced notifications. Qt refresh does not refresh libass's caches. Recreate affected libass contexts through their owner and retain source bytes until all consumers finish. Pinned tools use their own target document's environment.

## Required feasibility evidence and UI review

Use licensed fixtures on Windows and Linux for aliases, vertical names, TTC/OTC faces, variable instances, simulated styles, CJK/RTL/combining text, missing glyphs, fallback and conflicting attachments. Record renderer-selected identities against collected hashes/face indices, refresh behavior, concurrent documents and cancellation. Report inconclusive cases. [Prototype renderer-verified font identities and collection diagnostics](https://github.com/altqx/hikari/issues/47) must establish the diagnostic mechanism before the final implementation plan can call this contract proved.

Picker, preview, font-catalog and collector workflows still need runnable surface prototypes and user reactions. Choosing strict agreement does not approve their screen layout or remove any existing capability.

## Reviewed Windows experiment

The user accepted the diagnostic approach after reviewing the [native experiment](https://github.com/altqx/hikari/blob/0f776e446df085d463fc82c1073a868a7dd24c51/HikariSub/prototypes/font-identity/README.md). All 25 sampled stock/hook frames agreed; 17 of 18 captured-set reimports agreed. The multilingual reimport failed despite loading all three captured files because system fallback behavior was not reconstructed. Equal pixels can also reproduce a missing request or glyph; no whole-document completeness claim follows.

The experiment discloses its Debug/archive provenance and guarded invalid source-version diagnostic. Linux/fontconfig, variable instances, fallback reconstruction, concurrent refresh/cancellation and a production hook ABI remain open in the native prototype ticket. This acceptance does not remove those gates or approve a collector UI.

The later [fallback diagnosis](https://github.com/altqx/hikari/blob/1dbe1275e45c294905a41f6577146d983fbf06b5/HikariSub/prototypes/font-identity/diagnostic/README.md) reproduces the original mismatch and narrows its cause: provider NONE loads the captured files but has no cross-family fallback resolver. Nineteen fresh-context cases repeated consistently. The captured TTC face and emoji file reproduce their isolated glyphs when the appropriate default family is selected; reversing attachment order does not repair the original replay. Restoring DirectWrite with the captured attachments reproduces the original full frame on the same host. This rules out missing capture bytes or the TTC face as the cause for these specimens, not for every font environment.

Same-host provider-assisted reproduction is not a self-contained portable font bundle. Explicitly adding font tags to the synthetic full text selects the expected identities but changes its pixels, so authored-name rewriting is not a transparent remedy. The original 17/18 result and incomplete status remain intact. A recorded fallback representation and any diagnostic replay seam need further design and evidence; this diagnosis does not select a production resolver or weaken strict agreement.

Evidence and reviewed alternatives: [font-resolution study](proposals/font-resolution.md), [completed source research](https://github.com/altqx/hikari/blob/aff69e6c950a1ab03d02eb837ece9d47b90d1dae/docs/research/fonts.md).
