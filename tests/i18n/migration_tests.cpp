// O4: the PO-to-TS migration (docs/qt/localisation.md, "Keys and migration")
// over small fixtures and over the checked-in inputs: Locale/*.po at the
// recorded commit, i18n/migration/keys.ts and the reviewed keymap.tsv.

#include "hikari/i18n/migration.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <array>

using namespace hikari::i18n;

namespace {

QByteArray readSource(const QString &relative)
{
    QFile file(QStringLiteral(HIKARI_SOURCE_DIR "/") + relative);
    if (!file.open(QIODevice::ReadOnly))
        ADD_FAILURE() << "cannot read " << relative.toStdString();
    return file.readAll();
}

// Byte equality, reporting the first differing line instead of every byte.
testing::AssertionResult sameBytes(const QByteArray &actual, const QByteArray &expected)
{
    if (actual == expected)
        return testing::AssertionSuccess();
    const QList<QByteArray> a = actual.split('\n'), e = expected.split('\n');
    for (qsizetype i = 0; i < std::max(a.size(), e.size()); ++i)
        if (a.value(i) != e.value(i))
            return testing::AssertionFailure() << "line " << i + 1 << ": got \"" << a.value(i).toStdString()
                                               << "\", expected \"" << e.value(i).toStdString() << '"';
    return testing::AssertionFailure() << "differs";
}

const QStringList languages = {QStringLiteral("pl"), QStringLiteral("ko_KR"), QStringLiteral("th_TH"), QStringLiteral("ta")};

QList<LanguageInput> legacyInputs()
{
    QList<LanguageInput> out;
    for (const QString &language : languages)
        out.append({language, language + QStringLiteral(".po"), readSource(QStringLiteral("Locale/%1.po").arg(language))});
    return out;
}

PoCatalog po(const char *text)
{
    auto catalog = parsePo(QByteArray(text));
    EXPECT_TRUE(catalog.has_value()) << (catalog ? "" : catalog.error().toStdString());
    return catalog.value_or(PoCatalog{});
}

TsCatalog keys(const QList<std::pair<QString, QList<TsMessage>>> &contexts)
{
    TsCatalog catalog;
    for (const auto &[name, messages] : contexts)
        catalog.contexts.append({name, messages});
    return catalog;
}

TsMessage key(const QString &source, const QString &comment = {}, bool numerus = false)
{
    TsMessage m;
    m.source = source;
    m.comment = comment;
    m.numerus = numerus;
    m.translations = {QString()};
    return m;
}

const char *fixturePot = R"(# Template.
msgid ""
msgstr ""
"Content-Type: text/plain; charset=UTF-8\n"
"Plural-Forms: nplurals=2; plural=(n != 1);\n"

#: a.cpp:1
msgid "Cancel"
msgstr ""

#: a.cpp:2
#, c-format
msgid "Rule \"%s\" is invalid."
msgstr ""

#: a.cpp:3
msgid "50% d"
msgstr ""

#: a.cpp:4
#, c-format
msgctxt "found or copied"
msgid "%d font"
msgid_plural "%d fonts"
msgstr[0] ""
msgstr[1] ""

#: a.cpp:5
#, c-format
msgctxt "not found or not copied"
msgid "%d font"
msgid_plural "%d fonts"
msgstr[0] ""
msgstr[1] ""

#: a.cpp:6
#, c-format
msgid "%d element"
msgid_plural "%d elements"
msgstr[0] ""
msgstr[1] ""

#: a.cpp:7
msgid "Only legacy"
msgstr ""

#: a.cpp:8
msgid ""
"Two\n"
"lines"
msgstr ""
)";

const char *fixturePl = R"(# Polish translation.
# A translator <t@example.org>, 2022.
msgid ""
msgstr ""
"Last-Translator: altqx <al@altqx.com>\n"
"Language-Team: Polish\n"
"Language: pl\n"
"Content-Type: text/plain; charset=UTF-8\n"
"Plural-Forms: nplurals=3; plural=(n==1 ? 0 : n%10>=2 && n%10<=4 && (n%100<10 || n%100>=20) ? 1 : 2);\n"

#: a.cpp:1
msgid "Cancel"
msgstr "Anuluj"

#: a.cpp:2
#, c-format
msgid "Rule \"%s\" is invalid."
msgstr "Regu\305\202a \"%s\" jest nieprawid\305\202owa."

