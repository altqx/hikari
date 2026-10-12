#include "hikari/i18n/contexts.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <optional>
#include <tuple>

namespace hikari::i18n {
namespace {

QString escape(const QString &value)
{
    QString out;
    out.reserve(value.size());
    for (const QChar c : value) {
        switch (c.unicode()) {
        case u'\\': out += QStringLiteral("\\\\"); break;
        case u'\t': out += QStringLiteral("\\t"); break;
        case u'\n': out += QStringLiteral("\\n"); break;
        case u'\r': out += QStringLiteral("\\r"); break;
        default: out += c;
        }
    }
    return out;
}

std::expected<QString, QString> unescape(const QString &field)
{
    QString out;
    out.reserve(field.size());
    for (qsizetype i = 0; i < field.size(); ++i) {
        if (field[i] != u'\\') {
            out += field[i];
            continue;
        }
        if (++i == field.size())
            return std::unexpected(QStringLiteral("dangling backslash"));
        switch (field[i].unicode()) {
        case u'\\': out += u'\\'; break;
        case u't': out += u'\t'; break;
        case u'n': out += u'\n'; break;
        case u'r': out += u'\r'; break;
        default: return std::unexpected(QStringLiteral("unknown escape \\%1").arg(field[i]));
        }
    }
    return out;
}

// A message's key within its context.
struct Key {
    QString source;
    QString comment;
    bool numerus = false;
    friend bool operator==(const Key &, const Key &) = default;
};
size_t qHash(const Key &key, size_t seed = 0)
{
    return qHashMulti(seed, key.source, key.comment, key.numerus);
}

struct FullKey {
    QString context;
    Key key;
    friend bool operator==(const FullKey &, const FullKey &) = default;
};
size_t qHash(const FullKey &key, size_t seed = 0)
{
    return qHashMulti(seed, key.context, key.key);
}

Key keyOf(const TsMessage &m)
{
    return {m.source, m.comment, m.numerus};
}

QString describe(const QString &context, const Key &key)
{
    QString text = key.source;
    text.replace(u'\n', QStringLiteral("\\n"));
    return QStringLiteral("%1 \"%2\"%3%4")
        .arg(context, text, key.comment.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(key.comment),
             key.numerus ? QStringLiteral(" [numerus]") : QString());
}

bool hasText(const TsMessage &m)
{
    return std::ranges::any_of(m.translations, [](const QString &t) { return !t.isEmpty(); });
}

// The 1-based line of a text offset.
int lineAt(const QString &text, qsizetype offset)
{
    return int(QStringView(text).left(offset).count(u'\n')) + 1;
}

// The text with C++ comments blanked (line breaks kept), and string and
// character literals' contents too when `literals`, so a comment or a string
// naming tr() is no call.
QString withoutCppComments(const QString &text, bool literals = false)
{
    QString out = text;
    enum { Code, Line, Block, String, Char } state = Code;
    for (qsizetype i = 0; i < out.size(); ++i) {
        const QChar c = out[i];
        const QChar next = i + 1 < out.size() ? out[i + 1] : QChar();
        switch (state) {
        case Code:
            if (c == u'/' && next == u'/') {
                state = Line;
                out[i] = u' ';
            } else if (c == u'/' && next == u'*') {
                state = Block;
                out[i] = u' ';
            } else if (c == u'"' && i > 0 && out[i - 1] == u'R' &&
                       (i < 2 || !(out[i - 2].isLetterOrNumber() || out[i - 2] == u'_') || out[i - 2] == u'8' ||
                        out[i - 2] == u'u' || out[i - 2] == u'U' || out[i - 2] == u'L')) {
                // A raw string: R"delimiter( ... )delimiter".
                const qsizetype open = out.indexOf(u'(', i);
                const QString close = u')' + out.mid(i + 1, open - i - 1) + u'"';
                const qsizetype end = open < 0 ? -1 : out.indexOf(close, open);
                const qsizetype last = end < 0 ? out.size() - 1 : end + close.size() - 1;
                if (literals)
                    for (qsizetype k = open + 1; open >= 0 && k < (end < 0 ? out.size() : end); ++k)
                        if (out[k] != u'\n')
                            out[k] = u' ';
                i = last;
            } else if (c == u'"') {
                state = String;
            } else if (c == u'\'') {
                // A digit separator (1'000'000) is no character literal.
                qsizetype start = i;
                while (start > 0 && (out[start - 1].isLetterOrNumber() || out[start - 1] == u'\''))
                    --start;
                if (start == i || !out[start].isDigit())
                    state = Char;
            }
            break;
        case Line:
            if (c == u'\n')
                state = Code;
            else
                out[i] = u' ';
            break;
        case Block:
            if (c == u'*' && next == u'/') {
                out[i] = out[i + 1] = u' ';
                ++i;
                state = Code;
            } else if (c != u'\n') {
                out[i] = u' ';
            }
            break;
        case String:
        case Char:
            if (c == u'\\') {
                if (literals) {
                    out[i] = u' ';
                    if (i + 1 < out.size() && out[i + 1] != u'\n')
                        out[i + 1] = u' ';
                }
                ++i;
            } else if (c == (state == String ? u'"' : u'\'')) {
                state = Code;
            } else if (literals && c != u'\n') {
                out[i] = u' ';
            }
            break;
        }
    }
    return out;
}

} // namespace

std::expected<ContextRegistry, QString> readContextRegistry(const QByteArray &bytes)
{
    ContextRegistry registry;
    const QStringList lines = QString::fromUtf8(bytes).split(u'\n');
    bool header = false;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const QString &line = lines[i];
        if (line.isEmpty() || line.startsWith(u'#'))
            continue;
        const QStringList fields = line.split(u'\t');
        if (!header) {
            if (fields != QStringList{QStringLiteral("context"), QStringLiteral("surface")})
                return std::unexpected(QStringLiteral("line %1: expected the header \"context\\tsurface\"").arg(i + 1));
            header = true;
            continue;
        }
        if (fields.size() != 2 || fields[0].isEmpty() || fields[1].isEmpty())
            return std::unexpected(QStringLiteral("line %1: expected a context and its surface").arg(i + 1));
        if (registry.contains(fields[0]))
            return std::unexpected(QStringLiteral("line %1: %2 registered twice").arg(i + 1).arg(fields[0]));
        registry.insert(fields[0], fields[1]);
    }
    if (registry.isEmpty())
        return std::unexpected(QStringLiteral("no contexts"));
    return registry;
}

