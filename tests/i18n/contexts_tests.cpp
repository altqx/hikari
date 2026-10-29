// O6 (#205): semantic translation contexts (docs/localisation.md, "Keys
// and migration"). The lint over small fixtures, the record of the move from
// lupdate's default contexts (pairing, escaping, joining entries, the
// comparison), and the checked-in registry, map and catalogs.

#include "hikari/i18n/contexts.h"
#include "hikari/i18n/migration.h"

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

} // namespace

TEST(ContextRegistry, ReadsContextsAndRefusesBrokenFiles)
{
    const auto read = readContextRegistry("# comment\ncontext\tsurface\nHikariSub.Editor\tThe line editor\n");
    ASSERT_TRUE(read.has_value()) << read.error().toStdString();
    EXPECT_EQ(read->value(QStringLiteral("HikariSub.Editor")), QStringLiteral("The line editor"));
    EXPECT_FALSE(readContextRegistry("HikariSub.Editor\tx\n").has_value());                             // no header
    EXPECT_FALSE(readContextRegistry("context\tsurface\nHikariSub.Editor\n").has_value());              // no surface
    EXPECT_FALSE(readContextRegistry("context\tsurface\nA\tx\nA\ty\n").has_value());                    // twice
    EXPECT_FALSE(readContextRegistry("context\tsurface\n").has_value());                                // empty
}

// A message lupdate keys by a QML file or a C++ class fails, with where it is.
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

// Sources: literal contexts and pragmas must be registered (also a runtime
// lookup lupdate cannot extract), and C++ never takes the class's context.
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

// A lookup of a computed text (translate or qsTranslate with a non-literal
// source) needs its texts marked for the same context in the same file:
// lupdate extracts nothing from the lookup itself, so an unmarked table never
// reaches the catalogs (as the Grid's headings did not before O6).
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

// Calls pair by file and line order; a key now used by two surfaces moves to
// both, and a catalog entry without a call follows most of its context.
TEST(ContextMap, PairsCallsInLineOrderAndMovesEntriesWithoutACall)
{
    const QString file = QStringLiteral("src/ui/Main.qml");
    const TsCatalog before = catalog(
        {{QStringLiteral("Main"),
          {message(QStringLiteral("Cancel"), {{file, 10}, {file, 90}}), message(QStringLiteral("Line"), {{file, 50}})}},
         {QStringLiteral("DockTitleBar"), {message(QStringLiteral("Cancel"), {{QStringLiteral("src/ui/DockTitleBar.qml"), 3}})}}});
    const TsCatalog after = catalog(
        {{QStringLiteral("HikariSub.Editor"), {message(QStringLiteral("Cancel"), {{file, 11}})}},
         {QStringLiteral("HikariSub.Grid"),
          {message(QStringLiteral("Cancel"), {{file, 91}, {QStringLiteral("src/ui/DockTitleBar.qml"), 4}}),
           message(QStringLiteral("Line"), {{file, 51}})}}});
    const TsCatalog translations = catalog({{QStringLiteral("Main"), {message(QStringLiteral("Gone"), {}, TsState::Vanished)}}});
    const auto map = buildContextMap(before, after, {translations});
    ASSERT_TRUE(map.has_value()) << map.error().toStdString();
    QStringList rows;
    for (const ContextMove &m : map->moves)
        rows << QStringLiteral("%1/%2->%3 %4").arg(m.from, m.source, m.to, m.basis);
    EXPECT_EQ(rows, (QStringList{QStringLiteral("DockTitleBar/Cancel->HikariSub.Grid extracted"),
                                 QStringLiteral("Main/Cancel->HikariSub.Editor extracted"),
                                 QStringLiteral("Main/Cancel->HikariSub.Grid extracted"),
                                 QStringLiteral("Main/Line->HikariSub.Grid extracted"),
                                 QStringLiteral("Main/Gone->HikariSub.Grid context")}));
    // A call added or removed is not a move.
    TsCatalog fewer = after;
    fewer.contexts[0].messages[0].locations.clear();
    EXPECT_FALSE(buildContextMap(before, fewer, {}).has_value());
}

TEST(ContextMap, RoundTripsThroughItsFile)
{
    ContextMap map;
    map.moves.append({QStringLiteral("Main"), QStringLiteral("Two\nlines\tand \\"), QStringLiteral("meaning"), true,
                      QStringLiteral("HikariSub.Grid"), QStringLiteral("extracted")});
    map.moves.append({QStringLiteral("Main"), QStringLiteral("Gone"), QString(), false, QStringLiteral("HikariSub.Editor"),
                      QStringLiteral("context")});
    const auto read = readContextMap(writeContextMap(map));
    ASSERT_TRUE(read.has_value()) << read.error().toStdString();
    ASSERT_EQ(read->moves.size(), 2);
    EXPECT_EQ(read->moves[0].source, map.moves[0].source);
    EXPECT_EQ(read->moves[0].comment, QStringLiteral("meaning"));
    EXPECT_TRUE(read->moves[0].numerus);
    EXPECT_EQ(read->moves[1].basis, QStringLiteral("context"));
    EXPECT_FALSE(readContextMap("from\tsource\tcomment\tnumerus\tto\tbasis\nMain\tx\t\tmaybe\tA\textracted\n").has_value());
}