#: a.cpp:3
#, fuzzy
msgid "50% d"
msgstr "50% d?"

#: a.cpp:4
#, c-format
msgctxt "found or copied"
msgid "%d font"
msgid_plural "%d fonts"
msgstr[0] "%d czcionk\304\231"
msgstr[1] "%d czcionki"
msgstr[2] "%d czcionek"

#: a.cpp:5
#, c-format
msgctxt "not found or not copied"
msgid "%d font"
msgid_plural "%d fonts"
msgstr[0] "%d czcionki"
msgstr[1] "%d czcionek"
msgstr[2] "%d czcionek"

#: a.cpp:6
#, c-format
msgid "%d element"
msgid_plural "%d elements"
msgstr[0] "%d element"
msgstr[1] "%d elementy"
msgstr[2] "%d element\303\263w"

#: a.cpp:7
msgid "Only legacy"
msgstr ""

#: a.cpp:8
msgid ""
"Two\n"
"lines"
msgstr ""
"Dwie\n"
"linie"
)";

TsCatalog fixtureKeys()
{
    return keys({{QStringLiteral("Main"),
                  {key(QStringLiteral("Cancel")), key(QStringLiteral("Rule \"%1\" is invalid.")),
                   key(QStringLiteral("50% d")), key(QStringLiteral("Two\nlines")), key(QStringLiteral("Brand new")),
                   key(QStringLiteral("%n font(s)"), {}, true)}},
                 {QStringLiteral("StyleManager"),
                  {key(QStringLiteral("Cancel")), key(QStringLiteral("Cancel"), QStringLiteral("found or copied")),
                   key(QStringLiteral("%d font"), QStringLiteral("found or copied"))}}});
}

const KeyMapRow *findRow(const KeyMap &map, MapStatus status, const QString &msgid, const QString &context = {})
{
    for (const KeyMapRow &row : map.rows)
        if (row.status == status && row.msgid.value_or(QString()) == msgid
            && (context.isEmpty() || row.context.value_or(QString()) == context))
            return &row;
    return nullptr;
}

} // namespace

TEST(PoParser, ReadsHeaderEntriesContextsPluralsFlagsAndContinuations)
{
    const PoCatalog catalog = po(fixturePl);
    EXPECT_EQ(catalog.headerComments,
              (QStringList{QStringLiteral("# Polish translation."), QStringLiteral("# A translator <t@example.org>, 2022.")}));
    EXPECT_EQ(catalog.header(QStringLiteral("language")), u"pl");
    EXPECT_EQ(catalog.header(QStringLiteral("Last-Translator")), u"altqx <al@altqx.com>");
    ASSERT_EQ(catalog.messages.size(), 8);
    EXPECT_EQ(catalog.messages[1].msgid, u"Rule \"%s\" is invalid.");
    EXPECT_EQ(catalog.messages[1].msgstr.value(0), QStringLiteral("Reguła \"%s\" jest nieprawidłowa."));
    EXPECT_EQ(catalog.messages[1].references, QStringList{QStringLiteral("a.cpp:2")});
    EXPECT_TRUE(catalog.messages[2].fuzzy());
    EXPECT_EQ(catalog.messages[3].msgctxt, std::optional<QString>(QStringLiteral("found or copied")));
    EXPECT_EQ(catalog.messages[3].msgidPlural, std::optional<QString>(QStringLiteral("%d fonts")));
    EXPECT_EQ(catalog.messages[3].msgstr.size(), 3);
    EXPECT_FALSE(catalog.messages[5].msgctxt.has_value());
    EXPECT_EQ(catalog.messages[7].msgid, u"Two\nlines");
    EXPECT_EQ(catalog.messages[7].msgstr, QStringList{QStringLiteral("Dwie\nlinie")});
}

TEST(PoParser, RefusesBrokenEntries)
{
    EXPECT_FALSE(parsePo("msgid \"a\"\n").has_value());
    EXPECT_FALSE(parsePo("msgid \"a\"\nmsgstr[1] \"b\"\n").has_value());
    EXPECT_FALSE(parsePo("\"continued\"\n").has_value());
    EXPECT_FALSE(parsePo("msgid \"a\\q\"\nmsgstr \"\"\n").has_value());
}

