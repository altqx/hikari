// O4: the QM the build compiles from the checked-in TS (embedded under
// :/i18n by hikari_translations) answer exactly the finished entries; the
// gettext catalogs answer as the legacy MO did through wxGetTranslation; and
// Qt's numerus rules pick the legacy Plural-Forms' form for 0/1/2/5/12/22.

#include "hikari/i18n/migration.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QHash>
#include <QTranslator>
#include <QtEndian>

#include <algorithm>

using namespace hikari::i18n;

namespace {

const QStringList languages = {QStringLiteral("pl"), QStringLiteral("ko_KR"), QStringLiteral("th_TH"), QStringLiteral("ta")};
const QList<int> counts = {0, 1, 2, 5, 12, 22};

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        ADD_FAILURE() << "cannot read " << path.toStdString();
    return file.readAll();
}

TsCatalog readCatalog(const QString &path)
{
    auto catalog = readTs(readFile(path));
    EXPECT_TRUE(catalog.has_value()) << path.toStdString();
    return catalog.value_or(TsCatalog{});
}

QString source(const QString &relative) { return QStringLiteral(HIKARI_SOURCE_DIR "/") + relative; }

QString lookup(const QTranslator &translator, const QString &context, const TsMessage &m, int n = -1)
{
    return translator.translate(context.toUtf8().constData(), m.source.toUtf8().constData(),
                                m.comment.isEmpty() ? nullptr : m.comment.toUtf8().constData(), n);
}

// The legacy Plural-Forms of each catalog at 20d647c4, as gettext and wx
// evaluate them; English has no catalog and wxGetTranslation(str1, str2, n)
// falls back to n == 1 ? str1 : str2 (wx include/wx/translation.h:305-307).
int legacyForm(const QString &language, int n)
{
    if (language == u"pl")
        return n == 1 ? 0 : n % 10 >= 2 && n % 10 <= 4 && (n % 100 < 10 || n % 100 >= 20) ? 1 : 2;
    if (language == u"ko_KR" || language == u"th_TH")
        return 0;
    return n != 1 ? 1 : 0; // ta, and en's fallback
}

const QHash<QString, QString> legacyPluralForms = {
    {QStringLiteral("pl"), QStringLiteral("nplurals=3; plural=(n==1 ? 0 : n%10>=2 && n%10<=4 && (n%100<10 || n%100>=20) ? 1 : 2);")},
    {QStringLiteral("ko_KR"), QStringLiteral("nplurals=1; plural=0;")},
    {QStringLiteral("th_TH"), QStringLiteral("nplurals=1; plural=0;")},
    {QStringLiteral("ta"), QStringLiteral("nplurals=2; plural=n != 1;")},
    {QStringLiteral("en"), QStringLiteral("nplurals=2; plural=(n != 1);")}};

} // namespace

TEST(Qm, EveryCatalogLoadsFromTheBuild)
{
    for (const QString &language : languages) {
        for (const QString &name : {QStringLiteral("hikarisub_%1"), QStringLiteral("hikarisub_gettext_%1")}) {
            QTranslator translator;
            const QString file = QStringLiteral(":/i18n/") + name.arg(language) + QStringLiteral(".qm");
            ASSERT_TRUE(translator.load(file)) << file.toStdString();
            EXPECT_EQ(translator.language(), language);
            EXPECT_FALSE(translator.isEmpty());
        }
    }
}

// lrelease -nounfinished: finished entries answer, unfinished (fuzzy, carried
// placeholder conversions, new keys) fall back to the source.
TEST(Qm, RewriteCatalogsAnswerFinishedEntriesOnly)
{
    for (const QString &language : languages) {
        QTranslator translator;
        ASSERT_TRUE(translator.load(QStringLiteral(":/i18n/hikarisub_%1.qm").arg(language)));
        const TsCatalog catalog = readCatalog(source(QStringLiteral("i18n/hikarisub_%1.ts").arg(language)));
        int finished = 0, unfinished = 0;
        for (const TsContext &context : catalog.contexts)
            for (const TsMessage &m : context.messages) {
                if (m.state == TsState::Vanished || m.state == TsState::Obsolete)
                    continue;
                const bool done = m.state == TsState::Finished;
                const QString expected = done ? m.translations.value(0) : QString();
                EXPECT_EQ(lookup(translator, context.name, m, m.numerus ? 2 : -1), expected)
                    << language.toStdString() << " " << context.name.toStdString() << " " << m.source.toStdString();
                ++(done ? finished : unfinished);
            }
        EXPECT_GT(finished, 700) << language.toStdString();
        EXPECT_GT(unfinished, 300) << language.toStdString();
    }
    QTranslator pl;
    ASSERT_TRUE(pl.load(QStringLiteral(":/i18n/hikarisub_pl.qm")));
    EXPECT_EQ(pl.translate("Main", "Search bar"), QStringLiteral("Pasek szukania"));
    EXPECT_EQ(pl.translate("hikari::app::StyleManagerController", "Copy of "), QStringLiteral("Kopia "));
    EXPECT_EQ(pl.translate("hikari::app::Application", "Rule \"%1\" is invalid."), QString()); // carried, unfinished
}

