#include "hikari/i18n/po.h"

#include <QRegularExpression>

namespace hikari::i18n {
namespace {

// A C string literal's contents as gettext writes them. Escapes are bytes
// ("\305\202" is one UTF-8 character), so the contents are decoded last.
std::expected<QString, QString> unquote(QStringView text)
{
    text = text.trimmed();
    if (text.size() < 2 || text.front() != u'"' || text.back() != u'"')
        return std::unexpected(QStringLiteral("expected a quoted string"));
    text = text.mid(1, text.size() - 2);
    QByteArray out;
    out.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (c != u'\\') {
            if (c.isHighSurrogate() && i + 1 < text.size()) {
                out += text.mid(i, 2).toUtf8();
                ++i;
            } else {
                out += QStringView(&c, 1).toUtf8();
            }
            continue;
        }
        if (++i == text.size())
            return std::unexpected(QStringLiteral("dangling backslash"));
        switch (text[i].unicode()) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case 'a': out += '\a'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'v': out += '\v'; break;
        case '"': out += '"'; break;
        case '\'': out += '\''; break;
        case '?': out += '?'; break;
        case '\\': out += '\\'; break;
        case 'x': {
            qsizetype end = i + 1;
            while (end < text.size() && isxdigit(text[end].unicode()))
                ++end;
            if (end == i + 1)
                return std::unexpected(QStringLiteral("\\x without digits"));
            out += char(text.mid(i + 1, end - i - 1).toUInt(nullptr, 16));
            i = end - 1;
            break;
        }
        default:
            if (text[i] >= u'0' && text[i] <= u'7') {
                qsizetype end = i;
                while (end < text.size() && end < i + 3 && text[end] >= u'0' && text[end] <= u'7')
                    ++end;
                out += char(text.mid(i, end - i).toUInt(nullptr, 8));
                i = end - 1;
                break;
            }
            return std::unexpected(QStringLiteral("unknown escape \\%1").arg(text[i]));
        }
    }
    return QString::fromUtf8(out);
}

struct Builder {
    PoMessage message;
    bool any = false;      // a keyword or comment was read
    bool hasMsgid = false;
    bool hasMsgstr = false;
    QString *continued = nullptr;
};

} // namespace

QString PoMessage::keyText() const
{
    QString text;
    if (msgctxt)
        text += QStringLiteral("msgctxt \"%1\" ").arg(*msgctxt);
    text += QStringLiteral("msgid \"%1\"").arg(msgid);
    if (msgidPlural)
        text += QStringLiteral(" msgid_plural \"%1\"").arg(*msgidPlural);
    return text;
}

QString PoCatalog::header(const QString &name) const
{
    for (const auto &[key, value] : headerFields)
        if (key.compare(name, Qt::CaseInsensitive) == 0)
            return value;
    return {};
}

