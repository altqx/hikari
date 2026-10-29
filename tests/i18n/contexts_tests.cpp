#include "hikari/i18n/contexts.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

using namespace hikari::i18n;

namespace {

QByteArray readSource(const QString &relative)
{
    QFile file(QStringLiteral(HIKARI_SOURCE_DIR "/") + relative);
    if (!file.open(QIODevice::ReadOnly))
        ADD_FAILURE() << "cannot read " << relative.toStdString();
    return file.readAll();
}

const ContextRegistry registry{{QStringLiteral("HikariSub.Editor"), QStringLiteral("The line editor")},
                               {QStringLiteral("HikariSub.Grid"), QStringLiteral("The grid")}};

TsMessage message(const QString &source, QList<std::pair<QString, int>> locations = {},
                  TsState state = TsState::Unfinished, QStringList translations = {QString()})
{
    TsMessage m;
    m.source = source;
    m.state = state;
    m.translations = translations;
    m.locations = locations;
    return m;
}

TsCatalog catalog(const QList<TsContext> &contexts)
{
    TsCatalog c;
    c.language = QStringLiteral("pl");
    c.contexts = contexts;
    return c;
}

bool mentions(const QStringList &problems, const QString &text)
{
    return std::ranges::any_of(problems, [&](const QString &p) { return p.contains(text); });
}

}

TEST(ContextRegistry, ReadsContextsAndRefusesBrokenFiles)
{
    const auto read = readContextRegistry("# comment\ncontext\tsurface\nHikariSub.Editor\tThe line editor\n");
    ASSERT_TRUE(read.has_value()) << read.error().toStdString();
    EXPECT_EQ(read->value(QStringLiteral("HikariSub.Editor")), QStringLiteral("The line editor"));
    EXPECT_FALSE(readContextRegistry("HikariSub.Editor\tx\n").has_value());
    EXPECT_FALSE(readContextRegistry("context\tsurface\nHikariSub.Editor\n").has_value());
    EXPECT_FALSE(readContextRegistry("context\tsurface\nA\tx\nA\ty\n").has_value());
    EXPECT_FALSE(readContextRegistry("context\tsurface\n").has_value());
}

TEST(ContextLint, FailsOnAnExtractedMessageWithoutASemanticContext)
{
    const TsCatalog extracted = catalog({{QStringLiteral("HikariSub.Editor"), {message(QStringLiteral("Comment"))}},
                                         {QStringLiteral("LineInspector"),
                                          {message(QStringLiteral("Layer"), {{QStringLiteral("src/ui/LineInspector.qml"), 12}})}},
                                         {QStringLiteral("hikari::ui::LineGrid"),
                                          {message(QStringLiteral("changed"), {{QStringLiteral("src/ui/line_grid.cpp"), 226}})}}});
    const QStringList problems = lintContexts(registry, extracted, {});
    ASSERT_EQ(problems.size(), 2) << problems.join(u'\n').toStdString();
    EXPECT_TRUE(mentions(problems, QStringLiteral("src/ui/LineInspector.qml:12: LineInspector \"Layer\"")));
    EXPECT_TRUE(mentions(problems, QStringLiteral("src/ui/line_grid.cpp:226: hikari::ui::LineGrid \"changed\"")));
    EXPECT_TRUE(lintContexts(registry, catalog({{QStringLiteral("HikariSub.Grid"), {message(QStringLiteral("x"))}}}), {}).isEmpty());
}

