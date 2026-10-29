// O4: the QM the build compiles from the checked-in TS (embedded under
// :/i18n by hikari_translations) answer exactly the finished entries, a
// numerus one in the legacy plural form for each count, and the gettext
// catalogs their finished entries.

#include "hikari/i18n/ts.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTranslator>

using namespace hikari::i18n;

namespace {

// The context of the aegisub.gettext compatibility entries (Localisation::kGettextContext).
constexpr auto gettextContext = "HikariSub.Automation.Gettext";
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

// The legacy Plural-Forms of each catalog (the same at 20d647c4 and 0.0.1), as gettext and wx
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
        int finished = 0;
        for (const TsContext &context : catalog.contexts)
            for (const TsMessage &m : context.messages) {
                if (m.state == TsState::Vanished || m.state == TsState::Obsolete)
                    continue;
                const bool done = m.state == TsState::Finished;
                // A numerus entry answers the form Qt's rule picks for each
                // count (the legacy Plural-Forms' form, see
                // RewriteNumerusKeysRenderTheLegacyPluralForm); a plain one its text.
                for (int n : m.numerus ? counts : QList<int>{-1}) {
                    const QString expected = done ? m.translations.value(n < 0 ? 0 : legacyForm(language, n)) : QString();
                    EXPECT_EQ(lookup(translator, context.name, m, n), expected)
                        << language.toStdString() << " " << context.name.toStdString() << " " << m.source.toStdString() << " n=" << n;
                }
                finished += done;
            }
        EXPECT_GT(finished, 700) << language.toStdString();
    }
    QTranslator pl;
    ASSERT_TRUE(pl.load(QStringLiteral(":/i18n/hikarisub_pl.qm")));
    EXPECT_EQ(pl.translate("HikariSub.Audio", "Horizontal stretching"), QStringLiteral("Rozciągnięcie w poziomie"));
    EXPECT_EQ(pl.translate("HikariSub.Audio", "Search bar"), QString()); // #385: the tooltip went, vanished
    EXPECT_EQ(pl.translate("HikariSub.Styles", "Copy of "), QStringLiteral("Kopia "));
}

// #385: the English catalog holds only numerus entries, all finished, with
// the singular for 1 and the plural otherwise; no form keeps the source's
// "(s)" for its own count.
TEST(Qm, EnglishCatalogGivesTheNumerusForms)
{
    QTranslator en;
    ASSERT_TRUE(en.load(QStringLiteral(":/i18n/hikarisub_en.qm")));
    EXPECT_EQ(en.language(), QStringLiteral("en"));
    const TsCatalog catalog = readCatalog(source(QStringLiteral("i18n/hikarisub_en.ts")));
    int numerus = 0;
    for (const TsContext &context : catalog.contexts)
        for (const TsMessage &m : context.messages) {
            ASSERT_TRUE(m.numerus) << m.source.toStdString();
            ASSERT_EQ(m.state, TsState::Finished) << m.source.toStdString();
            ASSERT_EQ(m.translations.size(), 2) << m.source.toStdString();
            for (const QString &form : m.translations)
                EXPECT_FALSE(QString(form).remove(QStringLiteral("%1 karaoke line(s)")).contains(QStringLiteral("(s)")))
                    << form.toStdString();
            for (int n : counts)
                EXPECT_EQ(lookup(en, context.name, m, n), m.translations.value(n == 1 ? 0 : 1))
                    << m.source.toStdString() << " n=" << n;
            ++numerus;
        }
    // 79 with KP1's "%n generated lines", UD1's three user folder notices and
    // GR1's rule count, ignored group's toast and the walk's two line counts.
    // 81 with the freeze report's and the freeze notice's seconds (CR1).
    EXPECT_EQ(numerus, 81);
    EXPECT_EQ(en.translate("HikariSub.Spelling", "Remove %n word(s)", nullptr, 1), QStringLiteral("Remove %n word"));
    EXPECT_EQ(en.translate("HikariSub.Spelling", "Remove %n word(s)", nullptr, 3), QStringLiteral("Remove %n words"));
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
        EXPECT_EQ(translator.translate("HikariSub.Audio", "Search bar"), QString());
    }
    QTranslator pl;
    ASSERT_TRUE(pl.load(QStringLiteral(":/i18n/hikarisub_gettext_pl.qm")));
    EXPECT_EQ(pl.translate(gettextContext, "Search bar"), QStringLiteral("Pasek szukania"));
    EXPECT_EQ(pl.translate(gettextContext, "%d element"), QStringLiteral("%d element")); // msgstr[0], printf kept
}

