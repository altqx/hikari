#include "hikari/i18n/migration.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>

namespace hikari::i18n {
namespace {

constexpr auto absentField = "\\N";

QString escapeField(const std::optional<QString> &value)
{
    if (!value)
        return QString::fromLatin1(absentField);
    QString out;
    out.reserve(value->size());
    for (const QChar c : *value) {
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

std::expected<std::optional<QString>, QString> unescapeField(const QString &field)
{
    if (field == QLatin1StringView(absentField))
        return std::optional<QString>{};
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
    return std::optional<QString>(out);
}

std::optional<MapStatus> parseStatus(QStringView name)
{
    for (MapStatus status : {MapStatus::Exact, MapStatus::Placeholder, MapStatus::New, MapStatus::Obsolete})
        if (statusName(status) == name)
            return status;
    return std::nullopt;
}

const QStringList columns = {QStringLiteral("status"),  QStringLiteral("msgctxt"), QStringLiteral("msgid"),
                             QStringLiteral("msgid_plural"), QStringLiteral("context"), QStringLiteral("source"),
                             QStringLiteral("comment"), QStringLiteral("numerus"), QStringLiteral("placeholders")};

// A rewrite key: context, source, disambiguation, numerus.
QString rewriteKey(const QString &context, const QString &source, const QString &comment, bool numerus)
{
    return context + QChar(0) + source + QChar(0) + comment + QChar(0) + (numerus ? u'1' : u'0');
}

QString legacyKeyOf(const std::optional<QString> &msgctxt, const QString &msgid, const std::optional<QString> &plural)
{
    return (msgctxt ? u'1' + *msgctxt : QStringLiteral("0")) + QChar(0) + msgid + QChar(0)
           + (plural ? u'1' + *plural : QStringLiteral("0"));
}

QString legacyKeyOf(const PoMessage &m) { return legacyKeyOf(m.msgctxt, m.msgid, m.msgidPlural); }

bool live(const TsMessage &m) { return m.state != TsState::Vanished && m.state != TsState::Obsolete; }

QString describePlaceholders(const QString &msgid, const QtFormat &format)
{
    QStringList from;
    for (const PrintfDirective &d : printfDirectives(msgid))
        from.append(msgid.mid(d.position, d.length));
    QStringList to;
    for (auto it = format.arguments.cbegin(); it != format.arguments.cend(); ++it)
        to.append(QStringLiteral("%%1").arg(it.key()));
    return from.join(u' ') + QStringLiteral(" -> ") + to.join(u' ');
}

QString markdownCode(const QString &text)
{
    QString escaped = text;
    escaped.replace(u'\n', QStringLiteral("\\n"));
    escaped.replace(u'|', QStringLiteral("\\|"));
    return u'`' + escaped + u'`';
}

// The legacy catalog's state of a message, as msgfmt compiles it
// (tools/compile_catalogs.py:42 runs "msgfmt -c" without --use-fuzzy, so
// fuzzy entries never reach the MO) and wx loads it (translation.cpp:1074 at
// wxWidgets 670835ae stores only non-empty strings).
enum class LegacyState { Translated, Fuzzy, Untranslated };

LegacyState legacyState(const PoMessage &m, qsizetype form = 0)
{
    if (m.msgstr.value(form).isEmpty())
        return LegacyState::Untranslated;
    return m.fuzzy() ? LegacyState::Fuzzy : LegacyState::Translated;
}

TsState tsStateFor(LegacyState state)
{
    return state == LegacyState::Translated ? TsState::Finished : TsState::Unfinished;
}

QList<std::pair<QString, QString>> catalogExtras(const PoCatalog &po, const LanguageInput &input, const QString &commit)
{
    QList<std::pair<QString, QString>> extras;
    extras.append({QStringLiteral("po-input"), QStringLiteral("Locale/%1 at %2").arg(input.fileName, commit)});
    if (!po.headerComments.isEmpty())
        extras.append({QStringLiteral("po-header-comments"), po.headerComments.join(u'\n')});
    for (const QString &field : {QStringLiteral("Last-Translator"), QStringLiteral("Language-Team"),
                                 QStringLiteral("PO-Revision-Date"), QStringLiteral("Plural-Forms")}) {
        const QString value = po.header(field);
        if (!value.isEmpty())
            extras.append({QStringLiteral("po-") + field.toLower().remove(QRegularExpression(QStringLiteral("^po-"))), value});
    }
    return extras;
}

struct LanguageStats {
    QString language;
    QString fileName;
    int legacyForms = 0;
    int qtForms = 0;
    int translated = 0, fuzzy = 0, untranslated = 0;
    int uiFinished = 0, uiCarriedUnfinished = 0, uiEmpty = 0;
    int gettextFinished = 0, gettextCarriedUnfinished = 0, gettextEmpty = 0;
    int formatChecked = 0;
    QStringList formatMismatches;
    QStringList placeholderDropped;
};

// msgfmt -c's check: the same directives, in the same argument positions.
bool sameDirectives(const QString &msgid, const QString &msgstr)
{
    const auto a = printfToQt(msgid);
    const auto b = printfToQt(msgstr);
    if (!a || !b)
        return false;
    return a->arguments == b->arguments;
}

} // namespace

QString statusName(MapStatus status)
{
    switch (status) {
    case MapStatus::Exact: return QStringLiteral("exact");
    case MapStatus::Placeholder: return QStringLiteral("placeholder");
    case MapStatus::New: return QStringLiteral("new");
    case MapStatus::Obsolete: return QStringLiteral("obsolete");
    }
    return {};
}

std::optional<QtFormat> printfToQt(const QString &text)
{
    const QList<PrintfDirective> directives = printfDirectives(text);
    bool positional = false, sequential = false;
    for (const PrintfDirective &d : directives) {
        if (d.starArgs)
            return std::nullopt;
        (d.argument > 0 ? positional : sequential) = true;
    }
    if (positional && sequential)
        return std::nullopt;
    QtFormat out;
    qsizetype from = 0;
    int next = 0;
    const auto appendLiteral = [&](QStringView literal) -> bool {
        for (qsizetype i = 0; i < literal.size(); ++i) {
            if (literal[i] != u'%') {
                out.text += literal[i];
                continue;
            }
            // Only "%%" is left between directives: it becomes "%", which
            // must not be read back as a marker.
            if (i + 1 < literal.size() && literal[i + 1] == u'%')
                ++i;
            const qsizetype after = i + 1;
            if (after < literal.size() && (literal[after].isDigit() || literal[after] == u'L'))
                return false;
            out.text += u'%';
        }
        return true;
    };
    for (const PrintfDirective &d : directives) {
        if (!appendLiteral(QStringView(text).mid(from, d.position - from)))
            return std::nullopt;
        const int argument = d.argument > 0 ? d.argument : ++next;
        if (argument > 99)
            return std::nullopt;
        if (out.arguments.contains(argument) && out.arguments.value(argument) != d.conversion)
            return std::nullopt;
        out.arguments.insert(argument, d.conversion);
        out.text += QStringLiteral("%%1").arg(argument);
        from = d.position + d.length;
    }
    if (!appendLiteral(QStringView(text).mid(from)))
        return std::nullopt;
    // A marker followed by a digit would be read as a longer one.
    for (qsizetype i = 0; i + 2 < out.text.size(); ++i)
        if (out.text[i] == u'%' && out.text[i + 1].isDigit() && out.text[i + 2].isDigit()
            && !out.arguments.contains(QStringView(out.text).mid(i + 1, 2).toInt()))
            return std::nullopt;
    return out;
}

KeyMap buildKeyMap(const PoCatalog &legacy, const TsCatalog &rewriteKeys, const QString &inputCommit)
{
    KeyMap map;
    map.inputCommit = inputCommit;
    QSet<QString> matched;
    for (const PoMessage &m : legacy.messages) {
        if (m.obsolete)
            continue;
        bool any = false;
        const bool cFormat = m.flags.contains(QStringLiteral("c-format"));
        const std::optional<QtFormat> qtFormat = cFormat && !m.plural() ? printfToQt(m.msgid) : std::nullopt;
        for (const TsContext &context : rewriteKeys.contexts) {
            for (const TsMessage &key : context.messages) {
                if (!live(key) || m.plural() || key.numerus || key.comment != m.msgctxt.value_or(QString()))
                    continue;
                KeyMapRow row;
                if (key.source == m.msgid) {
                    row.status = MapStatus::Exact;
                } else if (qtFormat && !qtFormat->arguments.isEmpty() && key.source == qtFormat->text) {
                    row.status = MapStatus::Placeholder;
                    row.placeholders = describePlaceholders(m.msgid, *qtFormat);
                } else {
                    continue;
                }
                row.msgctxt = m.msgctxt;
                row.msgid = m.msgid;
                row.msgidPlural = m.msgidPlural;
                row.context = context.name;
                row.source = key.source;
                row.comment = key.comment;
                row.numerus = key.numerus;
                map.rows.append(row);
                matched.insert(rewriteKey(context.name, key.source, key.comment, key.numerus));
                any = true;
            }
        }
        if (!any) {
            KeyMapRow row;
            row.status = MapStatus::Obsolete;
            row.msgctxt = m.msgctxt;
            row.msgid = m.msgid;
            row.msgidPlural = m.msgidPlural;
            map.rows.append(row);
        }
    }
    for (const TsContext &context : rewriteKeys.contexts) {
        for (const TsMessage &key : context.messages) {
            if (!live(key) || matched.contains(rewriteKey(context.name, key.source, key.comment, key.numerus)))
                continue;
            KeyMapRow row;
            row.status = MapStatus::New;
            row.context = context.name;
            row.source = key.source;
            row.comment = key.comment;
            row.numerus = key.numerus;
            map.rows.append(row);
        }
    }
    return map;
}

QByteArray writeKeyMap(const KeyMap &map)
{
    QString out = QStringLiteral("# O4 key map: legacy gettext keys (Locale/template.pot at %1) to the rewrite's Qt keys.\n"
                                 "# Reviewed input of hikari_i18n_migrate convert; regenerate with hikari_i18n_keymap.\n")
                      .arg(map.inputCommit);
    out += QStringLiteral("# input-commit\t%1\n").arg(map.inputCommit);
    out += columns.join(u'\t') + u'\n';
    for (const KeyMapRow &row : map.rows) {
        const bool hasRewrite = row.context.has_value();
        QStringList fields = {statusName(row.status),
                              escapeField(row.msgctxt),
                              escapeField(row.msgid),
                              escapeField(row.msgidPlural),
                              escapeField(row.context),
                              escapeField(hasRewrite ? std::optional(row.source) : std::nullopt),
                              escapeField(hasRewrite ? std::optional(row.comment) : std::nullopt),
                              hasRewrite ? (row.numerus ? QStringLiteral("yes") : QStringLiteral("no"))
                                         : QString::fromLatin1(absentField),
                              escapeField(row.placeholders.isEmpty() ? std::nullopt : std::optional(row.placeholders))};
        out += fields.join(u'\t') + u'\n';
    }
    return out.toUtf8();
}

std::expected<KeyMap, QString> readKeyMap(const QByteArray &bytes)
{
    KeyMap map;
    const QStringList lines = QString::fromUtf8(bytes).split(u'\n');
    bool header = false;
    for (qsizetype n = 0; n < lines.size(); ++n) {
        const QString &line = lines[n];
        const auto fail = [&](const QString &why) {
            return std::unexpected(QStringLiteral("line %1: %2").arg(n + 1).arg(why));
        };
        if (line.isEmpty())
            continue;
        if (line.startsWith(QStringLiteral("# input-commit\t"))) {
            map.inputCommit = line.mid(15);
            continue;
        }
        if (line.startsWith(u'#'))
            continue;
        const QStringList fields = line.split(u'\t');
        if (!header) {
            if (fields != columns)
                return fail(QStringLiteral("expected the column header"));
            header = true;
            continue;
        }
        if (fields.size() != columns.size())
            return fail(QStringLiteral("%1 fields, expected %2").arg(fields.size()).arg(columns.size()));
        QList<std::optional<QString>> values;
        for (const QString &field : fields) {
            auto value = unescapeField(field);
            if (!value)
                return fail(value.error());
            values.append(*value);
        }
        KeyMapRow row;
        const auto status = parseStatus(fields[0]);
        if (!status)
            return fail(QStringLiteral("unknown status %1").arg(fields[0]));
        row.status = *status;
        row.msgctxt = values[1];
        row.msgid = values[2];
        row.msgidPlural = values[3];
        row.context = values[4];
        row.source = values[5].value_or(QString());
        row.comment = values[6].value_or(QString());
        row.numerus = fields[7] == u"yes";
        row.placeholders = values[8].value_or(QString());
        const bool needsLegacy = row.status != MapStatus::New;
        const bool needsRewrite = row.status != MapStatus::Obsolete;
        if (needsLegacy != row.msgid.has_value() || needsRewrite != row.context.has_value()
            || needsRewrite != values[5].has_value())
            return fail(QStringLiteral("a %1 row with the wrong keys").arg(fields[0]));
        if (!row.msgid && (row.msgctxt || row.msgidPlural))
            return fail(QStringLiteral("a msgctxt or msgid_plural without msgid"));
        map.rows.append(row);
    }
    if (!header)
        return std::unexpected(QStringLiteral("no column header"));
    if (map.inputCommit.isEmpty())
        return std::unexpected(QStringLiteral("no input commit"));
    return map;
}

std::expected<MigrationOutput, QString> migrate(const KeyMap &map, const TsCatalog &rewriteKeys,
                                                const QByteArray &templatePot, const QList<LanguageInput> &languages)
{
    const auto pot = parsePo(templatePot);
    if (!pot)
        return std::unexpected(QStringLiteral("template.pot: %1").arg(pot.error()));

    // The map must still describe the template and the keys.
    QHash<QString, const PoMessage *> templateMessages;
    for (const PoMessage &m : pot->messages)
        if (!m.obsolete)
            templateMessages.insert(legacyKeyOf(m), &m);
    QHash<QString, const KeyMapRow *> rowByRewriteKey;
    QSet<QString> legacyCovered;
    for (const KeyMapRow &row : map.rows) {
        if (row.msgid) {
            const QString key = legacyKeyOf(row.msgctxt, *row.msgid, row.msgidPlural);
            if (!templateMessages.contains(key))
                return std::unexpected(QStringLiteral("key map: %1 is not in the template").arg(markdownCode(*row.msgid)));
            legacyCovered.insert(key);
        }
        if (row.context) {
            const QString key = rewriteKey(*row.context, row.source, row.comment, row.numerus);
            if (rowByRewriteKey.contains(key))
                return std::unexpected(QStringLiteral("key map: two rows for %1 in %2").arg(markdownCode(row.source), *row.context));
            rowByRewriteKey.insert(key, &row);
        }
    }
    for (auto it = templateMessages.cbegin(); it != templateMessages.cend(); ++it)
        if (!legacyCovered.contains(it.key()))
            return std::unexpected(QStringLiteral("key map: no row for the template's %1").arg(markdownCode(it.value()->msgid)));
    QSet<QString> liveKeys;
    for (const TsContext &context : rewriteKeys.contexts)
        for (const TsMessage &m : context.messages)
            if (live(m)) {
                const QString key = rewriteKey(context.name, m.source, m.comment, m.numerus);
                liveKeys.insert(key);
                if (!rowByRewriteKey.contains(key))
                    return std::unexpected(QStringLiteral("key map: no row for %1 in %2").arg(markdownCode(m.source), context.name));
            }
    for (auto it = rowByRewriteKey.cbegin(); it != rowByRewriteKey.cend(); ++it)
        if (!liveKeys.contains(it.key()))
            return std::unexpected(QStringLiteral("key map: %1 in %2 is no longer extracted")
                                       .arg(markdownCode(it.value()->source), *it.value()->context));

    MigrationOutput output;
    QList<LanguageStats> stats;
    for (const LanguageInput &input : languages) {
        const auto po = parsePo(input.bytes);
        if (!po)
            return std::unexpected(QStringLiteral("%1: %2").arg(input.fileName, po.error()));
        const int qtForms = qtNumerusForms(input.language);
        if (qtForms < 0)
            return std::unexpected(QStringLiteral("%1: no numerus rule known for %2").arg(input.fileName, input.language));
        QHash<QString, const PoMessage *> messages;
        for (const PoMessage &m : po->messages)
            if (!m.obsolete)
                messages.insert(legacyKeyOf(m), &m);
        if (messages.size() != templateMessages.size())
            return std::unexpected(QStringLiteral("%1 has %2 messages, the template %3")
                                       .arg(input.fileName).arg(messages.size()).arg(templateMessages.size()));
        for (auto it = templateMessages.cbegin(); it != templateMessages.cend(); ++it)
            if (!messages.contains(it.key()))
                return std::unexpected(QStringLiteral("%1 lacks the template's %2").arg(input.fileName, markdownCode(it.value()->msgid)));

        LanguageStats s;
        s.language = input.language;
        s.fileName = input.fileName;
        s.qtForms = qtForms;
        const QString pluralForms = po->header(QStringLiteral("Plural-Forms"));
        const qsizetype np = pluralForms.indexOf(QStringLiteral("nplurals="));
        s.legacyForms = np < 0 ? 0 : pluralForms.mid(np + 9).section(u';', 0, 0).trimmed().toInt();
        for (const PoMessage &m : po->messages) {
            if (m.obsolete)
                continue;
            switch (legacyState(m)) {
            case LegacyState::Translated: ++s.translated; break;
            case LegacyState::Fuzzy: ++s.fuzzy; break;
            case LegacyState::Untranslated: ++s.untranslated; break;
            }
            if (!m.flags.contains(QStringLiteral("c-format")))
                continue;
            for (qsizetype form = 0; form < m.msgstr.size(); ++form) {
                if (m.msgstr[form].isEmpty())
                    continue;
                ++s.formatChecked;
                const QString &id = form == 0 || !m.msgidPlural ? m.msgid : *m.msgidPlural;
                if (!sameDirectives(id, m.msgstr[form]))
                    s.formatMismatches.append(QStringLiteral("%1 msgstr[%2]").arg(markdownCode(m.msgid)).arg(form));
            }
        }

        // The rewrite's catalog: the extracted keys in lupdate's order with
        // the mapped legacy translations.
        TsCatalog ui;
        ui.language = input.language;
        ui.sourceLanguage = rewriteKeys.sourceLanguage;
        for (const TsContext &context : rewriteKeys.contexts) {
            TsContext out{context.name, {}};
            for (TsMessage m : context.messages) {
                if (!live(m))
                    continue;
                m.translatorComment.clear();
                m.state = TsState::Unfinished;
                m.translations = m.numerus ? QStringList(qtForms, QString()) : QStringList{QString()};
                const KeyMapRow *row = rowByRewriteKey.value(rewriteKey(context.name, m.source, m.comment, m.numerus));
                if (row && row->msgid && (row->status == MapStatus::Exact || row->status == MapStatus::Placeholder)) {
                    const PoMessage *legacy = messages.value(legacyKeyOf(row->msgctxt, *row->msgid, row->msgidPlural));
                    const LegacyState state = legacyState(*legacy);
                    QString text = legacy->msgstr.value(0);
                    if (row->status == MapStatus::Placeholder && !text.isEmpty()) {
                        // A formatting change is never finished without review.
                        const auto id = printfToQt(*row->msgid);
                        const auto translated = printfToQt(text);
                        if (translated && id && translated->arguments == id->arguments) {
                            text = translated->text;
                        } else {
                            s.placeholderDropped.append(QStringLiteral("%1 (%2)").arg(markdownCode(*row->msgid), context.name));
                            text.clear();
                        }
                        m.state = TsState::Unfinished;
                    } else {
                        m.state = tsStateFor(state);
                    }
                    if (!m.numerus)
                        m.translations = {text};
                    if (m.state == TsState::Finished)
                        ++s.uiFinished;
                    else if (!text.isEmpty())
                        ++s.uiCarriedUnfinished;
                    else
                        ++s.uiEmpty;
                }
                out.messages.append(m);
            }
            if (!out.messages.isEmpty())
                ui.contexts.append(out);
        }
        output.uiCatalogs.insert(input.language, writeTs(ui));

        // aegisub.gettext's catalog: every key the legacy wxGetTranslation(str)
        // reaches (Automation.cpp:85-86): the context-free ids, a plural
        // entry's by its singular id with msgstr[0] (wx stores the MO's
        // original up to its first NUL with form 0 under the bare id,
        // translation.cpp:1061-1078, and finds it with n == UINT_MAX,
        // translation.cpp:1144-1160). Context entries are unreachable there.
        TsCatalog gettext;
        gettext.language = input.language;
        gettext.extras = catalogExtras(*po, input, map.inputCommit);
        TsContext context{QString::fromLatin1(gettextContext), {}};
        for (const PoMessage &templateMessage : pot->messages) {
            if (templateMessage.obsolete || templateMessage.msgctxt || templateMessage.msgid.isEmpty())
                continue;
            const PoMessage *legacy = messages.value(legacyKeyOf(templateMessage));
            TsMessage m;
            m.source = legacy->msgid;
            const LegacyState state = legacyState(*legacy);
            m.state = tsStateFor(state);
            m.translations = {legacy->msgstr.value(0)};
            if (!legacy->flags.isEmpty())
                m.extras.append({QStringLiteral("po-flags"), legacy->flags.join(QStringLiteral(", "))});
            if (legacy->msgidPlural)
                m.extras.append({QStringLiteral("po-msgid_plural"), *legacy->msgidPlural});
            if (!legacy->translatorComments.isEmpty())
                m.translatorComment = legacy->translatorComments.join(u'\n');
            if (state == LegacyState::Translated)
                ++s.gettextFinished;
            else if (state == LegacyState::Fuzzy)
                ++s.gettextCarriedUnfinished;
            else
                ++s.gettextEmpty;
            context.messages.append(m);
        }
        gettext.contexts.append(context);
        output.gettextCatalogs.insert(input.language, writeTs(gettext));
        stats.append(s);
    }

    // The accounting report.
    int exact = 0, placeholder = 0, added = 0, obsolete = 0, numerusNew = 0;
    QSet<QString> exactLegacy, placeholderLegacy;
    QHash<QString, int> rowsPerLegacy;
    for (const KeyMapRow &row : map.rows) {
        const QString key = row.msgid ? legacyKeyOf(row.msgctxt, *row.msgid, row.msgidPlural) : QString();
        switch (row.status) {
        case MapStatus::Exact: ++exact; exactLegacy.insert(key); ++rowsPerLegacy[key]; break;
        case MapStatus::Placeholder: ++placeholder; placeholderLegacy.insert(key); ++rowsPerLegacy[key]; break;
        case MapStatus::New: ++added; numerusNew += row.numerus; break;
        case MapStatus::Obsolete: ++obsolete; break;
        }
    }
    int oneToMany = 0;
    for (int n : std::as_const(rowsPerLegacy))
        oneToMany += n > 1;
    int liveKeyCount = 0, numerusKeys = 0;
    QSet<QString> contexts;
    for (const TsContext &context : rewriteKeys.contexts)
        for (const TsMessage &m : context.messages)
            if (live(m)) {
                ++liveKeyCount;
                numerusKeys += m.numerus;
                contexts.insert(context.name);
            }
    int legacyPlural = 0, legacyContext = 0, legacyCFormat = 0, gettextKeys = 0;
    for (const PoMessage &m : pot->messages) {
        legacyPlural += m.plural();
        legacyContext += m.msgctxt.has_value();
        legacyCFormat += m.flags.contains(QStringLiteral("c-format"));
        gettextKeys += !m.msgctxt;
    }
    const auto mapped = [&](const auto &predicate) {
        int n = 0;
        for (const KeyMapRow &row : map.rows)
            if ((row.status == MapStatus::Exact || row.status == MapStatus::Placeholder) && predicate(row.source))
                ++n;
        return n;
    };

    QString r;
    r += QStringLiteral("# O4 migration accounting\n\n");
    r += QStringLiteral("Generated by `hikari_i18n_migrate convert` (the `hikari_i18n_convert` target) from `i18n/migration/keymap.tsv` and the keys `i18n/migration/keys.ts`; do not edit by hand. "
                        "The legacy catalogs are the immutable input `Locale/*.po` at `%1`.\n\n")
             .arg(map.inputCommit);
    r += QStringLiteral("## Inputs\n\n");
    r += QStringLiteral("- Legacy template `Locale/template.pot`: %1 messages (%2 with a plural, %3 with a msgctxt, %4 c-format).\n")
             .arg(templateMessages.size()).arg(legacyPlural).arg(legacyContext).arg(legacyCFormat);
    r += QStringLiteral("- Rewrite keys (lupdate 6.11.2 over the source targets): %1 messages in %2 contexts, %3 numerus.\n\n")
             .arg(liveKeyCount).arg(contexts.size()).arg(numerusKeys);
    r += QStringLiteral("## Key map\n\n");
    r += QStringLiteral("| Status | Rows | Meaning |\n| --- | --- | --- |\n");
    r += QStringLiteral("| exact | %1 rows from %2 legacy messages (%3 one-to-many) | Same source text and disambiguation (msgctxt), neither plural. "
                        "The reviewed translation is kept; a fuzzy or empty one stays unfinished. |\n")
             .arg(exact).arg(exactLegacy.size()).arg(oneToMany);
    r += QStringLiteral("| placeholder | %1 rows from %2 legacy messages | A c-format id that is the rewrite source once its directives are Qt markers. "
                        "The converted translation is carried unfinished for review. |\n")
             .arg(placeholder).arg(placeholderLegacy.size());
    r += QStringLiteral("| new | %1 (%2 numerus) | Rewrite keys no legacy message matches: untranslated. |\n").arg(added).arg(numerusNew);
    r += QStringLiteral("| obsolete | %1 | Legacy messages the rewrite does not (yet) use. Their translations stay in the "
                        "PO at the input commit and, when context-free, in the gettext catalogs. |\n\n")
             .arg(obsolete);
    static const QRegularExpression literalPercent(QStringLiteral("%(?![0-9L])"));
    static const QRegularExpression marker(QStringLiteral("%L?[0-9]"));
    r += QStringLiteral("Mapped rows keep their formatting deliberately: %1 with an accelerator `&`, %2 with a line break, "
                        "%3 with a literal `%`, %4 with Qt markers, %5 with non-ASCII text.\n\n")
             .arg(mapped([](const QString &s) { return s.contains(u'&'); }))
             .arg(mapped([](const QString &s) { return s.contains(u'\n'); }))
             .arg(mapped([](const QString &s) { return s.contains(literalPercent); }))
             .arg(mapped([](const QString &s) { return s.contains(marker); }))
             .arg(mapped([](const QString &s) { return std::any_of(s.cbegin(), s.cend(), [](QChar c) { return c.unicode() > 0x7f; }); }));
    r += QStringLiteral("The two font-count meanings (msgctxt \"found or copied\" and \"not found or not copied\") stay separate keys; "
                        "both are plural and context-only, so neither maps to a rewrite key nor reaches the gettext catalogs. "
                        "The plural probe keeps them apart by disambiguation.\n\n");
    if (placeholder > 0) {
        r += QStringLiteral("### Placeholder rows\n\n| Legacy id | Rewrite context | Directives |\n| --- | --- | --- |\n");
        for (const KeyMapRow &row : map.rows)
            if (row.status == MapStatus::Placeholder)
                r += QStringLiteral("| %1 | %2 | %3 |\n").arg(markdownCode(*row.msgid), *row.context, markdownCode(row.placeholders));
        r += u'\n';
    }
    r += QStringLiteral("## Catalogs\n\n");
    r += QStringLiteral("`i18n/hikarisub_<language>.ts` holds the rewrite's keys (lupdate updates it); "
                        "`i18n/hikarisub_gettext_<language>.ts` holds the %1 context-free legacy ids in the context `%2` "
                        "for aegisub.gettext (lupdate never touches it). Unfinished entries stay out of the QM (lrelease -nounfinished), "
                        "as fuzzy and empty entries stayed out of the legacy MO.\n\n")
             .arg(gettextKeys).arg(QString::fromLatin1(gettextContext));
    r += QStringLiteral("| Language | PO | Plural forms (legacy / Qt) | PO translated / fuzzy / empty | UI finished / unfinished with text / empty (of mapped) "
                        "| gettext finished / unfinished with text / empty | c-format forms checked / mismatched |\n"
                        "| --- | --- | --- | --- | --- | --- | --- |\n");
    for (const LanguageStats &s : stats)
        r += QStringLiteral("| %1 | %2 | %3 / %4 | %5 / %6 / %7 | %8 / %9 / %10 | %11 / %12 / %13 | %14 / %15 |\n")
                 .arg(s.language, s.fileName)
                 .arg(s.legacyForms).arg(s.qtForms)
                 .arg(s.translated).arg(s.fuzzy).arg(s.untranslated)
                 .arg(s.uiFinished).arg(s.uiCarriedUnfinished).arg(s.uiEmpty)
                 .arg(s.gettextFinished).arg(s.gettextCarriedUnfinished).arg(s.gettextEmpty)
                 .arg(s.formatChecked).arg(s.formatMismatches.size());
    r += u'\n';
    for (const LanguageStats &s : stats) {
        for (const QString &m : s.formatMismatches)
            r += QStringLiteral("- %1: directive mismatch in %2.\n").arg(s.language, m);
        for (const QString &m : s.placeholderDropped)
            r += QStringLiteral("- %1: the translation of %2 did not convert and is left empty.\n").arg(s.language, m);
    }
    r += QStringLiteral("Attribution, the PO header fields and the input file are kept as `<extra-po-*>` elements of each gettext catalog; "
                        "per-message flags (fuzzy, c-format) and plural ids as `<extra-po-flags>` and `<extra-po-msgid_plural>`.\n");
    output.report = r.toUtf8();
    return output;
}

std::expected<QByteArray, QString> legacyPluralProbe(const LanguageInput &input)
{
    const auto po = parsePo(input.bytes);
    if (!po)
        return std::unexpected(QStringLiteral("%1: %2").arg(input.fileName, po.error()));
    const bool isTemplate = input.fileName.endsWith(QStringLiteral(".pot"));
    TsCatalog catalog;
    catalog.language = input.language;
    TsContext context{QString::fromLatin1(pluralProbeContext), {}};
    const auto toNumerus = [&](const QString &text) -> std::expected<QString, QString> {
        const QList<PrintfDirective> directives = printfDirectives(text);
        if (directives.size() != 1 || directives[0].conversion != u'd' || directives[0].length != 2)
            return std::unexpected(QStringLiteral("%1: %2 is not one plain %d").arg(input.fileName, text));
        QString out = text;
        out.replace(directives[0].position, 2, QStringLiteral("%n"));
        out.replace(QStringLiteral("%%"), QStringLiteral("%"));
        return out;
    };
    for (const PoMessage &m : po->messages) {
        if (m.obsolete || !m.plural())
            continue;
        TsMessage message;
        message.numerus = true;
        auto source = toNumerus(m.msgid);
        if (!source)
            return std::unexpected(source.error());
        message.source = *source;
        message.comment = m.msgctxt.value_or(QString());
        const QStringList forms = isTemplate ? QStringList{m.msgid, *m.msgidPlural} : m.msgstr;
        // An entry with an empty form is untranslated: wx falls back to the
        // ids (translation.h:305-307), so the probe leaves it to the English
        // catalog. A fuzzy one never reached the MO.
        const bool untranslated = std::any_of(forms.cbegin(), forms.cend(), [](const QString &f) { return f.isEmpty(); });
        if (untranslated) {
            message.translations = QStringList(std::max(qtNumerusForms(input.language), 1), QString());
            message.state = TsState::Unfinished;
        } else {
            for (const QString &form : forms) {
                auto converted = toNumerus(form);
                if (!converted)
                    return std::unexpected(converted.error());
                message.translations.append(*converted);
            }
            message.state = !isTemplate && m.fuzzy() ? TsState::Unfinished : TsState::Finished;
        }
        context.messages.append(message);
    }
    catalog.contexts.append(context);
    return writeTs(catalog);
}

} // namespace hikari::i18n