QStringList lintContexts(const ContextRegistry &registry, const TsCatalog &extracted, const QList<SourceFile> &sources)
{
    QStringList problems;
    for (const TsContext &context : extracted.contexts) {
        if (registry.contains(context.name))
            continue;
        for (const TsMessage &m : context.messages) {
            if (m.state == TsState::Vanished || m.state == TsState::Obsolete)
                continue;
            QStringList where;
            for (const auto &[file, line] : m.locations)
                where << QStringLiteral("%1:%2").arg(file).arg(line);
            problems << QStringLiteral("%1: %2 has no semantic context")
                            .arg(where.isEmpty() ? QStringLiteral("(no location)") : where.join(QStringLiteral(", ")),
                                 describe(context.name, keyOf(m)));
        }
    }

    // A literal context: the first argument of the calls that take one, and
    // QML's per-file pragma.
    static const QRegularExpression literalContext(
        QStringLiteral(R"re((?<![\w.])(qsTranslate|qsTranslateNoOp|QT_TRANSLATE_NOOP3?|QT_TRANSLATE_N_NOOP3?|)re"
                       R"re(QT_TRANSLATE_NOOP_UTF8|translate)\s*\(\s*"([^"\\]*)")re"));
    static const QRegularExpression pragma(QStringLiteral(R"re(^\s*pragma\s+Translator\s*:\s*"([^"\\]*)")re"),
                                           QRegularExpression::MultilineOption);
    // Calls whose context is the enclosing class (or QObject) at run time.
    static const QRegularExpression classContext(
        QStringLiteral(R"((?<![\w])(?:\w+::)?(tr|trUtf8|QT_TR_NOOP|QT_TR_N_NOOP|QT_TR_NOOP_UTF8)\s*\()"));
    // A run-time lookup and the marks that put its possible texts in the
    // catalogs (the marking calls of literalContext).
    static const QRegularExpression lookup(
        QStringLiteral(R"re((?<![\w.])(qsTranslate|translate)\s*\(\s*"([^"\\]*)"\s*,\s*)re"));
    static const QRegularExpression mark(
        QStringLiteral(R"re((?<![\w.])(?:qsTranslateNoOp|QT_TRANSLATE_NOOP3?|QT_TRANSLATE_N_NOOP3?|)re"
                       R"re(QT_TRANSLATE_NOOP_UTF8)\s*\(\s*"([^"\\]*)")re"));
    for (const SourceFile &file : sources) {
        const bool cpp = !file.path.endsWith(QStringLiteral(".qml")) && !file.path.endsWith(QStringLiteral(".js")) &&
                         !file.path.endsWith(QStringLiteral(".mjs"));
        const QString text = cpp ? withoutCppComments(file.text) : file.text;
        for (auto it = literalContext.globalMatch(text); it.hasNext();) {
            const auto match = it.next();
            if (!registry.contains(match.captured(2)))
                problems << QStringLiteral("%1:%2: %3 names the unregistered context \"%4\"")
                                .arg(file.path).arg(lineAt(text, match.capturedStart())).arg(match.captured(1), match.captured(2));
        }
        // A computed source text: lupdate extracts nothing from the call, so
        // its texts reach the catalogs only through marks with the same
        // context. The lint cannot follow the text to its marks; it asks for
        // at least one in the same file (the table it is looked up in).
        const QString uncommented = cpp ? text : withoutCppComments(file.text);
        QSet<QString> marked;
        for (auto it = mark.globalMatch(uncommented); it.hasNext();)
            marked.insert(it.next().captured(1));
        for (auto it = lookup.globalMatch(uncommented); it.hasNext();) {
            const auto match = it.next();
            if (uncommented.mid(match.capturedEnd(), 1) == u"\"" || marked.contains(match.captured(2)))
                continue;
            problems << QStringLiteral("%1:%2: %3 looks up a computed text in \"%4\", which lupdate cannot extract; "
                                       "mark its possible texts in this file (QT_TRANSLATE_NOOP or qsTranslateNoOp "
                                       "with \"%4\")")
                            .arg(file.path).arg(lineAt(uncommented, match.capturedStart())).arg(match.captured(1), match.captured(2));
        }
        if (!cpp) {
            for (auto it = pragma.globalMatch(text); it.hasNext();) {
                const auto match = it.next();
                if (!registry.contains(match.captured(1)))
                    problems << QStringLiteral("%1:%2: pragma Translator names the unregistered context \"%3\"")
                                    .arg(file.path).arg(lineAt(text, match.capturedStart())).arg(match.captured(1));
            }
            continue;
        }
        const QString code = withoutCppComments(file.text, true);
        for (auto it = classContext.globalMatch(code); it.hasNext();) {
            const auto match = it.next();
            problems << QStringLiteral("%1:%2: %3() takes the class's context; name a semantic one "
                                       "(QCoreApplication::translate, QT_TRANSLATE_NOOP)")
                            .arg(file.path).arg(lineAt(code, match.capturedStart())).arg(match.captured(1));
        }
    }
    return problems;
}

std::expected<ContextMap, QString> buildContextMap(const TsCatalog &before, const TsCatalog &after,
                                                   const QList<TsCatalog> &catalogs)
{
    // (file, key) -> the calls' lines and contexts.
    struct Occurrences {
        QList<std::pair<int, QString>> before, after;
    };
    QHash<std::pair<QString, Key>, Occurrences> calls;
    QList<std::pair<QString, Key>> order;
    const auto collect = [&](const TsCatalog &catalog, bool isBefore) {
        for (const TsContext &context : catalog.contexts)
            for (const TsMessage &m : context.messages)
                for (const auto &[file, line] : m.locations) {
                    const auto id = std::pair{file, keyOf(m)};
                    auto &o = calls[id];
                    if (o.before.isEmpty() && o.after.isEmpty())
                        order.append(id);
                    (isBefore ? o.before : o.after).append({line, context.name});
                }
    };
    collect(before, true);
    collect(after, false);

    ContextMap map;
    QSet<QStringList> seen;
    QHash<QString, QMap<QString, int>> targets; // old context -> new context -> keys
    QSet<FullKey> moved;
    for (const auto &id : order) {
        Occurrences o = calls.value(id);
        if (o.before.size() != o.after.size())
            return std::unexpected(QStringLiteral("%1: %2 has %3 call(s) before and %4 after")
                                       .arg(id.first, describe(QString(), id.second).trimmed())
                                       .arg(o.before.size())
                                       .arg(o.after.size()));
        std::ranges::sort(o.before);
        std::ranges::sort(o.after);
        for (qsizetype i = 0; i < o.before.size(); ++i) {
            const QString &from = o.before[i].second;
            const QString &to = o.after[i].second;
            const Key &key = id.second;
            const QStringList row{from, key.source, key.comment, key.numerus ? QStringLiteral("yes") : QString(), to};
            if (seen.contains(row))
                continue;
            seen.insert(row);
            map.moves.append({from, key.source, key.comment, key.numerus, to, QStringLiteral("extracted")});
            moved.insert({from, key});
            ++targets[from][to];
        }
    }
    // Catalog entries no call holds any more go where their context went.
    for (const TsCatalog &catalog : catalogs)
        for (const TsContext &context : catalog.contexts)
            for (const TsMessage &m : context.messages) {
                const FullKey full{context.name, keyOf(m)};
                if (moved.contains(full) || !targets.contains(context.name))
                    continue;
                const QMap<QString, int> &counts = targets.value(context.name);
                QString to = counts.firstKey();
                for (auto it = counts.cbegin(); it != counts.cend(); ++it)
                    if (it.value() > counts.value(to))
                        to = it.key();
                map.moves.append({context.name, m.source, m.comment, m.numerus, to, QStringLiteral("context")});
                moved.insert(full);
            }
    std::ranges::stable_sort(map.moves, [](const ContextMove &a, const ContextMove &b) {
        return std::tie(a.from, a.to) < std::tie(b.from, b.to);
    });
    return map;
}

QByteArray writeContextMap(const ContextMap &map)
{
    QString out = QStringLiteral(
        "# O6: the interface strings' move from lupdate's default contexts (QML file and C++ class names) to the\n"
        "# semantic contexts of i18n/contexts.tsv. Reviewed record of hikari_i18n_migrate contextmap; the catalogs\n"
        "# were moved with hikari_i18n_migrate recontext.\n"
        "# basis: extracted = the same call before and after; context = a catalog entry without a call (vanished or\n"
        "# obsolete), moved where most keys of its old context went.\n"
        "from\tsource\tcomment\tnumerus\tto\tbasis\n");
    for (const ContextMove &m : map.moves)
        out += QStringList{escape(m.from), escape(m.source), escape(m.comment),
                           m.numerus ? QStringLiteral("yes") : QStringLiteral("no"), escape(m.to), m.basis}
                   .join(u'\t') +
               u'\n';
    return out.toUtf8();
}

std::expected<ContextMap, QString> readContextMap(const QByteArray &bytes)
{
    ContextMap map;
    const QStringList lines = QString::fromUtf8(bytes).split(u'\n');
    bool header = false;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const QString &line = lines[i];
        if (line.isEmpty() || line.startsWith(u'#'))
            continue;
        const QStringList fields = line.split(u'\t');
        if (!header) {
            if (fields.join(u' ') != QStringLiteral("from source comment numerus to basis"))
                return std::unexpected(QStringLiteral("line %1: expected the header").arg(i + 1));
            header = true;
            continue;
        }
        if (fields.size() != 6)
            return std::unexpected(QStringLiteral("line %1: expected 6 fields, got %2").arg(i + 1).arg(fields.size()));
        ContextMove move;
        for (int f : {0, 1, 2, 4}) {
            auto value = unescape(fields[f]);
            if (!value)
                return std::unexpected(QStringLiteral("line %1: %2").arg(i + 1).arg(value.error()));
            (f == 0 ? move.from : f == 1 ? move.source : f == 2 ? move.comment : move.to) = *value;
        }
        if (fields[3] != u"yes" && fields[3] != u"no")
            return std::unexpected(QStringLiteral("line %1: numerus is yes or no").arg(i + 1));
        move.numerus = fields[3] == u"yes";
        if (fields[5] != u"extracted" && fields[5] != u"context")
            return std::unexpected(QStringLiteral("line %1: unknown basis %2").arg(i + 1).arg(fields[5]));
        move.basis = fields[5];
        if (move.from.isEmpty() || move.source.isEmpty() || move.to.isEmpty())
            return std::unexpected(QStringLiteral("line %1: empty context or source").arg(i + 1));
        map.moves.append(move);
    }
    if (!header)
        return std::unexpected(QStringLiteral("no header"));
    return map;
}

namespace {

// Old key -> new contexts.
QHash<FullKey, QStringList> movesByKey(const ContextMap &map)
{
    QHash<FullKey, QStringList> out;
    for (const ContextMove &m : map.moves)
        out[{m.from, {m.source, m.comment, m.numerus}}].append(m.to);
    return out;
}

// Entries that meet at one key: the same translations (and comments). A
// finished entry absorbs an unfinished one with the same text (lupdate's
// same-text suggestion on a later key of the same meaning): one key has one
// state, and the reviewed translation is never demoted.
std::optional<TsMessage> joined(const TsMessage &a, const TsMessage &b)
{
    if (a.translations != b.translations || a.extraComment != b.extraComment ||
        a.translatorComment != b.translatorComment || a.extras != b.extras)
        return std::nullopt;
    if (a.state == b.state)
        return a;
    const auto finishedOf = [](const TsMessage &x, const TsMessage &y) -> std::optional<TsMessage> {
        if (x.state == TsState::Finished && y.state == TsState::Unfinished)
            return x;
        return std::nullopt;
    };
    if (auto j = finishedOf(a, b))
        return j;
    return finishedOf(b, a);
}

} // namespace

std::expected<TsCatalog, QString> recontext(const TsCatalog &catalog, const ContextMap &map)
{
    const auto moves = movesByKey(map);
    QSet<QString> oldContexts;
    for (const ContextMove &m : map.moves)
        oldContexts.insert(m.from);

    QMap<QString, QList<TsMessage>> contexts; // name order
    QHash<FullKey, std::pair<QString, qsizetype>> placed;
    const auto place = [&](const QString &context, const TsMessage &m) -> std::expected<void, QString> {
        const FullKey full{context, keyOf(m)};
        if (const auto it = placed.constFind(full); it != placed.cend()) {
            TsMessage &there = contexts[it->first][it->second];
            const auto both = joined(there, m);
            if (!both)
                return std::unexpected(QStringLiteral("%1: entries that meet there differ").arg(describe(context, keyOf(m))));
            there = *both;
            return {};
        }
        auto &messages = contexts[context];
        placed.insert(full, {context, messages.size()});
        messages.append(m);
        return {};
    };
    for (const TsContext &context : catalog.contexts)
        for (const TsMessage &m : context.messages) {
            const auto to = moves.constFind({context.name, keyOf(m)});
            if (to == moves.cend()) {
                if (oldContexts.contains(context.name))
                    return std::unexpected(QStringLiteral("%1: no row in the map").arg(describe(context.name, keyOf(m))));
                if (auto placedHere = place(context.name, m); !placedHere)
                    return std::unexpected(placedHere.error());
                continue;
            }
            for (const QString &target : *to)
                if (auto placedThere = place(target, m); !placedThere)
                    return std::unexpected(placedThere.error());
        }
    TsCatalog out = catalog;
    out.contexts.clear();
    for (auto it = contexts.cbegin(); it != contexts.cend(); ++it)
        out.contexts.append({it.key(), it.value()});
    return out;
}

RecontextComparison compareRecontexted(const TsCatalog &before, const TsCatalog &after, const ContextMap &map)
{
    RecontextComparison result;
    const auto moves = movesByKey(map);
    QHash<FullKey, const TsMessage *> afterEntries;
    for (const TsContext &context : after.contexts)
        for (const TsMessage &m : context.messages)
            afterEntries.insert({context.name, keyOf(m)}, &m);
    // The translated texts, whatever their state or context.
    const auto text = [](const TsMessage &m) {
        return QStringList{m.source, m.comment, m.numerus ? QStringLiteral("yes") : QString()} + m.translations;
    };
    QSet<QStringList> textsBefore, textsAfter;
    // Each new key and the entries before that reach it.
    QHash<FullKey, QList<std::pair<QString, const TsMessage *>>> reached;
    for (const TsContext &context : before.contexts)
        for (const TsMessage &m : context.messages) {
            ++result.entriesBefore;
            if (hasText(m)) {
                ++result.translatedBefore;
                textsBefore.insert(text(m));
            }
            if (m.state == TsState::Finished)
                ++result.finishedBefore;
            const auto to = moves.constFind({context.name, keyOf(m)});
            for (const QString &target : to == moves.cend() ? QStringList{context.name} : *to)
                reached[{target, keyOf(m)}].append({context.name, &m});
        }
    for (auto it = reached.cbegin(); it != reached.cend(); ++it) {
        const FullKey &key = it.key();
        const TsMessage *now = afterEntries.value(key);
        if (it.value().size() > 1)
            ++result.joinedKeys;
        const bool finishedThere = std::ranges::any_of(it.value(), [](const auto &e) { return e.second->state == TsState::Finished; });
        for (const auto &[from, m] : it.value()) {
            if (!now) {
                result.problems << QStringLiteral("lost: %1 (was %2)").arg(describe(key.context, key.key), from);
            } else if (now->translations != m->translations) {
                result.problems << QStringLiteral("changed: %1 (was %2): \"%3\" -> \"%4\"")
                                       .arg(describe(key.context, key.key), from, m->translations.join(u'|'),
                                            now->translations.join(u'|'));
            } else if (now->state != m->state) {
                if (m->state == TsState::Unfinished && now->state == TsState::Finished && finishedThere)
                    result.promoted << QStringLiteral("%1 (was %2)").arg(describe(key.context, key.key), from);
                else
                    result.problems << QStringLiteral("state changed: %1 (was %2)").arg(describe(key.context, key.key), from);
            }
        }
    }
    for (const TsContext &context : after.contexts)
        for (const TsMessage &m : context.messages) {
            ++result.entriesAfter;
            if (hasText(m)) {
                ++result.translatedAfter;
                textsAfter.insert(text(m));
            }
            if (m.state == TsState::Finished)
                ++result.finishedAfter;
            if (!reached.contains({context.name, keyOf(m)}))
                result.problems << QStringLiteral("added: %1").arg(describe(context.name, keyOf(m)));
        }
    result.distinctTranslatedBefore = int(textsBefore.size());
    result.distinctTranslatedAfter = int(textsAfter.size());
    if (textsBefore != textsAfter)
        result.problems << QStringLiteral("the distinct translated texts differ");
    result.promoted.sort();
    return result;
}

} // namespace hikari::i18n
