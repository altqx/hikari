#include "hikari/i18n/ts.h"

#include <QXmlStreamReader>

namespace hikari::i18n {
namespace {

// translator/ts.cpp protect() and numericEntity(): markup characters as
// entities, control characters as <byte/>, non-ASCII spaces as character
// references; line breaks and tabs stay literal.
QString protect(const QString &text)
{
    QString out;
    out.reserve(text.size() * 12 / 10);
    for (const QChar ch : text) {
        const char16_t c = ch.unicode();
        switch (c) {
        case u'"': out += QStringLiteral("&quot;"); break;
        case u'&': out += QStringLiteral("&amp;"); break;
        case u'>': out += QStringLiteral("&gt;"); break;
        case u'<': out += QStringLiteral("&lt;"); break;
        case u'\'': out += QStringLiteral("&apos;"); break;
        default:
            if ((c < 0x20 || (c > 0x7f && ch.isSpace())) && c != u'\n' && c != u'\t')
                out += (c <= 0x20 ? QStringLiteral("<byte value=\"x%1\"/>") : QStringLiteral("&#x%1;"))
                           .arg(uint(c), 0, 16);
            else
                out += ch;
        }
    }
    return out;
}

QString stateAttribute(TsState state)
{
    switch (state) {
    case TsState::Finished: return {};
    case TsState::Unfinished: return QStringLiteral(" type=\"unfinished\"");
    case TsState::Vanished: return QStringLiteral(" type=\"vanished\"");
    case TsState::Obsolete: return QStringLiteral(" type=\"obsolete\"");
    }
    return {};
}

// writeExtras() sorts the written elements, not their names: "<extra-a_10>"
// comes before "<extra-a_1>".
void writeExtras(QString &out, const QList<std::pair<QString, QString>> &extras, const QString &indent)
{
    QStringList lines;
    for (const auto &[name, value] : extras)
        lines.append(QStringLiteral("<extra-%1>%2</extra-%1>").arg(name, protect(value)));
    lines.sort();
    for (const QString &line : lines)
        out += indent + line + u'\n';
}

// Element text with TS's <byte value="xN"/> escapes.
std::expected<QString, QString> readText(QXmlStreamReader &xml)
{
    QString text;
    const QString name = xml.name().toString();
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isCharacters()) {
            text += xml.text();
        } else if (xml.isStartElement() && xml.name() == u"byte") {
            QString value = xml.attributes().value(u"value").toString();
            bool ok = false;
            const uint code = value.startsWith(u'x') ? value.mid(1).toUInt(&ok, 16) : value.toUInt(&ok, 10);
            if (!ok)
                return std::unexpected(QStringLiteral("bad <byte> value %1").arg(value));
            text += QChar(char16_t(code));
            xml.skipCurrentElement();
        } else if (xml.isStartElement()) {
            return std::unexpected(QStringLiteral("unexpected <%1> in <%2>").arg(xml.name(), name));
        } else if (xml.isEndElement()) {
            return text;
        }
    }
    return std::unexpected(QStringLiteral("unterminated <%1>").arg(name));
}

TsState parseState(QStringView type)
{
    if (type == u"unfinished")
        return TsState::Unfinished;
    if (type == u"vanished")
        return TsState::Vanished;
    if (type == u"obsolete")
        return TsState::Obsolete;
    return TsState::Finished;
}

} // namespace

const TsMessage *TsCatalog::find(const QString &context, const QString &source, const QString &comment) const
{
    for (const TsContext &c : contexts) {
        if (c.name != context)
            continue;
        for (const TsMessage &m : c.messages)
            if (m.source == source && m.comment == comment)
                return &m;
    }
    return nullptr;
}

