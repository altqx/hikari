# Localisation in Qt: gettext .po vs Qt .ts, migrating existing translations

Research for [#19](https://github.com/altqx/hikari/issues/19), completed 2026-09-27. Local baseline: `20d647c4c769ab7f5d383cf3c1c33f03876a94e9`. Source and catalog inspection only; no catalog conversion or QML runtime test was executed. Recommendations remain inputs to the localisation decision.

## Answer

Prefer Qt TS/QM for newly written QML and preserve existing translations as migration input. Keeping PO as an authoring format is feasible, either by a validated conversion pipeline or a custom QTranslator, but it shifts context/plural/extraction maintenance into HikariSub. A temporary mixed system is reasonable while old wx surfaces or Lua scripts remain; choose one authoritative source per string and define a retirement condition for the bridge.

Format migration is not text migration: converting a PO file into TS does not teach the new QML source which context identifies each translation, nor convert printf placeholders into Qt placeholders. Preserve translators' work and attribution, explicitly map keys, and review changed meanings.

## Existing catalogs and loading

The repository has `pl.po`, `ko_KR.po`, `th_TH.po`, `ta.po` and `template.pot`. English is the source language embedded in the binary, not a fifth translated PO file. Headers declare UTF-8; Polish has three plural forms, Korean and Thai one, Tamil two. Each inspected catalog contains three plural entries, including two font-count entries distinguished by contexts “found or copied” and “not found or not copied”, plus an element count. There are numerous `c-format` entries. [Catalog directory](https://github.com/altqx/hikari/tree/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/Locale), [Polish example](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/Locale/pl.po).

Startup registers the catalog path and a wxTranslations instance, chooses the configured language, loads the `hikarisub` domain, and restores numeric formatting to the C locale. Linux CMake invokes catalog compilation into `Locale/<lang>/LC_MESSAGES/hikarisub.mo`; source lookup uses wx translation calls. `aegisub.gettext` also delegates to wxGetTranslation. [Startup](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/hikarisubApp.cpp), [build](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/CMakeLists.txt), [automation bridge](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Automation.cpp). UI language must remain independent from subtitle serialization: translating a UI must not change decimal separators in ASS data.

## Options and tooling

| Approach | Workflow | Benefits | Costs / risks |
|---|---|---|---|
| Qt TS/QM | `qsTr`/`qsTranslate` or C++ translation calls → lupdate → translator edits TS → lrelease → QM | Native QML extraction, numerus support, QTranslator loading and CMake integration | New contexts need mapping to old gettext keys; XML diffs; key churn if component names move |
| PO + custom QTranslator | Keep PO/MO; override `translate(context, sourceText, disambiguation, n)` and bridge to a gettext/catalog implementation | Preserves existing PO workflow and old keys | Must define Qt context/disambiguation ↔ msgctxt mapping, plural-source lookup, fallback and thread behavior; QTranslator does not load MO itself |
| PO authoring, generated TS/QM | Extract new Qt strings and deterministically convert/merge for translator editing; generate runtime QM | Native runtime, familiar translator format | Conversion round trips require fixtures; never permit both PO and TS to be independently edited |
| Transitional mixed | Old domain/runtime for wx/Lua, Qt catalogs for rewritten surfaces | Incremental migration without discarding translations | Duplicate keys, inconsistent language switches and packaging; define domain ownership and removal plan |

Qt's translator lookup includes context, source text, disambiguation and count; a null result allows fallback. It has a virtual `translate`, making a bridge possible, but no native gettext parser. Installed translators are searched in reverse installation order, so layering needs an explicit precedence rule. [QTranslator](https://doc.qt.io/qt-6/qtranslator.html). The custom bridge is a design inference from this API, not an implementation tested here.

`qt_add_translations` creates update/release targets and can embed or install QM files; it uses LinguistTools and has version-sensitive arguments (e.g. `TS_OUTPUT_DIRECTORY` since 6.9; the earlier name was `TS_FILE_DIR`). Separate developer-controlled extraction from normal build compilation so ordinary builds do not rewrite translator sources. [CMake integration](https://doc.qt.io/qt-6/qtlinguist-cmake-qt-add-translations.html).

All three hosted platforms support both formats, so format choice need not force a provider change:

| Platform | PO | TS | Practical migration check |
|---|---|---|---|
| Weblate | [GNU gettext support](https://docs.weblate.org/en/latest/formats/gettext.html) | [Qt Linguist support](https://docs.weblate.org/en/latest/formats/qt.html) | Configure source language/file masks; preserve needs-review states and translator comments |
| Crowdin | [PO support](https://store.crowdin.com/gnu-gettext) | [TS support](https://store.crowdin.com/qtts) | TS supports plurals and comments but its source is not editable in Crowdin; verify importer context matching |
| Transifex | [PO support](https://help.transifex.com/en/articles/6220794-gettext-po) | [TS support](https://help.transifex.com/en/articles/6223301-qt-linguist) | Check context/comment identity, download mode and review-state handling |

Provider format support does not guarantee lossless round trips or identical project configuration. No hosted project settings were changed and no pricing/plan recommendation is made.

## Plurals and conversion

Use Qt numerus calls such as `qsTr("%n subtitle(s)", "", count)` and proper language-specific forms; do not append an English “s” or assume two forms. English source strings with plurals still need the source-language plural catalog generated by the Qt translation setup. Qt `%n` and numbered `%1` substitutions are different from existing printf `%d`/`%s`; conversion of file syntax does not rewrite application formatting. [Source translation guidance](https://doc.qt.io/qt-6/i18n-source-translation.html), [source plural catalog](https://doc.qt.io/qt-6/qtlinguist-cmake-qt-add-translations.html).

Qt's `lconvert` supports PO, POT, TS, QM, XLF and QPH, plus explicit source/target language options. A starting command is `lconvert -i Locale/pl.po -o translations/hikarisub_pl.ts -source-language en -target-language pl`. This is a proposed migration command, not an executed result. [lconvert](https://doc.qt.io/qt-6/linguist-lconvert.html).

Safe migration sequence:

1. Freeze an immutable PO baseline; retain original files and translator headers/history.
2. Extract new QML contexts using the selected Qt version. Establish a mapping from old `(msgctxt,msgid)` to new `(context,source,disambiguation)`; do not drop the two distinct font-count contexts.
3. Convert in a scratch directory and merge exact meaningful matches. Mark changed strings/placeholder schemes unfinished for review.
4. Compare counts, plural forms, translator comments, fuzzy/unfinished/obsolete flags, accelerator ampersands, line breaks, quotes and non-ASCII text. Round-trip fixtures must cover all four languages and identical source text in different contexts.
5. Compile QM files and run a small QML dialog in every language, with count values 0/1/2/5/12/22. Confirm missing-translation fallback and translated accessible names. Only then change the production authoring format.

## Runtime language switching and RTL

Replace/remove/install the relevant translators, then call `QQmlEngine::retranslate()` on each live engine. That refreshes bindings containing translation functions; strings cached in C++ models, preformatted command labels and custom wrapper results require explicit invalidation/notify logic. The engine also provides `markCurrentFunctionAsTranslationBinding` since 6.6 for suitable custom translation bindings. A language switch should refresh dialogs, actions, model display strings and accessible names, not only visible labels. [QQmlEngine](https://doc.qt.io/qt-6/qqmlengine.html#retranslate).

`LayoutMirroring.enabled` and `childrenInherit` support mirrored anchors/positioners/views; layout direction and text direction remain separate. Test RTL mixed with LTR ASS tags, timestamps, filenames and shortcut labels. The subtitle text editor must preserve document bidi behavior independently of UI direction; video geometry and time-axis direction need an explicit product decision, not blanket mirroring. [LayoutMirroring](https://doc.qt.io/qt-6/qml-qtquick-layoutmirroring.html). Existing supported languages do not remove the value of an RTL/pseudolocalized layout test.

## MuseScore and Audacity 4

MuseScore's inspected main revision `1c81f0a6f3eeb1acff185b4b67569903faf1df36` keeps TS catalogs and compiles them with `qt_add_lrelease`, packaging QM and Qt translations. The Muse framework wraps `QCoreApplication::translate` for C++ and QML-facing calls. [MuseScore locale build](https://github.com/musescore/MuseScore/blob/1c81f0a6f3eeb1acff185b4b67569903faf1df36/share/locale/CMakeLists.txt), [framework translation](https://github.com/musescore/muse_framework/blob/1d8529e782618391bf0187a2e34c85f499b0680e/framework/global/translation.cpp), [QML wrapper](https://github.com/musescore/muse_framework/blob/1d8529e782618391bf0187a2e34c85f499b0680e/framework/ui/view/qmltranslation.cpp).

Audacity master `36146d838c934ae429cb164e8eb3d39af75a65ff` likewise has `share/locale/audacity_*.ts`, builds QM via `qt_add_lrelease`, and an explicitly AU4-labeled lupdate workflow. Its `au3/locale/*.po` still exists; that is legacy subtree evidence, not proof that the new QML UI uses gettext. [AU4 locale build](https://github.com/audacity/audacity/blob/36146d838c934ae429cb164e8eb3d39af75a65ff/share/locale/CMakeLists.txt), [AU4 extraction workflow](https://github.com/audacity/audacity/blob/36146d838c934ae429cb164e8eb3d39af75a65ff/.github/workflows/translate_lupdate.yml).

## Remaining decision

Choose authoritative authoring format, stable context/key policy and transitional Lua/wx domain ownership. The evidence supports TS/QM for new QML with a tested migration, but catalog conversion fidelity and live switching remain implementation acceptance gates. Agent QML/HTML prototypes should expose long-label and language-switch states for user reaction; Figma Starter remains occasional handoff only.
