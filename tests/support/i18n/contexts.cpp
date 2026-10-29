#include "hikari/i18n/contexts.h"

#include <QRegularExpression>
#include <QSet>

namespace hikari::i18n {
namespace {

QString describe(const QString &context, const TsMessage &m)
{
    QString text = m.source;
    text.replace(u'\n', QStringLiteral("\\n"));
    return QStringLiteral("%1 \"%2\"%3%4")
        .arg(context, text, m.comment.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(m.comment),
             m.numerus ? QStringLiteral(" [numerus]") : QString());
}

int lineAt(const QString &text, qsizetype offset)
{
    return int(QStringView(text).left(offset).count(u'\n')) + 1;
}

// Blanks C++ comments, and literals' contents when `literals`, keeping line breaks.
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

}

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
                                 describe(context.name, m));
        }
    }

    static const QRegularExpression literalContext(
        QStringLiteral(R"re((?<![\w.])(qsTranslate|qsTranslateNoOp|QT_TRANSLATE_NOOP3?|QT_TRANSLATE_N_NOOP3?|)re"
                       R"re(QT_TRANSLATE_NOOP_UTF8|translate)\s*\(\s*"([^"\\]*)")re"));
    static const QRegularExpression pragma(QStringLiteral(R"re(^\s*pragma\s+Translator\s*:\s*"([^"\\]*)")re"),
                                           QRegularExpression::MultilineOption);
    // Calls whose context is the enclosing class (or QObject) at run time.
    static const QRegularExpression classContext(
        QStringLiteral(R"((?<![\w])(?:\w+::)?(tr|trUtf8|QT_TR_NOOP|QT_TR_N_NOOP|QT_TR_NOOP_UTF8)\s*\()"));
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
        // lupdate extracts nothing from a computed source text: ask for a same-context mark in the file.
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

}
