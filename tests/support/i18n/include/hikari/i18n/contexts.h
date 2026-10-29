#pragma once
// O6: semantic translation contexts (docs/localisation.md, "Keys"). Every
// interface string is keyed by the surface it belongs to (HikariSub.Editor,
// HikariSub.Grid, ...), named in the registry i18n/contexts.tsv, rather than
// by the QML file or C++ class that happens to hold it.

#include "hikari/i18n/ts.h"

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <expected>

namespace hikari::i18n {

// The registry: context name -> the surface it covers.
using ContextRegistry = QMap<QString, QString>;
std::expected<ContextRegistry, QString> readContextRegistry(const QByteArray &bytes);

// A source file the lint reads: its path (as reported) and text.
struct SourceFile {
    QString path;
    QString text;
};

// The lint: every context the extraction (a TS from lupdate, with locations
// when it was run with -locations) gives a message is registered; every
// literal context a source names (qsTranslate, QT_TRANSLATE_NOOP*,
// translate, pragma Translator) is registered, also one only used at run time
// with a computed source text; a file that looks up a computed text
// (translate or qsTranslate with a non-literal source) marks texts for that
// context too (QT_TRANSLATE_NOOP*, qsTranslateNoOp), since lupdate extracts
// nothing from the lookup; and no C++ source uses tr(), QT_TR_NOOP or
// QT_TR_N_NOOP, whose context is the enclosing class at run time. Returns one
// line per problem, naming the file (and line) and the context.
QStringList lintContexts(const ContextRegistry &registry, const TsCatalog &extracted, const QList<SourceFile> &sources);

} // namespace hikari::i18n
