#pragma once
// O4: the one-shot PO-to-TS migration (docs/qt/localisation.md, "Keys and
// migration"). The key map pairs each legacy gettext key with the rewrite's
// Qt keys: an exact match (same source text, same disambiguation as the
// msgctxt, neither plural) keeps the reviewed translation; a c-format id that
// becomes the rewrite source once its directives are Qt markers is a review
// candidate whose translation is carried unfinished; everything else is new
// (rewrite) or obsolete (legacy). Conversion merges the legacy translations
// through the checked-in map, never by recomputing it.

#include "hikari/i18n/po.h"
#include "hikari/i18n/ts.h"

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>

#include <expected>
#include <optional>

namespace hikari::i18n {

// The context of the aegisub.gettext compatibility entries.
inline constexpr auto gettextContext = "HikariSub.Automation.Gettext";
// The context of the legacy plural probe (not a runtime catalog).
inline constexpr auto pluralProbeContext = "HikariSub.Migration.LegacyPlurals";

enum class MapStatus { Exact, Placeholder, New, Obsolete };

QString statusName(MapStatus status);

struct KeyMapRow {
    MapStatus status = MapStatus::New;
    // Legacy gettext key (unset msgid for New rows).
    std::optional<QString> msgctxt;
    std::optional<QString> msgid;
    std::optional<QString> msgidPlural;
    // Rewrite Qt key (unset context for Obsolete rows).
    std::optional<QString> context;
    QString source;
    QString comment;
    bool numerus = false;
    // Placeholder rows: the printf directives and the Qt markers they become,
    // e.g. "%s %i -> %1 %2".
    QString placeholders;
};

struct KeyMap {
    QString inputCommit;
    QList<KeyMapRow> rows;
};

// Builds the candidate map from the legacy template and the lupdate-extracted
// rewrite keys (any TS; translations, vanished and obsolete entries ignored).
// Rows follow the template's order, one per matching rewrite key (a legacy
// message serving several contexts maps one-to-many) or one Obsolete row, then
// the New rows in the keys' order.
KeyMap buildKeyMap(const PoCatalog &legacy, const TsCatalog &rewriteKeys, const QString &inputCommit);

// The map as a tab-separated file: "\t", "\n", "\r" and "\\" escaped, "\N" for
// an absent field.
QByteArray writeKeyMap(const KeyMap &map);
std::expected<KeyMap, QString> readKeyMap(const QByteArray &bytes);

// A c-format text with its directives as Qt markers: "%N$" directives become
// %N, the others %1..%n in order, "%%" becomes "%". nullopt when it mixes
// positional and sequential directives, takes "*" arguments, or would leave a
// "%<digit>" QString::arg would read as a marker.
struct QtFormat {
    QString text;
    QMap<int, QChar> arguments; // marker number -> printf conversion
};
std::optional<QtFormat> printfToQt(const QString &text);

struct LanguageInput {
    QString language;  // TS language attribute (the PO file's stem)
    QString fileName;  // e.g. "pl.po", for the report and the catalog extras
    QByteArray bytes;
};

struct MigrationOutput {
    QMap<QString, QByteArray> uiCatalogs;      // language -> hikarisub_<language>.ts
    QMap<QString, QByteArray> gettextCatalogs; // language -> hikarisub_gettext_<language>.ts
    QByteArray report;                         // Markdown accounting
};

// Fails when the map no longer matches the template or the keys (a stale
// row, or a key without a row): the map is regenerated and reviewed first.
std::expected<MigrationOutput, QString> migrate(const KeyMap &map, const TsCatalog &rewriteKeys,
                                                const QByteArray &templatePot,
                                                const QList<LanguageInput> &languages);

// The legacy plural entries of one catalog as Qt numerus messages ("%d" as
// "%n", msgctxt as the disambiguation) in pluralProbeContext, for checking
// Qt's numerus rules against the legacy Plural-Forms. A template (.pot) gives
// the English catalog: msgid and msgid_plural as its two forms, as wx falls
// back to them without a catalog. Not a runtime catalog.
std::expected<QByteArray, QString> legacyPluralProbe(const LanguageInput &language);

} // namespace hikari::i18n