TEST(ContextLint, ChecksTheContextsSourcesName)
{
    const QList<SourceFile> clean{
        {QStringLiteral("src/ui/A.qml"),
         QStringLiteral("// tr(\"comment\")\npragma Translator: \"HikariSub.Editor\"\nimport QtQuick\nText { text: qsTr(\"a\") "
                        "+ qsTranslate(\"HikariSub.Grid\", \"b\") }\n")},
        {QStringLiteral("src/ui/a.cpp"),
         QStringLiteral("// made with tr() before O6\n/* QT_TR_NOOP(\"x\") */\nconst char *n = QT_TRANSLATE_NOOP(\"HikariSub.Grid\", "
                        "\"Start\");\nQString s = QCoreApplication::translate(\"HikariSub.Grid\", n) + attr(1) + "
                        "QStringLiteral(\"tr(\");\nint n = 1'000'000; char c = u8'0';\n"
                        "const char *r = R\"x(tr(\"y\") )\" )x\"; QString t = QObject::translate(\"HikariSub.Editor\", \"t\");\n")}};
    EXPECT_TRUE(lintContexts(registry, {}, clean).isEmpty()) << lintContexts(registry, {}, clean).join(u'\n').toStdString();

    const QList<SourceFile> bad{
        {QStringLiteral("src/ui/B.qml"), QStringLiteral("pragma Translator: \"Editor\"\nimport QtQuick\nText { text: qsTranslate(\"Main\", \"b\") }\n")},
        {QStringLiteral("src/ui/b.cpp"),
         QStringLiteral("QString a = tr(\"x\");\nQString b = QObject::tr(\"y\");\nconst char *c = QT_TR_NOOP(\"z\");\n"
                        "QString d = QCoreApplication::translate(\"DirectShowPlayer\", text);\n")}};
    const QStringList problems = lintContexts(registry, {}, bad);
    EXPECT_EQ(problems.size(), 7) << problems.join(u'\n').toStdString();
    EXPECT_TRUE(mentions(problems, QStringLiteral("src/ui/B.qml:1: pragma Translator names the unregistered context \"Editor\"")));
    EXPECT_TRUE(mentions(problems, QStringLiteral("src/ui/B.qml:3: qsTranslate names the unregistered context \"Main\"")));
    EXPECT_TRUE(mentions(problems, QStringLiteral("src/ui/b.cpp:1: tr()")));
    EXPECT_TRUE(mentions(problems, QStringLiteral("src/ui/b.cpp:2: tr()")));
    EXPECT_TRUE(mentions(problems, QStringLiteral("src/ui/b.cpp:3: QT_TR_NOOP()")));
    EXPECT_TRUE(mentions(problems, QStringLiteral("src/ui/b.cpp:4: translate names the unregistered context \"DirectShowPlayer\"")));
    EXPECT_TRUE(mentions(problems, QStringLiteral("src/ui/b.cpp:4: translate looks up a computed text in \"DirectShowPlayer\"")));
}

// lupdate extracts nothing from a computed-text lookup, so its texts must be marked in the same file.
TEST(ContextLint, AComputedTextNeedsItsContextMarkedInTheFile)
{
    const QList<SourceFile> clean{
        {QStringLiteral("src/ui/table.cpp"),
         QStringLiteral("static const char *const names[] = {QT_TRANSLATE_NOOP(\"HikariSub.Grid\", \"L.\")};\n"
                        "QString a = QCoreApplication::translate(\"HikariSub.Grid\", names[0]);\n"
                        "QString b = QCoreApplication::translate(\"HikariSub.Editor\",\n    \"a\" \"b\");\n"
                        "QString c = QCoreApplication::translate(\"HikariSub.Editor\", \"%n\", nullptr, 2);\n"
                        "// QCoreApplication::translate(\"HikariSub.Editor\", text) in a comment\n"
                        "QString d = QStringLiteral(\"translate(\\\"HikariSub.Editor\\\", text)\");\n")},
        {QStringLiteral("src/ui/T.qml"),
         QStringLiteral("import QtQuick\nText {\n    readonly property var names: [qsTranslateNoOp(\"HikariSub.Grid\", \"End\")]\n"
                        "    // qsTranslate(\"HikariSub.Editor\", names[0])\n"
                        "    text: qsTranslate(\"HikariSub.Grid\", names[0]) + qsTranslate(\"HikariSub.Editor\", \"x\")\n}\n")}};
    EXPECT_TRUE(lintContexts(registry, {}, clean).isEmpty()) << lintContexts(registry, {}, clean).join(u'\n').toStdString();

    const QList<SourceFile> bad{
        {QStringLiteral("src/app/player.cpp"),
         QStringLiteral("static const char *const kMessages[] = {QT_TRANSLATE_NOOP(\"HikariSub.Editor\", \"Cannot open\")};\n"
                        "void failed(const std::string &message)\n{\n"
                        "    log(QCoreApplication::translate(\"HikariSub.Grid\", message.c_str()));\n}\n")},
        {QStringLiteral("src/ui/U.qml"),
         QStringLiteral("import QtQuick\nText { text: qsTranslate(\"HikariSub.Grid\", model.name) }\n")}};
    const QStringList problems = lintContexts(registry, {}, bad);
    EXPECT_EQ(problems.size(), 2) << problems.join(u'\n').toStdString();
    EXPECT_TRUE(mentions(problems, QStringLiteral("src/app/player.cpp:4: translate looks up a computed text in \"HikariSub.Grid\"")));
    EXPECT_TRUE(mentions(problems, QStringLiteral("src/ui/U.qml:2: qsTranslate looks up a computed text in \"HikariSub.Grid\"")));
}