// The legacy catalogs at the input commit, as msgfmt --statistics counts them.
TEST(PoParser, ReadsTheLegacyCatalogs)
{
    const auto pot = parsePo(readSource(QStringLiteral("Locale/template.pot")));
    ASSERT_TRUE(pot.has_value()) << pot.error().toStdString();
    EXPECT_EQ(pot->messages.size(), 1825);
    int plural = 0, context = 0;
    for (const PoMessage &m : pot->messages) {
        plural += m.plural();
        context += m.msgctxt.has_value();
    }
    EXPECT_EQ(plural, 3);
    EXPECT_EQ(context, 2);
    const QMap<QString, std::array<int, 3>> expected = {{QStringLiteral("pl"), {1825, 0, 0}},
                                                        {QStringLiteral("ko_KR"), {1767, 47, 11}},
                                                        {QStringLiteral("th_TH"), {1825, 0, 0}},
                                                        {QStringLiteral("ta"), {1767, 37, 21}}};
    for (const LanguageInput &input : legacyInputs()) {
        const auto catalog = parsePo(input.bytes);
        ASSERT_TRUE(catalog.has_value()) << catalog.error().toStdString();
        std::array<int, 3> counts{};
        for (const PoMessage &m : catalog->messages) {
            // msgfmt counts a plural entry as translated when all forms are.
            const bool empty = std::any_of(m.msgstr.cbegin(), m.msgstr.cend(), [](const QString &s) { return s.isEmpty(); });
            ++counts[m.fuzzy() ? 1 : empty ? 2 : 0];
        }
        EXPECT_EQ(counts, expected.value(input.language)) << input.language.toStdString();
        EXPECT_EQ(catalog->header(QStringLiteral("Language")), input.language);
    }
}

TEST(Printf, DirectivesAsMsgfmtComparesThem)
{
    const auto conversions = [](const QString &text) {
        QString out;
        for (const PrintfDirective &d : printfDirectives(text))
            out += d.conversion;
        return out;
    };
    EXPECT_EQ(conversions(QStringLiteral("%s of %i, %5.2f%% and %ld")), u"sifd");
    EXPECT_EQ(conversions(QStringLiteral("100%% done")), u"");
    EXPECT_EQ(printfDirectives(QStringLiteral("%2$s %1$d")).value(0).argument, 2);
    EXPECT_TRUE(printfDirectives(QStringLiteral("%*d")).value(0).starArgs);
}

TEST(Printf, BecomesQtMarkers)
{
    const auto qt = [](const char *text) {
        const auto out = printfToQt(QString::fromUtf8(text));
        return out ? out->text : QStringLiteral("<none>");
    };
    EXPECT_EQ(qt("Rule \"%s\" in line %i."), u"Rule \"%1\" in line %2.");
    EXPECT_EQ(qt("%2$s before %1$s"), u"%2 before %1");
    EXPECT_EQ(qt("%d%% done"), u"%1% done");
    EXPECT_EQ(qt("no directives"), u"no directives");
    EXPECT_EQ(qt("%*d"), u"<none>");                // a star argument
    EXPECT_EQ(qt("%1$s and %s"), u"<none>");        // positional mixed with sequential
    EXPECT_EQ(qt("%%1"), u"<none>");                // "%1" would be a marker
    EXPECT_EQ(qt("%s5"), u"<none>");                // "%15" would be another marker
    const auto args = printfToQt(QStringLiteral("%2$i %1$s"));
    ASSERT_TRUE(args.has_value());
    EXPECT_EQ(args->arguments, (QMap<int, QChar>{{1, u's'}, {2, u'i'}}));
}

// The checked-in catalogs are written exactly as they are read.
TEST(Ts, RoundTripsTheCheckedInCatalogs)
{
    QStringList files = {QStringLiteral("i18n/migration/keys.ts")};
    for (const QString &language : languages)
        files << QStringLiteral("i18n/hikarisub_%1.ts").arg(language) << QStringLiteral("i18n/hikarisub_gettext_%1.ts").arg(language);
    for (const QString &file : files) {
        const QByteArray bytes = readSource(file);
        const auto catalog = readTs(bytes);
        ASSERT_TRUE(catalog.has_value()) << file.toStdString() << ": " << catalog.error().toStdString();
        EXPECT_TRUE(sameBytes(writeTs(*catalog), bytes)) << file.toStdString();
    }
}