// Entries move to every new key; entries that meet must agree, except that
// a finished translation absorbs an unfinished suggestion of the same text.
TEST(Recontext, MovesJoinsAndRefusesDisagreeingEntries)
{
    ContextMap map;
    const auto move = [&](const char *from, const char *source, const char *to) {
        map.moves.append({QLatin1String(from), QLatin1String(source), QString(), false, QLatin1String(to), QStringLiteral("extracted")});
    };
    move("Main", "Cancel", "HikariSub.Editor");
    move("Main", "Cancel", "HikariSub.Grid");
    move("DockTitleBar", "Cancel", "HikariSub.Grid");
    move("Main", "Pause", "HikariSub.Grid");
    move("VideoMenu", "Pause", "HikariSub.Grid");
    const TsCatalog before = catalog(
        {{QStringLiteral("Main"),
          {message(QStringLiteral("Cancel"), {}, TsState::Finished, {QStringLiteral("Anuluj")}),
           message(QStringLiteral("Pause"), {}, TsState::Finished, {QStringLiteral("Pauza")})}},
         {QStringLiteral("DockTitleBar"), {message(QStringLiteral("Cancel"), {}, TsState::Finished, {QStringLiteral("Anuluj")})}},
         {QStringLiteral("VideoMenu"), {message(QStringLiteral("Pause"), {}, TsState::Unfinished, {QStringLiteral("Pauza")})}},
         {QStringLiteral("HikariSub.TagList"), {message(QStringLiteral("Tag"))}}});
    const auto after = recontext(before, map);
    ASSERT_TRUE(after.has_value()) << after.error().toStdString();
    QStringList names;
    for (const TsContext &c : after->contexts)
        names << c.name;
    EXPECT_EQ(names, (QStringList{QStringLiteral("HikariSub.Editor"), QStringLiteral("HikariSub.Grid"), QStringLiteral("HikariSub.TagList")}));
    const TsMessage *pause = after->find(QStringLiteral("HikariSub.Grid"), QStringLiteral("Pause"), {});
    ASSERT_NE(pause, nullptr);
    EXPECT_EQ(pause->state, TsState::Finished);
    EXPECT_EQ(after->contexts[1].messages.size(), 2);

    const RecontextComparison c = compareRecontexted(before, *after, map);
    EXPECT_TRUE(c.problems.isEmpty()) << c.problems.join(u'\n').toStdString();
    EXPECT_EQ(c.entriesBefore, 5);
    EXPECT_EQ(c.entriesAfter, 4);
    EXPECT_EQ(c.joinedKeys, 2);
    EXPECT_EQ(c.promoted, QStringList{QStringLiteral("HikariSub.Grid \"Pause\" (was VideoMenu)")});
    EXPECT_EQ(c.distinctTranslatedBefore, 2);
    EXPECT_EQ(c.distinctTranslatedAfter, 2);

    // Different translations at one key, and an old context's entry without a row.
    TsCatalog clash = before;
    clash.contexts[1].messages[0].translations = {QStringLiteral("Zamknij")};
    EXPECT_FALSE(recontext(clash, map).has_value());
    TsCatalog unmapped = before;
    unmapped.contexts[0].messages.append(message(QStringLiteral("Other")));
    EXPECT_FALSE(recontext(unmapped, map).has_value());
}

// The comparison names a lost, changed or added entry.
TEST(Recontext, ComparisonNamesEveryDifference)
{
    ContextMap map;
    map.moves.append({QStringLiteral("Main"), QStringLiteral("Cancel"), QString(), false, QStringLiteral("HikariSub.Grid"),
                      QStringLiteral("extracted")});
    map.moves.append({QStringLiteral("Main"), QStringLiteral("Close"), QString(), false, QStringLiteral("HikariSub.Grid"),
                      QStringLiteral("extracted")});
    const TsCatalog before = catalog({{QStringLiteral("Main"),
                                       {message(QStringLiteral("Cancel"), {}, TsState::Finished, {QStringLiteral("Anuluj")}),
                                        message(QStringLiteral("Close"), {}, TsState::Finished, {QStringLiteral("Zamknij")})}}});
    const TsCatalog after = catalog({{QStringLiteral("HikariSub.Grid"),
                                      {message(QStringLiteral("Cancel"), {}, TsState::Finished, {QStringLiteral("Anuluj!")}),
                                       message(QStringLiteral("New"), {}, TsState::Unfinished, {QStringLiteral("Nowy")})}}});
    const RecontextComparison c = compareRecontexted(before, after, map);
    EXPECT_TRUE(mentions(c.problems, QStringLiteral("changed: HikariSub.Grid \"Cancel\" (was Main)")));
    EXPECT_TRUE(mentions(c.problems, QStringLiteral("lost: HikariSub.Grid \"Close\" (was Main)")));
    EXPECT_TRUE(mentions(c.problems, QStringLiteral("added: HikariSub.Grid \"New\"")));
}