TEST(CheckedIn, CatalogsUseTheRegisteredContexts)
{
    const auto contexts = readContextRegistry(readSource(QStringLiteral("i18n/contexts.tsv")));
    ASSERT_TRUE(contexts.has_value()) << contexts.error().toStdString();
    EXPECT_FALSE(contexts->contains(QStringLiteral("HikariSub.Automation.Gettext")));
    for (auto it = contexts->cbegin(); it != contexts->cend(); ++it)
        EXPECT_TRUE(it.key().startsWith(QStringLiteral("HikariSub.")) || it.key() == u"QGuiApplication") << it.key().toStdString();
    for (const char *file : {"i18n/hikarisub_pl.ts", "i18n/hikarisub_ko_KR.ts", "i18n/hikarisub_th_TH.ts",
                             "i18n/hikarisub_ta.ts"}) {
        const auto catalog = readTs(readSource(QLatin1String(file)));
        ASSERT_TRUE(catalog.has_value()) << file;
        for (const TsContext &c : catalog->contexts)
            EXPECT_TRUE(contexts->contains(c.name)) << file << ": " << c.name.toStdString();
    }
}

TEST(CheckedIn, CatalogsHoldTheGridHeadings)
{
    const QStringList headings{QStringLiteral("L."),     QStringLiteral("Start"), QStringLiteral("End"),
                               QStringLiteral("Styles"), QStringLiteral("Actor"), QStringLiteral("Left"),
                               QStringLiteral("Right"),  QStringLiteral("Vert."), QStringLiteral("Effect"),
                               QStringLiteral("CPS"),    QStringLiteral("Wraps"), QStringLiteral("Text"),
                               QStringLiteral("Original text"), QStringLiteral("Translation")};
    for (const char *file : {"i18n/hikarisub_pl.ts", "i18n/hikarisub_ko_KR.ts", "i18n/hikarisub_th_TH.ts",
                             "i18n/hikarisub_ta.ts"}) {
        const auto catalog = readTs(readSource(QLatin1String(file)));
        ASSERT_TRUE(catalog.has_value()) << file;
        const auto grid = std::ranges::find(catalog->contexts, QStringLiteral("HikariSub.Grid"), &TsContext::name);
        ASSERT_NE(grid, catalog->contexts.end()) << file;
        for (const QString &heading : headings) {
            const auto key = std::ranges::find_if(grid->messages, [&](const TsMessage &m) {
                return m.source == heading && m.comment.isEmpty() && !m.numerus;
            });
            ASSERT_NE(key, grid->messages.end()) << file << ": HikariSub.Grid \"" << heading.toStdString() << '"';
            EXPECT_EQ(key->state, TsState::Finished) << file << ": HikariSub.Grid \"" << heading.toStdString() << '"';
            EXPECT_FALSE(key->translations.value(0).isEmpty()) << file << ": " << heading.toStdString();
        }
        EXPECT_FALSE(std::ranges::any_of(grid->messages, [](const TsMessage &m) { return m.source == u"#"; })) << file;
    }
}