TEST(Ts, EscapesMarkupAndControlCharacters)
{
    TsCatalog catalog;
    catalog.language = QStringLiteral("pl");
    TsMessage m = key(QStringLiteral("<a href=\"x\">'&'</a>\x01\tend\n"));
    m.state = TsState::Finished;
    m.translations = {QStringLiteral(" x")};
    catalog.contexts.append({QStringLiteral("Main"), {m}});
    const QByteArray bytes = writeTs(catalog);
    EXPECT_TRUE(bytes.contains("&lt;a href=&quot;x&quot;&gt;&apos;&amp;&apos;&lt;/a&gt;<byte value=\"x1\"/>\tend\n</source>"));
    EXPECT_TRUE(bytes.contains("<translation>&#xa0;x</translation>"));
    const auto back = readTs(bytes);
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->contexts[0].messages[0].source, m.source);
    EXPECT_EQ(back->contexts[0].messages[0].translations, m.translations);
}

TEST(KeyMap, ExactNeedsTheSameTextAndDisambiguation)
{
    const KeyMap map = buildKeyMap(po(fixturePot), fixtureKeys(), QStringLiteral("abc"));
    // "Cancel" serves two rewrite contexts: one row each.
    ASSERT_NE(findRow(map, MapStatus::Exact, QStringLiteral("Cancel"), QStringLiteral("Main")), nullptr);
    ASSERT_NE(findRow(map, MapStatus::Exact, QStringLiteral("Cancel"), QStringLiteral("StyleManager")), nullptr);
    int cancelRows = 0;
    for (const KeyMapRow &row : map.rows)
        cancelRows += row.msgid == QStringLiteral("Cancel");
    EXPECT_EQ(cancelRows, 2);
    // A rewrite disambiguation is a different key from the context-free id.
    bool disambiguatedCancelIsNew = false;
    for (const KeyMapRow &row : map.rows)
        if (row.source == u"Cancel" && row.comment == u"found or copied")
            disambiguatedCancelIsNew = row.status == MapStatus::New;
    EXPECT_TRUE(disambiguatedCancelIsNew);
    EXPECT_NE(findRow(map, MapStatus::Exact, QStringLiteral("Two\nlines")), nullptr);
    EXPECT_NE(findRow(map, MapStatus::Exact, QStringLiteral("50% d")), nullptr);
    EXPECT_NE(findRow(map, MapStatus::Obsolete, QStringLiteral("Only legacy")), nullptr);
}

TEST(KeyMap, PlaceholderCandidatesComeOnlyFromCFormatIds)
{
    const KeyMap map = buildKeyMap(po(fixturePot), fixtureKeys(), QStringLiteral("abc"));
    const KeyMapRow *rule = findRow(map, MapStatus::Placeholder, QStringLiteral("Rule \"%s\" is invalid."));
    ASSERT_NE(rule, nullptr);
    EXPECT_EQ(rule->source, u"Rule \"%1\" is invalid.");
    EXPECT_EQ(rule->placeholders, u"%s -> %1");
}

// Plural and context-only messages are never collapsed into other keys: the
// two font-count meanings stay apart, and a numerus key is not an id match.
TEST(KeyMap, PluralAndContextMessagesAreNotCollapsed)
{
    const KeyMap map = buildKeyMap(po(fixturePot), fixtureKeys(), QStringLiteral("abc"));
    int fontRows = 0;
    QSet<QString> fontContexts;
    for (const KeyMapRow &row : map.rows)
        if (row.msgid == QStringLiteral("%d font")) {
            ++fontRows;
            EXPECT_EQ(row.status, MapStatus::Obsolete);
            fontContexts.insert(row.msgctxt.value_or(QString()));
        }
    EXPECT_EQ(fontRows, 2);
    EXPECT_EQ(fontContexts, (QSet<QString>{QStringLiteral("found or copied"), QStringLiteral("not found or not copied")}));
    EXPECT_NE(findRow(map, MapStatus::Obsolete, QStringLiteral("%d element")), nullptr);
    int newRows = 0;
    for (const KeyMapRow &row : map.rows)
        newRows += row.status == MapStatus::New;
    // Brand new, %n font(s), the disambiguated Cancel and the singular-only "%d font".
    EXPECT_EQ(newRows, 4);
}

TEST(KeyMap, RoundTripsThroughItsFile)
{
    const KeyMap map = buildKeyMap(po(fixturePot), fixtureKeys(), QStringLiteral("abc"));
    const QByteArray bytes = writeKeyMap(map);
    const auto back = readKeyMap(bytes);
    ASSERT_TRUE(back.has_value()) << back.error().toStdString();
    EXPECT_EQ(back->inputCommit, u"abc");
    EXPECT_TRUE(sameBytes(writeKeyMap(*back), bytes));
    EXPECT_FALSE(readKeyMap("status\tmsgctxt\n").has_value());
}