std::expected<TsCatalog, QString> readTs(const QByteArray &bytes)
{
    TsCatalog catalog;
    QXmlStreamReader xml(bytes);
    const auto fail = [&](const QString &why) {
        return std::unexpected(QStringLiteral("line %1: %2").arg(xml.lineNumber()).arg(why));
    };
    if (!xml.readNextStartElement() || xml.name() != u"TS")
        return fail(QStringLiteral("not a TS file"));
    catalog.language = xml.attributes().value(u"language").toString();
    catalog.sourceLanguage = xml.attributes().value(u"sourcelanguage").toString();
    while (xml.readNextStartElement()) {
        if (xml.name().startsWith(u"extra-")) {
            const QString name = xml.name().mid(6).toString();
            auto value = readText(xml);
            if (!value)
                return fail(value.error());
            catalog.extras.append({name, *value});
            continue;
        }
        if (xml.name() != u"context") {
            xml.skipCurrentElement();
            continue;
        }
        TsContext context;
        while (xml.readNextStartElement()) {
            if (xml.name() == u"name") {
                auto value = readText(xml);
                if (!value)
                    return fail(value.error());
                context.name = *value;
                continue;
            }
            if (xml.name() != u"message") {
                xml.skipCurrentElement();
                continue;
            }
            TsMessage message;
            message.numerus = xml.attributes().value(u"numerus") == u"yes";
            while (xml.readNextStartElement()) {
                const QString element = xml.name().toString();
                if (element == u"translation") {
                    message.state = parseState(xml.attributes().value(u"type"));
                    if (!message.numerus) {
                        auto value = readText(xml);
                        if (!value)
                            return fail(value.error());
                        message.translations = {*value};
                        continue;
                    }
                    while (xml.readNextStartElement()) {
                        if (xml.name() != u"numerusform")
                            return fail(QStringLiteral("unexpected <%1> in a numerus translation").arg(xml.name()));
                        auto value = readText(xml);
                        if (!value)
                            return fail(value.error());
                        message.translations.append(*value);
                    }
                    continue;
                }
                if (element == u"location") {
                    xml.skipCurrentElement();
                    continue;
                }
                auto value = readText(xml);
                if (!value)
                    return fail(value.error());
                if (element == u"source")
                    message.source = *value;
                else if (element == u"comment")
                    message.comment = *value;
                else if (element == u"extracomment")
                    message.extraComment = *value;
                else if (element == u"translatorcomment")
                    message.translatorComment = *value;
                else if (element.startsWith(u"extra-"))
                    message.extras.append({element.mid(6), *value});
                else
                    return fail(QStringLiteral("unsupported <%1>").arg(element));
            }
            context.messages.append(message);
        }
        catalog.contexts.append(context);
    }
    if (xml.hasError())
        return fail(xml.errorString());
    return catalog;
}

QByteArray writeTs(const TsCatalog &catalog)
{
    QString out = QStringLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<!DOCTYPE TS>\n<TS version=\"2.1\"");
    if (!catalog.language.isEmpty())
        out += QStringLiteral(" language=\"%1\"").arg(catalog.language);
    if (!catalog.sourceLanguage.isEmpty())
        out += QStringLiteral(" sourcelanguage=\"%1\"").arg(catalog.sourceLanguage);
    out += QStringLiteral(">\n");
    writeExtras(out, catalog.extras, QStringLiteral("    "));
    for (const TsContext &context : catalog.contexts) {
        out += QStringLiteral("<context>\n    <name>%1</name>\n").arg(protect(context.name));
        for (const TsMessage &m : context.messages) {
            out += m.numerus ? QStringLiteral("    <message numerus=\"yes\">\n") : QStringLiteral("    <message>\n");
            out += QStringLiteral("        <source>%1</source>\n").arg(protect(m.source));
            if (!m.comment.isEmpty())
                out += QStringLiteral("        <comment>%1</comment>\n").arg(protect(m.comment));
            if (!m.extraComment.isEmpty())
                out += QStringLiteral("        <extracomment>%1</extracomment>\n").arg(protect(m.extraComment));
            if (!m.translatorComment.isEmpty())
                out += QStringLiteral("        <translatorcomment>%1</translatorcomment>\n").arg(protect(m.translatorComment));
            out += QStringLiteral("        <translation%1>").arg(stateAttribute(m.state));
            if (m.numerus) {
                out += u'\n';
                for (const QString &form : m.translations)
                    out += QStringLiteral("            <numerusform>%1</numerusform>\n").arg(protect(form));
                out += QStringLiteral("        ");
            } else {
                out += protect(m.translations.value(0));
            }
            out += QStringLiteral("</translation>\n");
            writeExtras(out, m.extras, QStringLiteral("        "));
            out += QStringLiteral("    </message>\n");
        }
        out += QStringLiteral("</context>\n");
    }
    out += QStringLiteral("</TS>\n");
    return out.toUtf8();
}

int qtNumerusForms(const QString &language)
{
    if (language == u"pl")
        return 3;
    if (language == u"ko_KR" || language == u"th_TH")
        return 1;
    if (language == u"ta" || language == u"en")
        return 2;
    return -1;
}

} // namespace hikari::i18n