TEST(CheckedIn, InterfaceCopyIsSentenceCase)
{
    const auto gettext = readTs(readSource(QStringLiteral("i18n/hikarisub_gettext_pl.ts")));
    ASSERT_TRUE(gettext.has_value());
    QSet<QString> legacy;
    for (const TsContext &context : gettext->contexts)
        for (const TsMessage &m : context.messages)
            legacy.insert(m.source);
    const auto catalog = readTs(readSource(QStringLiteral("i18n/hikarisub_pl.ts")));
    ASSERT_TRUE(catalog.has_value());
    static const QRegularExpression noun(QStringLiteral("\\b(Lines?|Documents?|Untitled)\\b"));
    int checked = 0;
    for (const TsContext &context : catalog->contexts)
        for (const TsMessage &m : context.messages) {
            if (m.state == TsState::Vanished || m.state == TsState::Obsolete
                || legacy.contains(m.source))
                continue;
            ++checked;
            for (auto it = noun.globalMatch(m.source); it.hasNext();) {
                const auto match = it.next();
                QString before = m.source.left(match.capturedStart());
                while (before.endsWith(u' '))
                    before.chop(1);
                const bool sentenceStart = before.isEmpty() || QStringLiteral(".!?:\n").contains(before.back());
                EXPECT_TRUE(sentenceStart) << context.name.toStdString() << ": \"" << m.source.toStdString() << '"';
            }
        }
    EXPECT_GT(checked, 300);
}

TEST(CheckedIn, TranslationsKeepTheSourcesPlaceholders)
{
    static const QRegularExpression marker(QStringLiteral("%L?(\\d+)"));
    const auto markers = [](const QString &text) {
        QStringList out;
        for (auto it = marker.globalMatch(text); it.hasNext();)
            out << it.next().captured(1);
        out.sort();
        return out;
    };
    for (const char *file : {"i18n/hikarisub_pl.ts", "i18n/hikarisub_ko_KR.ts", "i18n/hikarisub_th_TH.ts",
                             "i18n/hikarisub_ta.ts"}) {
        const auto catalog = readTs(readSource(QLatin1String(file)));
        ASSERT_TRUE(catalog.has_value()) << file;
        int checked = 0;
        for (const TsContext &context : catalog->contexts)
            for (const TsMessage &m : context.messages)
                if (!m.numerus && !m.translations.value(0).isEmpty()) {
                    EXPECT_EQ(markers(m.translations.value(0)), markers(m.source)) << file << ": " << m.source.toStdString();
                    ++checked;
                }
        EXPECT_GT(checked, 700) << file;
    }
}

// lupdate on Linux never reads the Windows-only DirectShow adapter, so application_directshow.cpp marks its texts.
TEST(CheckedIn, DirectShowOpenMessagesAreMarked)
{
    const QString adapter = QString::fromUtf8(readSource(QStringLiteral("src/backends/directshow/directshow_player.cpp")));
    const QString app = QString::fromUtf8(readSource(QStringLiteral("src/app/application_directshow.cpp")));
    QSet<QString> reported;
    static const QRegularExpression hr(QStringLiteral(R"re(W1_HR\([^;]*?,\s*"([^"\\]+)"\s*\)\s*;)re"),
                                       QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression assigned(QStringLiteral(R"re(\berror\s*=\s*"([^"\\]+)")re"));
    for (const auto *pattern : {&hr, &assigned})
        for (auto it = pattern->globalMatch(adapter); it.hasNext();)
            reported.insert(it.next().captured(1));
    EXPECT_GE(reported.size(), 20);
    QSet<QString> marked;
    static const QRegularExpression mark(QStringLiteral(R"re(QT_TRANSLATE_NOOP\("HikariSub\.Video",\s*"([^"\\]+)"\))re"));
    for (auto it = mark.globalMatch(app); it.hasNext();)
        marked.insert(it.next().captured(1));
    for (const QString &text : reported)
        EXPECT_TRUE(marked.contains(text)) << "\"" << text.toStdString() << "\" is not marked in application_directshow.cpp";
}