// The checked-in record: the registry reads, every move goes to a registered
// context, and the catalogs and keys hold registered contexts only (the
// gettext catalogs: their own context alone).
TEST(CheckedIn, CatalogsUseTheRegisteredContexts)
{
    const auto contexts = readContextRegistry(readSource(QStringLiteral("i18n/contexts.tsv")));
    ASSERT_TRUE(contexts.has_value()) << contexts.error().toStdString();
    EXPECT_FALSE(contexts->contains(QStringLiteral("HikariSub.Automation.Gettext")));
    for (auto it = contexts->cbegin(); it != contexts->cend(); ++it)
        EXPECT_TRUE(it.key().startsWith(QStringLiteral("HikariSub.")) || it.key() == u"QGuiApplication") << it.key().toStdString();
    const auto map = readContextMap(readSource(QStringLiteral("i18n/migration/contexts.tsv")));
    ASSERT_TRUE(map.has_value()) << map.error().toStdString();
    EXPECT_GT(map->moves.size(), 1800);
    for (const ContextMove &m : map->moves) {
        EXPECT_TRUE(contexts->contains(m.to)) << m.to.toStdString();
        // HikariSub.TagList was semantic before O6 (tag_list_controller.cpp's
        // QT_TRANSLATE_NOOP table): its keys stay where they were.
        EXPECT_TRUE(!contexts->contains(m.from) || m.from == m.to) << m.from.toStdString();
    }
    for (const char *file : {"i18n/hikarisub_pl.ts", "i18n/hikarisub_ko_KR.ts", "i18n/hikarisub_th_TH.ts",
                             "i18n/hikarisub_ta.ts", "i18n/migration/keys.ts"}) {
        const auto catalog = readTs(readSource(QLatin1String(file)));
        ASSERT_TRUE(catalog.has_value()) << file;
        for (const TsContext &c : catalog->contexts)
            EXPECT_TRUE(contexts->contains(c.name)) << file << ": " << c.name.toStdString();
    }
}

// The Grid's column headings (line_table_model.cpp's QT_TRANSLATE_NOOP table,
// legacy SubsGridWindow.cpp:310-330) are HikariSub.Grid keys in every
// catalog. They were a table of plain literals translated at run time, which
// lupdate never extracted, so no catalog had "Vert.", "CPS" or "Wraps". "#"
// is not a key: legacy never translates it. The headings O4 had no key for
// carry legacy's translation of the same id (keymap.tsv's exact rows, which
// CheckedIn.ConversionReproducesTheCatalogs checks), finished: every
// language's PO translates each of them.
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

// The rewrite's own copy is in sentence case (docs/ux/visual-language.md,
// "Sentence case"): "line", "document" and "untitled" are ordinary words
// inside a sentence ("1 line selected", "Follow the editing line"), not the
// glossary's capitalized terms. Legacy's own ids (keymap.tsv's exact and
// placeholder rows) keep their text: their translations depend on it.
TEST(CheckedIn, InterfaceCopyIsSentenceCase)
{
    const auto map = readKeyMap(readSource(QStringLiteral("i18n/migration/keymap.tsv")));
    ASSERT_TRUE(map.has_value()) << map.error().toStdString();
    QSet<QString> legacy;
    for (const KeyMapRow &row : map->rows)
        if ((row.status == MapStatus::Exact || row.status == MapStatus::Placeholder) && row.context)
            legacy.insert(*row.context + QChar(0x1f) + row.source);
    const auto catalog = readTs(readSource(QStringLiteral("i18n/hikarisub_pl.ts")));
    ASSERT_TRUE(catalog.has_value());
    static const QRegularExpression noun(QStringLiteral("\\b(Lines?|Documents?|Untitled)\\b"));
    int checked = 0;
    for (const TsContext &context : catalog->contexts)
        for (const TsMessage &m : context.messages) {
            if (m.state == TsState::Vanished || m.state == TsState::Obsolete
                || legacy.contains(context.name + QChar(0x1f) + m.source))
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

// The DirectShow adapter (Windows only, so no lupdate run on Linux reads its
// sources) reports a failed open step as legacy's HR text, and the
// application translates it through its HikariSub.Video table
// (application_directshow.cpp's kOpenMessages). Every text the adapter can
// report must be marked there, or it would stay untranslated.
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