TEST(Qm, GettextCatalogsAnswerTheirFinishedEntries)
{
    const QString context = QString::fromLatin1(gettextContext);
    for (const QString &language : languages) {
        QTranslator translator;
        ASSERT_TRUE(translator.load(QStringLiteral(":/i18n/hikarisub_gettext_%1.qm").arg(language)));
        const TsCatalog catalog = readCatalog(source(QStringLiteral("i18n/hikarisub_gettext_%1.ts").arg(language)));
        ASSERT_EQ(catalog.contexts.size(), 1);
        for (const TsMessage &m : catalog.contexts[0].messages)
            EXPECT_EQ(lookup(translator, context, m), m.state == TsState::Finished ? m.translations.value(0) : QString())
                << language.toStdString() << " " << m.source.toStdString();
        // Only the gettext context: the rewrite's contexts are not in this catalog.
        EXPECT_EQ(translator.translate("Main", "Search bar"), QString());
    }
    QTranslator pl;
    ASSERT_TRUE(pl.load(QStringLiteral(":/i18n/hikarisub_gettext_pl.qm")));
    EXPECT_EQ(pl.translate(gettextContext, "Search bar"), QStringLiteral("Pasek szukania"));
    EXPECT_EQ(pl.translate(gettextContext, "%d element"), QStringLiteral("%d element")); // msgstr[0], printf kept
}

// The gettext catalogs against the legacy MO built as tools/compile_catalogs.py
// builds it (msgfmt -c), read as wxMsgCatalogFile::LoadData reads it
// (wx translation.cpp:1054-1078 at 670835ae: each original up to its first NUL,
// form 0 under the bare id, empty strings dropped) and looked up as
// wxGetTranslation(str) does (translation.cpp:1144-1160 with n == UINT_MAX,
// 1567: the empty string is never translated). A missing translation is the
// input itself in both.
TEST(Qm, GettextCatalogsMatchTheLegacyMoLookup)
{
#ifndef HIKARI_LEGACY_MO_DIR
    GTEST_SKIP() << "msgfmt was not found when the build was configured";
#else
    const QString context = QString::fromLatin1(gettextContext);
    const auto pot = parsePo(readFile(source(QStringLiteral("Locale/template.pot"))));
    ASSERT_TRUE(pot.has_value());
    for (const QString &language : languages) {
        const QByteArray mo = readFile(QStringLiteral(HIKARI_LEGACY_MO_DIR "/legacy_%1.mo").arg(language));
        ASSERT_GE(mo.size(), 28);
        const auto word = [&](qsizetype offset) { return qFromLittleEndian<quint32>(mo.constData() + offset); };
        ASSERT_EQ(word(0), 0x950412deu);
        const quint32 n = word(8), originals = word(12), translations = word(16);
        QHash<QString, QString> wx;
        for (quint32 i = 0; i < n; ++i) {
            const QByteArray original(mo.constData() + word(originals + 8 * i + 4), word(originals + 8 * i));
            const QByteArray translated(mo.constData() + word(translations + 8 * i + 4), word(translations + 8 * i));
            const QString msgid = QString::fromUtf8(original.left(original.indexOf('\0') < 0 ? original.size() : original.indexOf('\0')));
            const QList<QByteArray> forms = translated.split('\0');
            for (qsizetype form = 0; form < forms.size(); ++form)
                if (!forms[form].isEmpty())
                    wx.insert(form == 0 ? msgid : msgid + QChar(char16_t(form)), QString::fromUtf8(forms[form]));
        }
        QTranslator translator;
        ASSERT_TRUE(translator.load(QStringLiteral(":/i18n/hikarisub_gettext_%1.qm").arg(language)));
        int translated = 0;
        for (const PoMessage &m : pot->messages) {
            if (m.msgid.isEmpty())
                continue;
            // A context entry is "ctxt\x04id" in the MO: no context-free lookup reaches it.
            const QString legacy = wx.value(m.msgid, m.msgid);
            const QString qt = translator.translate(gettextContext, m.msgid.toUtf8().constData());
            EXPECT_EQ(qt.isEmpty() ? m.msgid : qt, legacy) << language.toStdString() << " " << m.msgid.toStdString();
            translated += legacy != m.msgid;
        }
        EXPECT_GT(translated, 1700) << language.toStdString();
    }
#endif
}

