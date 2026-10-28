#pragma once
// O4: the legacy gettext catalogs (Locale/*.po at 20d647c4) as the migration
// reads them. Only what the migration needs is modelled: the header, each
// message's context, ids, forms, flags and comments. Obsolete (#~) entries are
// kept with their flag so the accounting can name them.

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <expected>
#include <optional>
#include <utility>

namespace hikari::i18n {

struct PoMessage {
    std::optional<QString> msgctxt; // absent and empty differ in gettext
    QString msgid;
    std::optional<QString> msgidPlural;
    QStringList msgstr; // msgstr, or msgstr[0..n-1]
    QStringList flags;  // "#," entries in file order
    QStringList translatorComments; // "# " lines without the marker
    QStringList extractedComments;  // "#." lines
    QStringList references;         // "#:" items
    QStringList previous;           // "#|" lines
    bool obsolete = false;          // "#~" entry
    int line = 0;                   // 1-based line of the msgid keyword

    bool fuzzy() const { return flags.contains(QStringLiteral("fuzzy")); }
    bool plural() const { return msgidPlural.has_value(); }
    // The message's gettext key: context, singular id and plural presence.
    QString keyText() const;
};

struct PoCatalog {
    QStringList headerComments; // the header entry's "#" lines, verbatim
    QList<std::pair<QString, QString>> headerFields; // "Name: value" in order
    QList<PoMessage> messages; // without the header entry

    QString header(const QString &name) const;
};

std::expected<PoCatalog, QString> parsePo(const QByteArray &bytes);

// A gettext C-format directive as msgfmt -c compares them: the conversion
// letter after the flags, width, precision and length modifiers. "%%" is no
// directive.
struct PrintfDirective {
    qsizetype position = 0;
    qsizetype length = 0;
    QChar conversion;
    int argument = 0;       // N of a "%N$" directive, 0 when not positional
    bool starArgs = false;  // "*" width or precision takes an argument too
};
QList<PrintfDirective> printfDirectives(const QString &text);

} // namespace hikari::i18n
