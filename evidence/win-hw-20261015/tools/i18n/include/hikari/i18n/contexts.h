#pragma once
// O6: semantic translation contexts (docs/qt/localisation.md, "Keys and
// migration"). Every interface string is keyed by the surface it belongs to
// (HikariSub.Editor, HikariSub.Grid, ...), named in the registry
// i18n/contexts.tsv, rather than by the QML file or C++ class that happens to
// hold it. The context move from lupdate's default contexts is recorded as a
// reviewable map (i18n/migration/contexts.tsv) that carries the catalogs'
// entries to their new keys without losing or changing a translation.

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

// One row of the move: the old key (lupdate's default context, source,
// disambiguation, numerus) and one of the contexts it moved to. A key whose
// calls now belong to several surfaces moves to each of them (one row each).
struct ContextMove {
    QString from;
    QString source;
    QString comment;
    bool numerus = false;
    QString to;
    // How the row was derived: "extracted" (the same call before and after)
    // or "context" (a catalog entry without a call: vanished or obsolete,
    // moved where most of its old context's keys went).
    QString basis;
};

struct ContextMap {
    QList<ContextMove> moves;
};

// Pairs the calls of two located extractions of the same sources (lupdate
// -locations absolute or relative), before and after the contexts changed:
// within each file, the calls of one source text (with its disambiguation and
// numerus) correspond in line order. Entries of `catalogs` that no call holds
// (vanished, obsolete) move where most keys of their old context went. Fails
// when a file's calls of a text differ in number.
std::expected<ContextMap, QString> buildContextMap(const TsCatalog &before, const TsCatalog &after,
                                                   const QList<TsCatalog> &catalogs);

// Tab-separated, the key map's escaping ("\t", "\n", "\r", "\\").
QByteArray writeContextMap(const ContextMap &map);
std::expected<ContextMap, QString> readContextMap(const QByteArray &bytes);

// The catalog with every entry of an old context at its new key(s), contexts
// in name order and messages in their first appearance order. Entries that
// meet at one key must agree (translations, comments, state), except that a
// finished entry absorbs an unfinished one with the same text; fails naming
// the key otherwise, and for an entry of an old context without a row.
std::expected<TsCatalog, QString> recontext(const TsCatalog &catalog, const ContextMap &map);

// Before and after a move, per language: every entry before is at each of
// its new keys with the same translations and state, and every entry after
// comes from one before (nothing translated added). The one state change
// allowed is recontext's join: an unfinished entry whose key it now shares
// with a finished one of the same text (`promoted`). `problems` names every
// other difference.
struct RecontextComparison {
    int entriesBefore = 0;
    int entriesAfter = 0;
    int translatedBefore = 0; // entries with a non-empty translation
    int translatedAfter = 0;
    int finishedBefore = 0;
    int finishedAfter = 0;
    // Distinct (source, disambiguation, numerus, translations) among the
    // translated entries: the same set on both sides when the move only
    // joined equal entries or copied one to several surfaces.
    int distinctTranslatedBefore = 0;
    int distinctTranslatedAfter = 0;
    int joinedKeys = 0; // new keys that several entries before reach
    QStringList promoted;
    QStringList problems;
};
RecontextComparison compareRecontexted(const TsCatalog &before, const TsCatalog &after, const ContextMap &map);

} // namespace hikari::i18n