TEST(Migrate, KeepsReviewedTranslationsAndLeavesTheRestUnfinished)
{
    const TsCatalog rewrite = fixtureKeys();
    const KeyMap map = buildKeyMap(po(fixturePot), rewrite, QStringLiteral("abc"));
    const auto out = migrate(map, rewrite, fixturePot, {{QStringLiteral("pl"), QStringLiteral("pl.po"), fixturePl}});
    ASSERT_TRUE(out.has_value()) << out.error().toStdString();
    const auto ui = readTs(out->uiCatalogs.value(QStringLiteral("pl")));
    ASSERT_TRUE(ui.has_value());
    EXPECT_EQ(ui->language, u"pl");
    const auto state = [&](const QString &context, const QString &source, const QString &comment = {}) {
        const TsMessage *m = ui->find(context, source, comment);
        return m ? std::pair(m->state, m->translations) : std::pair(TsState::Obsolete, QStringList{});
    };
    EXPECT_EQ(state(QStringLiteral("Main"), QStringLiteral("Cancel")), std::pair(TsState::Finished, QStringList{QStringLiteral("Anuluj")}));
    EXPECT_EQ(state(QStringLiteral("StyleManager"), QStringLiteral("Cancel")), std::pair(TsState::Finished, QStringList{QStringLiteral("Anuluj")}));
    EXPECT_EQ(state(QStringLiteral("Main"), QStringLiteral("Two\nlines")), std::pair(TsState::Finished, QStringList{QStringLiteral("Dwie\nlinie")}));
    // Fuzzy stays unfinished with its text; a placeholder change is never finished.
    EXPECT_EQ(state(QStringLiteral("Main"), QStringLiteral("50% d")), std::pair(TsState::Unfinished, QStringList{QStringLiteral("50% d?")}));
    EXPECT_EQ(state(QStringLiteral("Main"), QStringLiteral("Rule \"%1\" is invalid.")),
              std::pair(TsState::Unfinished, QStringList{QStringLiteral("Reguła \"%1\" jest nieprawidłowa.")}));
    EXPECT_EQ(state(QStringLiteral("Main"), QStringLiteral("Brand new")), std::pair(TsState::Unfinished, QStringList{QString()}));
    // Polish numerus keys get Qt's three forms.
    EXPECT_EQ(state(QStringLiteral("Main"), QStringLiteral("%n font(s)")), std::pair(TsState::Unfinished, QStringList(3, QString())));
}

// aegisub.gettext reaches what wxGetTranslation(str) reaches
// (Automation.cpp:85-86 at 20d647c4): the context-free ids, a plural entry
// by its singular id with msgstr[0] (wx translation.cpp:1061-1078,
// 1144-1160), never a context entry.
TEST(Migrate, GettextCatalogHoldsWhatTheLegacyLookupReaches)
{
    const TsCatalog rewrite = fixtureKeys();
    const KeyMap map = buildKeyMap(po(fixturePot), rewrite, QStringLiteral("abc"));
    const auto out = migrate(map, rewrite, fixturePot, {{QStringLiteral("pl"), QStringLiteral("pl.po"), fixturePl}});
    ASSERT_TRUE(out.has_value()) << out.error().toStdString();
    const auto gettext = readTs(out->gettextCatalogs.value(QStringLiteral("pl")));
    ASSERT_TRUE(gettext.has_value());
    ASSERT_EQ(gettext->contexts.size(), 1);
    EXPECT_EQ(gettext->contexts[0].name, QString::fromLatin1(gettextContext));
    QStringList sources;
    for (const TsMessage &m : gettext->contexts[0].messages)
        sources << m.source;
    EXPECT_EQ(sources, (QStringList{QStringLiteral("Cancel"), QStringLiteral("Rule \"%s\" is invalid."), QStringLiteral("50% d"),
                                    QStringLiteral("%d element"), QStringLiteral("Only legacy"), QStringLiteral("Two\nlines")}));
    const QString context = QString::fromLatin1(gettextContext);
    const TsMessage *rule = gettext->find(context, QStringLiteral("Rule \"%s\" is invalid."), {});
    ASSERT_NE(rule, nullptr);
    EXPECT_EQ(rule->translations.value(0), QStringLiteral("Reguła \"%s\" jest nieprawidłowa.")); // printf kept
    EXPECT_EQ(rule->state, TsState::Finished);
    const TsMessage *element = gettext->find(context, QStringLiteral("%d element"), {});
    ASSERT_NE(element, nullptr);
    EXPECT_FALSE(element->numerus);
    EXPECT_EQ(element->translations, QStringList{QStringLiteral("%d element")});
    EXPECT_EQ(gettext->find(context, QStringLiteral("50% d"), {})->state, TsState::Unfinished);
    EXPECT_EQ(gettext->find(context, QStringLiteral("Only legacy"), {})->state, TsState::Unfinished);
    bool hasInput = false;
    for (const auto &[name, value] : gettext->extras)
        hasInput |= name == u"po-input" && value == u"Locale/pl.po at abc";
    EXPECT_TRUE(hasInput);
}

