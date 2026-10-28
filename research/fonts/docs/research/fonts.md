# Font enumeration and matching across Windows and Linux

Research ticket: [#21](https://github.com/altqx/hikari/issues/21). Feeds: Choose font enumeration and matching strategy.
Date: 2026-09-27. Status: complete source investigation; runtime compatibility remains unmeasured.

## Answer and decision boundary

Recommendation: use QFontDatabase for ordinary UI typography and as a picker presentation aid, with a separate subtitle-font service for legacy ASS names, renderer matching, attachments, file identity and collection. Windows should investigate GDI-compatible DirectWrite resolution; Linux should use fontconfig metadata and the same configuration as the libass renderer. Use libass itself for authoritative ASS previews. A Qt text sample is useful, but is not evidence that libass or VSFilter selects or renders the same face.

This is a proposed division of responsibilities, not an adopted architecture. No font enumeration, refresh, attachment collision or renderer-parity experiment was run. The evidence below establishes available APIs and existing behavior; the decision still needs a small reference-font corpus and explicit compatibility target.

## Existing HikariSub behavior

At repository baseline 20d647c4, FontEnumerator uses LOGFONT/EnumFontFamiliesEx and maintains a sorted family list with observers and directory-change watching. The Linux compatibility layer supplies fontconfig enumeration/matching and inotify-backed font-directory watching. Thus the apparent GDI-shaped interface already masks different platform behavior. [FontEnumerator](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontEnumerator.cpp), [Linux platform adapter](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/platform.h).

FontCollector gathers fonts from style/override usage, selects a LOGFONT and checks glyph coverage. For export it obtains font bytes with GetFontData (trying a collection first), searches files of the same size, then compares bytes. Windows searches system, per-user and configured external font folders; Linux builds its path list from fontconfig. This is more substantial than enumerating a family name, but file scanning and byte comparison should not be mistaken for a universal face-to-file API. [Current collector](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCollector.cpp).

The current renderer passes provider value 1 to ass_set_fonts, meaning AUTODETECT in the pinned libass API. It passes "Arial" as both default_font and default_family; the API defines the former as a file path, so the new integration should make those concepts explicit. This inspection does not establish a visible failure in the current build. [Renderer](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubtitlesLibass.cpp), [pinned API](https://github.com/libass/libass/blob/4a05d8127f525943ebf45fdc6497c9e665947f0d/libass/ass.h).

## What libass actually selects

The repository pins libass 4a05d812. Its compiled provider table tries CoreText, DirectWrite, then fontconfig, skipping uncompiled entries; AUTODETECT selects the first successfully initialized provider. It also creates an embedded-font provider. Therefore "Windows uses DirectWrite" and "Linux uses fontconfig" are expected build configurations, not platform guarantees. Query ass_get_available_font_providers and capture the selected-provider log in diagnostic output. [Provider construction and selection](https://github.com/libass/libass/blob/4a05d8127f525943ebf45fdc6497c9e665947f0d/libass/ass_fontselect.c).

The selector matches names and requested weight/italic, checks character support, and consults provider substitutions/fallbacks. Its embedded metadata includes family, full and PostScript names. A successfully rendered glyph can therefore come from fallback rather than the requested family. Recording only the QFont family or ASS font string loses the actual face and fallback provenance. [Selector](https://github.com/libass/libass/blob/4a05d8127f525943ebf45fdc6497c9e665947f0d/libass/ass_fontselect.c).

On desktop Windows, the DirectWrite provider uses GDI interop, including creating a face from an HDC. It reads Win32 family information and other names instead of simply treating a modern typographic family as the ASS family. This also helps recover the selected face in a collection. The non-desktop path uses a LOGFONT conversion and explicitly favors Win32 naming. [DirectWrite provider](https://github.com/libass/libass/blob/4a05d8127f525943ebf45fdc6497c9e665947f0d/libass/ass_directwrite.c).

The fontconfig provider scans outline-font patterns, retaining paths, indices, family/full/PostScript names and character coverage; converts fontconfig weights to OpenType weights; applies configuration substitutions; and builds a fallback list. It owns its own FcConfig, so updating a different fontconfig or Qt database does not automatically update this provider. [fontconfig provider](https://github.com/libass/libass/blob/4a05d8127f525943ebf45fdc6497c9e665947f0d/libass/ass_fontconfig.c).

## Family/style/weight compatibility

GDI's legacy RBIZ family model groups regular, bold, italic and bold-italic faces. A broader typographic family can include widths and many weights that GDI exposes under separate names. For example, normalizing every legacy name to a modern family and selecting a nominal weight can change document meaning. Microsoft documents the distinction and DirectWrite's compatibility mechanisms. [Font-family models](https://learn.microsoft.com/en-us/windows/win32/directwrite/font-selection).

The inspected xy-VSFilter fork constructs a LOGFONT from the ASS style's font name, charset, weight and italic flag, and calls CreateFontIndirect. It is concrete evidence for preserving legacy Windows name semantics, not a guarantee that all VSFilter forks and versions match identically. [xy-VSFilter RTS](https://github.com/pinterf/xy-VSFilter/blob/4911cacb102bc0cbd41449793e54473265b04e62/src/subtitles/RTS.cpp), [style conversion](https://github.com/pinterf/xy-VSFilter/blob/4911cacb102bc0cbd41449793e54473265b04e62/src/subtitles/STS.cpp).

Recommendation: preserve the exact document font string, carry aliases separately, normalize ASS boolean bold versus numeric weight in one tested adapter, and expose substituted/missing/synthetic faces in collector results. Never serialize a localized UI family label or translated style label back over an ASS name. Include vertical @ names, full names, localized aliases, TTC face indices, explicit numeric weights and synthetic bold/italic in compatibility fixtures.

## API comparison

| API | What it provides | Implication for HikariSub |
| --- | --- | --- |
| QFontDatabase | Families, styles, writing systems, weights, sample fonts; application fonts from files or bytes. Its documented public surface has no general selected-font-file lookup. | Good for UI typography/presentation; insufficient alone for collecting the exact subtitle face. |
| GDI + DirectWrite | Legacy LOGFONT selection plus face metadata, simulations and local-file-loader path recovery. | Candidate Windows subtitle resolver; handle nonlocal/memory loaders explicitly. |
| fontconfig | Configured enumeration, aliases, weight/slant matching, charset, file and index; application font sets. | Candidate Linux resolver; distinguish exact requested-name availability from nearest fallback. |
| CoreText | Font descriptors and URL attributes; platform-specific family/trait semantics. | Future adapter, not a reason to assume Qt family names reproduce Windows ASS matching. |

Sources: [Qt API](https://doc.qt.io/qt-6/qfontdatabase.html), [Microsoft models](https://learn.microsoft.com/en-us/windows/win32/directwrite/font-selection), [fontconfig developer reference](https://fontconfig.pages.freedesktop.org/fontconfig/fontconfig-devel/), [CoreText URL attribute](https://developer.apple.com/documentation/coretext/kctfonturlattribute), and the concrete Aegisub adapters below. The absence of a Qt path API is an inference from its public surface, not a claim that Qt internally lacks file information.

## Attached MKV and memory fonts

Matroska specifies current font media types and legacy types such as application/x-truetype-font, plus case-insensitive filename fallback for some octet-stream attachments. The demux/import layer must supply those bytes; libass is not an MKV attachment demuxer. Recognizing a container MIME type does not prove a particular Qt/libass build can load every font encoding. [Matroska attachment rules](https://www.matroska.org/technical/attachments.html).

libass provides ass_add_font(library, name, data, size); the pinned implementation copies name and bytes. ass_set_fonts_dir supports a font directory. ass_clear_fonts requires all tracks and renderers for that library to be released first, so document switching needs a deliberate lifecycle. QFontDatabase::addApplicationFontFromData separately registers bytes with Qt and returns a removable application-font ID. One registration does not establish registration in the other engine. [libass API](https://github.com/libass/libass/blob/4a05d8127f525943ebf45fdc6497c9e665947f0d/libass/ass.h), [copy implementation](https://github.com/libass/libass/blob/4a05d8127f525943ebf45fdc6497c9e665947f0d/libass/ass_library.c), [Qt registration](https://doc.qt.io/qt-6/qfontdatabase.html#addApplicationFontFromData).

Recommendation: store attachment identity, bytes/hash, original name, face index and document ownership independently of a filesystem path. Register with each consumer that needs it; preserve the bytes for collection instead of asking a local-file loader for a nonexistent path. Define duplicate-family precedence and document isolation, then verify them with conflicting attached/system fonts. Do not promise a universal "attachment always wins" rule without testing the selected provider and version.

## Font changes and cache lifetime

Qt emits QGuiApplication::fontDatabaseChanged. Windows WM_FONTCHANGE tells applications to reenumerate after the font pool changes, and DirectWrite GetSystemFontCollection accepts checkForUpdates. These are notification/requery mechanisms, not guarantees that existing renderer caches refresh themselves. [Qt signal](https://doc.qt.io/qt-6/qguiapplication.html#fontDatabaseChanged), [WM_FONTCHANGE](https://learn.microsoft.com/en-us/windows/win32/gdi/wm-fontchange), [DirectWrite refresh](https://learn.microsoft.com/en-us/windows/win32/api/dwrite/nf-dwrite-idwritefactory-getsystemfontcollection).

fontconfig offers FcConfigUptoDate to inspect configuration timestamps and FcInitBringUptoDate to refresh the default configuration according to its rescan interval. Application fonts can be added to a configuration separately. libass's deprecated ass_fonts_update is explicitly a no-op, so it must not be the refresh implementation. [fontconfig reference](https://fontconfig.pages.freedesktop.org/fontconfig/fontconfig-devel/), [libass API](https://github.com/libass/libass/blob/4a05d8127f525943ebf45fdc6497c9e665947f0d/libass/ass.h).

Recommendation: debounce changes, rebuild one immutable catalog generation in the owning service, invalidate collector matches, then recreate/reconfigure renderers under their thread ownership rules. Retain old font bytes until their consumers finish. Provide explicit Refresh Fonts as a fallback; test installs/removals while a document and font popup are open.

## Aegisub's concrete collector design

Inspected arch1t3cht/Aegisub feature commit 9bfd5008:

- The collector parses used styles plus inline font, bold, italic and reset tags; tracks Unicode characters; excludes drawing text from character checks; reports missing glyphs and simulated styles. It collects dependencies inferred from the script, rather than tracing every rendered fallback glyph. [Common collector](https://github.com/arch1t3cht/Aegisub/blob/9bfd5008d9fc30bd86b3634416781d2c41ce8f89/src/font_file_lister.cpp).
- Windows maps ASS bold 0/1 to 400/700, selects a GDI font, rejects a differing GetTextFace name, converts the HDC to a DirectWrite face, checks coverage/simulations, and resolves its font-file loader key through IDWriteLocalFontFileLoader. It has a newer IDWriteFontFace3 path for private fonts that are absent from the system collection. Nonlocal loaders can fail the local-path query. [Windows adapter](https://github.com/arch1t3cht/Aegisub/blob/9bfd5008d9fc30bd86b3634416781d2c41ce8f89/src/font_file_lister_gdi.cpp).
- Linux strips the vertical @ prefix, builds a candidate set with matching family/full names before sorting by weight/slant, then obtains FC_FILE and coverage from the chosen pattern. This prevents fontconfig's nearest fallback from silently counting as finding the requested font. It does not prove equality with libass's fallback policy. [fontconfig adapter](https://github.com/arch1t3cht/Aegisub/blob/9bfd5008d9fc30bd86b3634416781d2c41ce8f89/src/font_file_lister_fontconfig.cpp).
- macOS uses descriptor URLs and character sets plus custom Windows-name and trait handling for VSFilter compatibility. It is a useful later implementation reference rather than a ready-made cross-platform Qt policy. [CoreText adapter](https://github.com/arch1t3cht/Aegisub/blob/9bfd5008d9fc30bd86b3634416781d2c41ce8f89/src/font_file_lister_coretext.mm).

## Follow-on decision and validation

The decision should specify: authoritative preview renderer/provider per OS; preserved document names versus UI labels; strict collection versus fallback collection; file/face/attachment identity; duplicate-family precedence; cache invalidation; and whether VSFilter compatibility is a named mode or a best-effort target.

A follow-on native spike should use licensed redistributable fixtures with legacy/typographic name differences, multiple TTC faces, missing bold/italic, CJK/Arabic combining text, missing glyphs, @ names and duplicate attached/system families. Record provider/version, selected paths or attachment IDs, face indices, warnings and rendered comparisons on Windows and Linux. Verify export/reimport in a clean font environment. No such measurements are claimed here; current citations are sufficient to close the research question while leaving the architecture decision and compatibility acceptance open.