// The rewrite's own numerus keys through lrelease and QTranslator: every
// numerus entry of each checked-in catalog given distinct forms and marked
// finished (the checked-in ones are all unfinished until translated), so the
// form each count renders is visible. The legacy rendering is the form the
// legacy Plural-Forms picks for that count (gettext and wx, legacyForm).
TEST(Qm, RewriteNumerusKeysRenderTheLegacyPluralForm)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    for (const QString &language : languages) {
        TsCatalog catalog = readCatalog(source(QStringLiteral("i18n/hikarisub_%1.ts").arg(language)));
        int numerus = 0;
        for (TsContext &context : catalog.contexts)
            for (TsMessage &m : context.messages)
                if (m.numerus) {
                    for (qsizetype form = 0; form < m.translations.size(); ++form)
                        m.translations[form] = QStringLiteral("[%1] %2").arg(form).arg(m.source);
                    m.state = TsState::Finished;
                    ++numerus;
                }
        // 13 since the UI polish: the batch picker's tooltip and description
        // came, its untranslated "Targets: %n picked line(s)" went. 14 with
        // the Home page's "%n documents" (SH2). 16 with the Automation
        // panel's script offer, "… and %n more" and its tooltip's (AM1); 19
        // with the karaoke templater's report, its summary, its problems and
        // its template Lines (KE1). 20 with the inspector bar's picked count
        // beside its picker, "%n lines" (VT1). 23 with the popup kit's "%n
        // more notice(s)" and the style copy's count and clash summary (PK1);
        // 24 with What's new's "And %n earlier versions" (SH3). 25 with the
        // karaoke check's problems not listed (KA7). 26 with the notices' "%n
        // value(s) could not be scaled." (NT1; its "%n style(s) deleted"
        // gave way to SY1's toast). 27 with Hydra's "Changes %n lines" (HY1).
        // 36 with the file and session popups' counts: the close review's
        // three questions, the autosaves, the startup offer's four and Close
        // all tabs (PU1). 41 with the import wizard's "Import %n setting(s)"
        // and "%n setting(s) imported." and the hotkeys import's three counts
        // (PU7). 46 with a macro run's lines, its template problems, the
        // card's log and a run's line count (AD3). 56 with the font picker's
        // counts, the catalog load's accept button, summary and its four
        // parts, and the fonts added from subtitles and their open files (PU3).
        // 61 with the Style manager's rows, multi-style preview, rename, apply
        // and catalog questions and its clean-up toast (SY1).
        // 62 with the added words window's "Remove %n word(s)" (PU6). 65 with
        // Edit shapes' and Edit tags' restore questions and the tags' count,
        // "The tag takes %n value(s)" (PU5). 66 with the Kanji timer's "Apply the
        // kanji timing of %n lines?" (KA3). 71 with the Grid's folded run,
        // "%n generated lines" (KP1), the grammar check's rule groups, its
        // ignored group's toast and the walk's two line counts (GR1). 73 with
        // the freeze report's and the freeze notice's seconds (CR1).
        ASSERT_EQ(numerus, 73) << language.toStdString();
        const QString ts = dir.filePath(QStringLiteral("numerus_%1.ts").arg(language));
        const QString qm = dir.filePath(QStringLiteral("numerus_%1.qm").arg(language));
        QFile file(ts);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(writeTs(catalog));
        file.close();
        QProcess lrelease;
        lrelease.start(QStringLiteral(HIKARI_LRELEASE), {QStringLiteral("-silent"), ts, QStringLiteral("-qm"), qm});
        ASSERT_TRUE(lrelease.waitForFinished(60000));
        ASSERT_EQ(lrelease.exitCode(), 0) << lrelease.readAllStandardError().toStdString();
        QTranslator translator;
        ASSERT_TRUE(translator.load(qm));
        for (const TsContext &context : catalog.contexts)
            for (const TsMessage &m : context.messages)
                if (m.numerus)
                    for (int n : counts)
                        EXPECT_EQ(lookup(translator, context.name, m, n), QStringLiteral("[%1] %2").arg(legacyForm(language, n)).arg(m.source))
                            << language.toStdString() << " n=" << n << " " << m.source.toStdString();
    }
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