TEST(Migrate, RefusesAMapThatNoLongerMatches)
{
    const TsCatalog rewrite = fixtureKeys();
    KeyMap map = buildKeyMap(po(fixturePot), rewrite, QStringLiteral("abc"));
    const QList<LanguageInput> pl = {{QStringLiteral("pl"), QStringLiteral("pl.po"), fixturePl}};
    KeyMap missing = map;
    missing.rows.removeLast(); // a New row
    EXPECT_FALSE(migrate(missing, rewrite, fixturePot, pl).has_value());
    KeyMap stale = map;
    stale.rows.last().source = QStringLiteral("Removed from the sources");
    EXPECT_FALSE(migrate(stale, rewrite, fixturePot, pl).has_value());
    KeyMap unknown = map;
    unknown.rows.first().msgid = QStringLiteral("Not in the template");
    EXPECT_FALSE(migrate(unknown, rewrite, fixturePot, pl).has_value());
    EXPECT_FALSE(migrate(map, rewrite, fixturePot, {{QStringLiteral("xx"), QStringLiteral("xx.po"), fixturePl}}).has_value());
}

// The reviewed map is the candidate map of the recorded keys and template.
TEST(CheckedIn, KeyMapIsTheCandidateMapOfTheRecordedInputs)
{
    const auto pot = parsePo(readSource(QStringLiteral("Locale/template.pot")));
    const auto rewrite = readTs(readSource(QStringLiteral("i18n/migration/keys.ts")));
    ASSERT_TRUE(pot && rewrite);
    const QByteArray checkedIn = readSource(QStringLiteral("i18n/migration/keymap.tsv"));
    EXPECT_TRUE(sameBytes(writeKeyMap(buildKeyMap(*pot, *rewrite, QStringLiteral(HIKARI_I18N_INPUT_COMMIT))), checkedIn));
    const auto map = readKeyMap(checkedIn);
    ASSERT_TRUE(map.has_value()) << map.error().toStdString();
    EXPECT_EQ(map->inputCommit, QStringLiteral(HIKARI_I18N_INPUT_COMMIT));
    QMap<MapStatus, int> counts;
    for (const KeyMapRow &row : map->rows)
        ++counts[row.status];
    EXPECT_EQ(counts.value(MapStatus::Exact), 785);
    EXPECT_EQ(counts.value(MapStatus::Placeholder), 22);
    EXPECT_EQ(counts.value(MapStatus::New), 321);
    EXPECT_EQ(counts.value(MapStatus::Obsolete), 1138);
}