// Qt's numerus rules for each catalog language against the legacy
// Plural-Forms, through the legacy plural entries compiled as Qt numerus
// messages, for 0/1/2/5/12/22: the rendered strings, not only the forms. An
// untranslated legacy entry renders msgid for 1 and msgid_plural otherwise
// (wx translation.h:305-307); in Qt that takes the English catalog installed
// below the language's one, since a numerus source alone has one form.
TEST(Qm, NumerusRulesPickTheLegacyPluralForm)
{
    const QString context = QString::fromLatin1(pluralProbeContext);
    QTranslator english;
    ASSERT_TRUE(english.load(QStringLiteral(HIKARI_PROBE_DIR "/legacy_plurals_en.qm")));
    for (const QString &language : QStringList(languages) << QStringLiteral("en")) {
        const QString file = language == u"en" ? QStringLiteral("template.pot") : language + QStringLiteral(".po");
        const auto po = parsePo(readFile(source(QStringLiteral("Locale/") + file)));
        ASSERT_TRUE(po.has_value());
        ASSERT_EQ(po->header(QStringLiteral("Plural-Forms")), legacyPluralForms.value(language)) << language.toStdString();
        QTranslator translator;
        ASSERT_TRUE(translator.load(QStringLiteral(HIKARI_PROBE_DIR "/legacy_plurals_%1.qm").arg(language)));
        int checked = 0, fallbacks = 0;
        for (const PoMessage &m : po->messages) {
            if (!m.plural())
                continue;
            const bool isTemplate = language == u"en";
            const bool untranslated = !isTemplate && std::any_of(m.msgstr.cbegin(), m.msgstr.cend(), [](const QString &f) { return f.isEmpty(); });
            if (!isTemplate && !untranslated)
                EXPECT_EQ(m.msgstr.size(), qtNumerusForms(language)) << language.toStdString();
            TsMessage probe;
            probe.source = QString(m.msgid).replace(QStringLiteral("%d"), QStringLiteral("%n"));
            probe.comment = m.msgctxt.value_or(QString());
            for (int n : counts) {
                const QString legacy = untranslated || isTemplate
                                           ? (n == 1 ? m.msgid : *m.msgidPlural)
                                           : m.msgstr.value(legacyForm(language, n));
                QString form = lookup(translator, context, probe, n);
                if (form.isEmpty()) {
                    form = lookup(english, context, probe, n);
                    ++fallbacks;
                }
                EXPECT_EQ(form.replace(QStringLiteral("%n"), QString::number(n)), QString(legacy).replace(QStringLiteral("%d"), QString::number(n)))
                    << language.toStdString() << " n=" << n << " " << m.msgctxt.value_or(QString()).toStdString();
                ++checked;
            }
        }
        EXPECT_EQ(checked, 3 * counts.size()) << language.toStdString();
        // Korean and Tamil leave all three plural entries untranslated at 20d647c4.
        const bool allUntranslated = language == u"ko_KR" || language == u"ta";
        EXPECT_EQ(fallbacks, allUntranslated ? checked : 0) << language.toStdString();
    }
    // The two font-count meanings answer differently where Polish differs.
    QTranslator pl;
    ASSERT_TRUE(pl.load(QStringLiteral(HIKARI_PROBE_DIR "/legacy_plurals_pl.qm")));
    EXPECT_EQ(pl.translate(gettextContext, "%n font", "found or copied", 1), QString());
    EXPECT_EQ(pl.translate(pluralProbeContext, "%n font", "found or copied", 1), QStringLiteral("%n czcionkę"));
    EXPECT_EQ(pl.translate(pluralProbeContext, "%n font", "not found or not copied", 1), QStringLiteral("%n czcionki"));
    EXPECT_EQ(pl.translate(pluralProbeContext, "%n font", "found or copied", 22), QStringLiteral("%n czcionki"));
    EXPECT_EQ(pl.translate(pluralProbeContext, "%n font", "found or copied", 12), QStringLiteral("%n czcionek"));
}

// The rewrite catalogs carry Qt's form count for their language on every
// numerus entry, as lupdate creates them.
TEST(Qm, RewriteNumerusEntriesHaveQtsFormCount)
{
    for (const QString &language : languages) {
        const TsCatalog catalog = readCatalog(source(QStringLiteral("i18n/hikarisub_%1.ts").arg(language)));
        int numerus = 0;
        for (const TsContext &context : catalog.contexts)
            for (const TsMessage &m : context.messages)
                if (m.numerus) {
                    EXPECT_EQ(m.translations.size(), qtNumerusForms(language)) << m.source.toStdString();
                    ++numerus;
                }
        EXPECT_GT(numerus, 0);
    }
}
