# Font resolution proposal

**Status: proposal for [Choose font enumeration and matching strategy](https://github.com/altqx/hikari/issues/35); not an accepted decision or verified implementation.** Based on [completed font research](https://github.com/altqx/hikari/blob/aff69e6c950a1ab03d02eb837ece9d47b90d1dae/docs/research/fonts.md), the [Hikari-owned architecture](../architecture.md) and [video proposal](video-pipeline.md).

## Human choice

**Recommend a provider-aware subtitle-font service, libass-authoritative ASS previews and strict collection with explicit unresolved results.** Accept the platform adapters and renderer-identity instrumentation needed for that contract, or prefer a simpler best-effort collector that cannot promise agreement with libass? Qt-only family matching is insufficient for the requested agreement and file collection.

This choice concerns correctness and maintenance cost. Picker layout, search/filter controls and diagnostics presentation belong in later QML/HTML prototype tickets.

## Proposed ownership

| Consumer | Authority |
| --- | --- |
| Application typography | `QFontDatabase` and Qt matching for ordinary UI text. |
| Subtitle font picker | Subtitle service catalog: document attachments plus platform metadata. Qt presents labels and may offer a clearly identified Qt sample; that sample is not an ASS preview. |
| Exact ASS style preview | The same pinned libass build, selected provider, font environment and rendering configuration used for the document's video subtitles. |
| Collector | Declared font requests plus verified renderer face/file identities, glyph coverage and fallback provenance; never infer a file from the Qt family label alone. |

On Windows, use GDI-compatible DirectWrite resolution to preserve legacy ASS family semantics and recover local face/file metadata. On Linux, use fontconfig with configuration aligned to libass's fontconfig provider. Explicitly select/verify the intended libass provider and report unavailable builds; do not infer it solely from the OS or leave AUTODETECT unrecorded. A future macOS adapter can use CoreText without changing the core's font-request contract.

Parity means preserved document requests, equivalent diagnostics and demonstrated renderer agreement on each platform. It does not imply identical system fonts, substitutions or pixels across different installations. Optional CSRI/VSFilter output needs its own named compatibility evidence; a libass match does not certify it.

## Identity and collection

Preserve the exact ASS font string and weight/italic request. Keep localized labels, legacy/typographic families, full/PostScript names and aliases as metadata; never overwrite document names with picker labels. Distinguish requested identity from the face actually selected, including synthetic bold/italic and per-glyph fallback.

Resolved identity includes provider/version/configuration generation, source bytes/content hash, local path or attachment ID, collection face index and available named-instance/variation coordinates. A path or family string alone is insufficient. Do not collapse TTC/OTC faces or treat a variable family as one interchangeable face. Validate default/named instances and axis handling; report unsupported or unverified instances rather than silently substituting or inventing ASS axis serialization.

The collector must account for used styles, inline font/reset/weight/italic changes and the rendered original/translation text, excluding drawing commands from glyph checks. Separate requested-font absence, missing glyphs, aliases, simulations and fallback files. Strict mode reports incomplete collection when identity cannot be established; it must not label a nearest substitute as the requested font. A single preview trace cannot certify every font request in a document.

The [pinned libass selector](https://github.com/libass/libass/blob/4a05d8127f525943ebf45fdc6497c9e665947f0d/libass/ass_fontselect.c#L817-L877) records selection internally and emits messages; its [public message callback](https://github.com/libass/libass/blob/4a05d8127f525943ebf45fdc6497c9e665947f0d/libass/ass.h#L330-L345) is not an implemented HikariSub identity/export contract. First prove that diagnostics plus provider metadata identify the actual bytes/faces. If insufficient, propose a narrowly maintained, versioned diagnostic hook before claiming strict agreement. Do not turn an internal libass function into an assumed stable API.

## Attachments and cache lifetime

Keep original attachment name, bytes/hash, face identity and document ownership independent of filesystem paths. Collect original bytes when no local file exists. Isolate document font environments so same-name attachments in two documents cannot silently change each other's preview. Record and verify duplicate attachment/system precedence; do not promise that attachments always win. Avoid process-wide Qt registration merely to populate picker names.

Use immutable catalog/font-environment generations. Font installs/removals, attachment changes and provider/configuration changes invalidate affected matches, previews and collection reports. Debounce notifications and provide explicit refresh; libass's separate provider caches do not refresh merely because Qt does. Recreate affected renderer/library contexts under their owner, retaining bytes until consumers finish. The [libass lifetime contract](https://github.com/libass/libass/blob/4a05d8127f525943ebf45fdc6497c9e665947f0d/libass/ass.h#L754-L768) requires releasing associated tracks/renderers before clearing library fonts. Pinned tool targets retain their own document font environment.

## Required proof

Run licensed fixtures on Windows/Linux covering aliases, vertical names, collection faces, variable instances, absent styles, CJK/RTL/combining text, fallback and conflicting attachments. Compare renderer-selected identities with collector output, then export/reimport in a clean font environment. Exercise refresh, concurrent documents and cancellation; record provider versions, hashes, face indices and unresolved cases. No native matching, pixel-parity, collection completeness or cache-lifetime pass is claimed here.