// The conversion reproduces the checked-in gettext catalogs and report, and
// every migrated rewrite entry is still in the rewrite catalogs as migrated
// (lupdate may since have added keys or marked some vanished).
TEST(CheckedIn, ConversionReproducesTheCatalogs)
{
    const auto map = readKeyMap(readSource(QStringLiteral("i18n/migration/keymap.tsv")));
    const auto rewrite = readTs(readSource(QStringLiteral("i18n/migration/keys.ts")));
    ASSERT_TRUE(map && rewrite);
    const auto out = migrate(*map, *rewrite, readSource(QStringLiteral("Locale/template.pot")), legacyInputs());
    ASSERT_TRUE(out.has_value()) << out.error().toStdString();
    EXPECT_TRUE(sameBytes(out->report, readSource(QStringLiteral("i18n/migration/report.md"))));
    for (const QString &language : languages) {
        EXPECT_TRUE(sameBytes(out->gettextCatalogs.value(language),
                              readSource(QStringLiteral("i18n/hikarisub_gettext_%1.ts").arg(language))))
            << language.toStdString();
        const auto migrated = readTs(out->uiCatalogs.value(language));
        const auto checkedIn = readTs(readSource(QStringLiteral("i18n/hikarisub_%1.ts").arg(language)));
        ASSERT_TRUE(migrated && checkedIn);
        EXPECT_EQ(checkedIn->language, language);
        for (const TsContext &context : migrated->contexts)
            for (const TsMessage &m : context.messages) {
                const TsMessage *now = checkedIn->find(context.name, m.source, m.comment);
                ASSERT_NE(now, nullptr) << language.toStdString() << " " << m.source.toStdString();
                EXPECT_EQ(now->translations, m.translations) << m.source.toStdString();
                EXPECT_TRUE(now->state == m.state || now->state == TsState::Vanished) << m.source.toStdString();
            }
    }
}

// Placeholder identity, multiplicity, order and type in every translated
// form: the gettext catalogs keep the printf text, and the rewrite catalogs'
// carried translations use the source's Qt markers.
TEST(CheckedIn, PlaceholderSignaturesMatchInEveryTranslatedForm)
{
    for (const QString &language : languages) {
        const auto gettext = readTs(readSource(QStringLiteral("i18n/hikarisub_gettext_%1.ts").arg(language)));
        ASSERT_TRUE(gettext.has_value());
        int checked = 0;
        for (const TsMessage &m : gettext->contexts.value(0).messages) {
            bool cFormat = false;
            for (const auto &[name, value] : m.extras)
                cFormat |= name == u"po-flags" && value.contains(QStringLiteral("c-format"));
            if (!cFormat || m.translations.value(0).isEmpty())
                continue;
            const auto source = printfToQt(m.source);
            const auto translation = printfToQt(m.translations.value(0));
            ASSERT_TRUE(source && translation) << m.source.toStdString();
            EXPECT_EQ(source->arguments, translation->arguments) << language.toStdString() << " " << m.source.toStdString();
            ++checked;
        }
        EXPECT_GT(checked, 80) << language.toStdString();

        const auto ui = readTs(readSource(QStringLiteral("i18n/hikarisub_%1.ts").arg(language)));
        ASSERT_TRUE(ui.has_value());
        static const QRegularExpression marker(QStringLiteral("%L?(\\d+)"));
        const auto markers = [](const QString &text) {
            QStringList out;
            for (auto it = marker.globalMatch(text); it.hasNext();)
                out << it.next().captured(1);
            out.sort();
            return out;
        };
        for (const TsContext &context : ui->contexts)
            for (const TsMessage &m : context.messages)
                if (!m.numerus && !m.translations.value(0).isEmpty())
                    EXPECT_EQ(markers(m.translations.value(0)), markers(m.source)) << language.toStdString() << " " << m.source.toStdString();
    }
}

// Both font-count meanings stay in the record as separate obsolete keys and
// never reach the context-free gettext catalog.
TEST(CheckedIn, FontCountMeaningsStaySeparate)
{
    const auto map = readKeyMap(readSource(QStringLiteral("i18n/migration/keymap.tsv")));
    ASSERT_TRUE(map.has_value());
    QStringList contexts;
    for (const KeyMapRow &row : map->rows)
        if (row.msgid == QStringLiteral("%d font")) {
            EXPECT_EQ(row.status, MapStatus::Obsolete);
            EXPECT_EQ(row.msgidPlural, std::optional<QString>(QStringLiteral("%d fonts")));
            contexts << row.msgctxt.value_or(QString());
        }
    EXPECT_EQ(contexts, (QStringList{QStringLiteral("found or copied"), QStringLiteral("not found or not copied")}));
    for (const QString &language : languages) {
        const auto gettext = readTs(readSource(QStringLiteral("i18n/hikarisub_gettext_%1.ts").arg(language)));
        ASSERT_TRUE(gettext.has_value());
        EXPECT_EQ(gettext->find(QString::fromLatin1(gettextContext), QStringLiteral("%d font"), {}), nullptr);
        EXPECT_NE(gettext->find(QString::fromLatin1(gettextContext), QStringLiteral("%d element"), {}), nullptr);
        EXPECT_EQ(gettext->contexts.value(0).messages.size(), 1823);
    }
}