std::expected<PoCatalog, QString> parsePo(const QByteArray &bytes)
{
    PoCatalog catalog;
    Builder current;
    bool headerSeen = false;
    QString failure;

    const auto flush = [&]() -> bool {
        if (!current.any)
            return true;
        if (!current.hasMsgid || !current.hasMsgstr) {
            failure = QStringLiteral("entry without msgid or msgstr");
            return false;
        }
        PoMessage &m = current.message;
        if (!headerSeen && !m.obsolete && !m.msgctxt && m.msgid.isEmpty()) {
            headerSeen = true;
            catalog.headerComments = m.translatorComments;
            const QStringList lines = m.msgstr.value(0).split(u'\n', Qt::SkipEmptyParts);
            for (const QString &line : lines) {
                const qsizetype colon = line.indexOf(u':');
                if (colon < 0) {
                    failure = QStringLiteral("header line without a colon: %1").arg(line);
                    return false;
                }
                catalog.headerFields.append({line.left(colon).trimmed(), line.mid(colon + 1).trimmed()});
            }
        } else {
            catalog.messages.append(m);
        }
        current = Builder{};
        return true;
    };

    const QList<QByteArray> lines = bytes.split('\n');
    for (qsizetype n = 0; n < lines.size(); ++n) {
        QString line = QString::fromUtf8(lines[n]);
        if (line.endsWith(u'\r'))
            line.chop(1);
        const auto fail = [&](const QString &why) {
            return std::unexpected(QStringLiteral("line %1: %2").arg(n + 1).arg(why));
        };
        if (line.trimmed().isEmpty()) {
            if (!flush())
                return fail(failure);
            continue;
        }
        bool obsolete = false;
        if (line.startsWith(QStringLiteral("#~"))) {
            obsolete = true;
            line = line.mid(2).trimmed();
        }
        if (!obsolete && line.startsWith(u'#')) {
            // A comment after the strings starts the next entry.
            if (current.hasMsgstr && !flush())
                return fail(failure);
            current.any = true;
            current.continued = nullptr;
            if (line.startsWith(QStringLiteral("#,"))) {
                for (const QString &flag : line.mid(2).split(u',', Qt::SkipEmptyParts))
                    current.message.flags.append(flag.trimmed());
            } else if (line.startsWith(QStringLiteral("#:"))) {
                current.message.references += line.mid(2).split(u' ', Qt::SkipEmptyParts);
            } else if (line.startsWith(QStringLiteral("#."))) {
                current.message.extractedComments.append(line.mid(2).trimmed());
            } else if (line.startsWith(QStringLiteral("#|"))) {
                current.message.previous.append(line.mid(2).trimmed());
            } else {
                current.message.translatorComments.append(line);
            }
            continue;
        }
        if (line.startsWith(u'"')) {
            if (!current.continued)
                return fail(QStringLiteral("string continuation without a keyword"));
            const auto part = unquote(line);
            if (!part)
                return fail(part.error());
            *current.continued += *part;
            continue;
        }
        const qsizetype space = line.indexOf(u' ');
        if (space < 0)
            return fail(QStringLiteral("unexpected line"));
        const QString keyword = line.left(space);
        const auto value = unquote(QStringView(line).mid(space + 1));
        if (!value)
            return fail(value.error());
        if ((keyword == u"msgctxt" || keyword == u"msgid") && current.hasMsgstr && !flush())
            return fail(failure);
        current.any = true;
        current.message.obsolete = current.message.obsolete || obsolete;
        PoMessage &m = current.message;
        static const QRegularExpression indexed(QStringLiteral("^msgstr\\[(\\d+)\\]$"));
        if (keyword == u"msgctxt") {
            m.msgctxt = *value;
            current.continued = &*m.msgctxt;
        } else if (keyword == u"msgid") {
            m.msgid = *value;
            m.line = int(n + 1);
            current.hasMsgid = true;
            current.continued = &m.msgid;
        } else if (keyword == u"msgid_plural") {
            m.msgidPlural = *value;
            current.continued = &*m.msgidPlural;
        } else if (keyword == u"msgstr") {
            m.msgstr = {*value};
            current.hasMsgstr = true;
            current.continued = &m.msgstr[0];
        } else if (const auto match = indexed.match(keyword); match.hasMatch()) {
            const qsizetype index = match.captured(1).toLongLong();
            if (index != m.msgstr.size())
                return fail(QStringLiteral("msgstr[%1] out of order").arg(index));
            m.msgstr.append(*value);
            current.hasMsgstr = true;
            current.continued = &m.msgstr.last();
        } else {
            return fail(QStringLiteral("unknown keyword %1").arg(keyword));
        }
    }
    if (!flush())
        return std::unexpected(failure);
    return catalog;
}

QList<PrintfDirective> printfDirectives(const QString &text)
{
    // flags, width, precision, length modifier, conversion (gettext's c-format).
    static const QRegularExpression directive(QStringLiteral(
        "%(?:(\\d+)\\$)?[-+ #0'I]*(\\d+|\\*)?(?:\\.(\\d+|\\*))?(?:hh|h|ll|l|L|q|j|z|Z|t|I64|I32)?([diouxXeEfFgGaAcCsSpn%])"));
    QList<PrintfDirective> out;
    auto it = directive.globalMatch(text);
    while (it.hasNext()) {
        const auto match = it.next();
        if (match.captured(4) == u"%")
            continue;
        out.append({match.capturedStart(), match.capturedLength(), match.captured(4).front(),
                    match.captured(1).toInt(), match.captured(2) == u"*" || match.captured(3) == u"*"});
    }
    return out;
}

} // namespace hikari::i18n
