#include "hikari/core/ass_load.h"
#include "hikari/core/ass_save.h"
#include "hikari/core/extradata.h"
#include "hikari/core/tag_transfer.h"
#include "hikari/core/text_projection.h"

#include <gtest/gtest.h>

#include <cstring>
#include <functional>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

using namespace hikari::core;

namespace {

std::string fixture(const std::string &name)
{
    std::ifstream in(std::string(HIKARI_EXTRADATA_FIXTURES) + "/" + name, std::ios::binary);
    EXPECT_TRUE(in) << name;
    return {std::istreambuf_iterator<char>(in), {}};
}

std::vector<std::byte> bytesOf(std::string_view s)
{
    std::vector<std::byte> out(s.size());
    std::memcpy(out.data(), s.data(), s.size());
    return out;
}

std::string text(const std::vector<std::byte> &b)
{
    return std::string(reinterpret_cast<const char *>(b.data()), b.size());
}

std::string str(std::u8string_view s)
{
    return std::string(s.begin(), s.end());
}

Document load(const std::string &bytes)
{
    return loadAss(bytesOf(bytes)).document;
}

std::string save(const Document &document)
{
    return text(encodeAss(document));
}

// What fa3.lua dumps: each event's text, then its line.extra sorted by key.
std::string scriptView(const Document &document)
{
    std::string out;
    for (const auto *line : document.lines()) {
        out += str(line->text);
        for (const auto &[key, value] : extradataByKey(line->extra))
            out += "\t" + str(key) + "=" + str(value);
        out += "\n";
    }
    return out;
}

LineId lineWith(const Document &document, std::u8string_view part)
{
    for (const auto *line : document.lines())
        if (line->text.find(part) != std::u8string::npos)
            return line->id;
    ADD_FAILURE() << str(part);
    return {};
}

std::vector<std::string> dataLines(const std::string &file)
{
    std::vector<std::string> out;
    std::istringstream in(file);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.starts_with("Data:"))
            out.push_back(line);
    }
    return out;
}

const std::string kHead = "[Script Info]\nScriptType: v4.00+\n\n[Events]\n"
                          "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                          "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,one\n";

std::string withExtra(const std::string &input, std::vector<Extradata> extra)
{
    Document document = load(input);
    document.editLine(lineWith(document, u8"one"), [&](LineRecord &l) { l.extra = extra; });
    return save(document);
}

}

TEST(Extradata, ReadsEscapedAndUuencodedValues)
{
    const auto escaped = parseExtradataLine(u8"Data: 3,_aegi_perspective_ambient_plane,e1;2#7C3;4");
    ASSERT_TRUE(escaped);
    EXPECT_EQ(escaped->id, 3u);
    EXPECT_EQ(escaped->data, (Extradata{u8"_aegi_perspective_ambient_plane", u8"1;2|3;4"}));
    const auto encoded = parseExtradataLine(u8"Data:8,k#2Cey,u93RC,'-[:(RF)W9M:SRI,'E[;HRL)Q");
    ASSERT_TRUE(encoded);
    EXPECT_EQ(encoded->data, (Extradata{u8"k,ey", u8"a,b,c:d|e#f,g,h,i:j|k#"}));
    EXPECT_EQ(parseExtradataLine(u8"Data: 1,key,xvalue")->data.value, u8"");
    EXPECT_FALSE(parseExtradataLine(u8"Data: 1,key"));
    EXPECT_FALSE(parseExtradataLine(u8"Data: x,key,evalue"));
    EXPECT_FALSE(parseExtradataLine(u8"Data: 99999999999,key,evalue"));
}

TEST(Extradata, WritesEveryDataLineAegisubWrote)
{
    for (const auto *name : {"folds.aegisub.ass", "perspective.aegisub.ass", "folds-edited.aegisub.ass"})
        for (const auto &line : dataLines(fixture(name))) {
            const std::u8string data(line.begin(), line.end());
            const auto entry = parseExtradataLine(data);
            ASSERT_TRUE(entry) << line;
            EXPECT_EQ(str(extradataLine(*entry)), line);
        }
}

TEST(Extradata, ReadsOnlyALeadingIdBlock)
{
    const auto prefix = extradataPrefix(u8"{=7=8}{=9}text");
    ASSERT_TRUE(prefix);
    EXPECT_EQ(prefix->ids, (std::vector<std::uint32_t>{7, 8}));
    EXPECT_EQ(prefix->length, 6u);
    for (const auto *other : {u8"{=}x", u8"{=1x}", u8"{\\i1}{=1}", u8" {=1}", u8"{=1", u8"{=1=}"})
        EXPECT_FALSE(extradataPrefix(other)) << str(other);
    EXPECT_EQ(extradataPrefixText({0, 12}), u8"{=0=12}");
}

TEST(Extradata, ScriptsSeeWhatAegisubShowsThem)
{
    EXPECT_EQ(scriptView(load(fixture("folds.ass"))), fixture("folds.lua-extra.txt"));
    EXPECT_EQ(scriptView(load(fixture("folds.aegisub.ass"))), fixture("folds.lua-extra.txt"));
    EXPECT_EQ(scriptView(load(fixture("perspective.ass"))), fixture("perspective.lua-extra.txt"));
    EXPECT_EQ(scriptView(load(fixture("perspective.aegisub.ass"))), fixture("perspective.lua-extra.txt"));
}

TEST(Extradata, KeepsTheSectionAndEveryIdAsLoaded)
{
    const Document document = load(fixture("folds.ass"));
    EXPECT_EQ(document.extradata().size(), 10u);
    EXPECT_EQ(document.sections().back().kind, SectionKind::AegisubExtradata);
    const auto *shared = document.lines()[8];
    EXPECT_EQ(shared->extradataIds, (std::vector<std::uint32_t>{7, 8}));
    EXPECT_EQ(document.lines()[10]->extradataIds, (std::vector<std::uint32_t>{42}));
    EXPECT_TRUE(document.lines()[10]->extra.empty());
}

TEST(Extradata, UnchangedFilesSaveByteForByte)
{
    for (const auto *name : {"folds.ass", "perspective.ass", "folds.aegisub.ass", "perspective.aegisub.ass",
                             "folds-edited.aegisub.ass"}) {
        const std::string input = fixture(name);
        const auto loaded = loadAss(bytesOf(input));
        for (const auto &d : loaded.diagnostics)
            EXPECT_FALSE(d.kind == Diagnostic::Kind::UnknownSection && d.excerpt.find(u8"Extradata") != std::u8string::npos)
                << name;
        EXPECT_EQ(save(loaded.document), input) << name;
    }
}

TEST(Extradata, EditingTheTextKeepsTheIds)
{
    Document document = load(fixture("folds.ass"));
    ASSERT_TRUE(document.setLineText(lineWith(document, u8"Shares the"), u8"Shares the script value (edited)"));
    ASSERT_TRUE(document.setLineText(lineWith(document, u8"no entry"), u8"Still names it"));
    const std::string saved = save(document);
    EXPECT_NE(saved.find(",,{=8}Shares the script value (edited)\r\n"), std::string::npos);
    EXPECT_NE(saved.find(",,{=42}Still names it\r\n"), std::string::npos);
    EXPECT_EQ(dataLines(saved), dataLines(fixture("folds.ass")));
}

TEST(Extradata, TheEditsAegisubMadeSaveTheSameValues)
{
    Document document = load(fixture("folds.ass"));
    document.editLine(lineWith(document, u8"Inside both"),
                      [](LineRecord &l) { l.extra.push_back({u8"hikari.added", u8"x,y:z|w#"}); });
    document.setLineText(lineWith(document, u8"Shares the"), u8"Shares the script value (edited)");
    document.editLine(lineWith(document, u8"Second adjacent fold ends"),
                      [](LineRecord &l) { l.extra.push_back({u8"hikari.big", u8"1,2,3,4,5:6|7#8,9"}); });
    document.editLine(lineWith(document, u8"First adjacent fold ends"), [](LineRecord &l) { l.extra.clear(); });
    LineRecord copy = *document.lines()[0];
    copy.text = u8"Copied fold line";
    document.appendLine(copy);
    const std::string saved = save(document);

    const std::string aegisub = fixture("folds-edited.aegisub.ass");
    EXPECT_EQ(scriptView(load(saved)), scriptView(load(aegisub)));
    auto expected = dataLines(fixture("folds.ass"));
    for (const auto &line : dataLines(aegisub))
        if (line.starts_with("Data: 10,"))
            expected.push_back("Data: 43," + line.substr(9));
        else if (line.starts_with("Data: 11,"))
            expected.push_back("Data: 44," + line.substr(9));
    EXPECT_EQ(dataLines(saved), expected);
    EXPECT_NE(saved.find(",,{=43}{\\i1}Inside both{\\i0}\r\n"), std::string::npos);
    EXPECT_NE(saved.find(",,{=7=8=44}Second adjacent fold ends"), std::string::npos);
    EXPECT_NE(saved.find(",,First adjacent fold ends\r\n"), std::string::npos);
    EXPECT_NE(saved.find(",,{=0}Copied fold line\r\n"), std::string::npos);
    EXPECT_NE(saved.find(",,{=42}Names an id with no entry\r\n"), std::string::npos);
    const auto data = saved.rfind("Data: 44,");
    EXPECT_EQ(saved.find("\r\n", data) + 2, saved.size());
}

TEST(Extradata, AValueAlreadyInTheTableReusesItsEntry)
{
    Document document = load(fixture("folds.ass"));
    const auto unused = document.extradata()[9].data;
    document.editLine(lineWith(document, u8"no entry"), [&](LineRecord &l) { l.extra = {unused}; });
    document.editLine(lineWith(document, u8"Inside both"), [&](LineRecord &l) { l.extra = {unused}; });
    const std::string saved = save(document);
    EXPECT_NE(saved.find(",,{=9}Names an id with no entry\r\n"), std::string::npos);
    EXPECT_NE(saved.find(",,{=9}{\\i1}Inside both"), std::string::npos);
    EXPECT_EQ(dataLines(saved), dataLines(fixture("folds.ass")));
}

TEST(Extradata, LinesSharingANewValueShareOneEntry)
{
    Document document = load(kHead + "Dialogue: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,two\n");
    for (const auto *part : {u8"one", u8"two"})
        document.editLine(lineWith(document, part), [](LineRecord &l) { l.extra = {{u8"k", u8"v"}}; });
    EXPECT_EQ(save(document), kHead.substr(0, kHead.size() - 4) + "{=0}one\n"
                                  "Dialogue: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,{=0}two\n"
                                  "\n[Aegisub Extradata]\nData: 0,k,ev\n");
}

TEST(Extradata, ANewSectionGoesAtTheEndOrBeforeEmbeddedData)
{
    const std::string one = kHead.substr(0, kHead.size() - 4);
    EXPECT_EQ(withExtra(kHead, {{u8"k", u8"v"}}), one + "{=0}one\n\n[Aegisub Extradata]\nData: 0,k,ev\n");
    const std::string fonts = "[Fonts]\nfontname: a_0.ttf\n!!!!\n";
    EXPECT_EQ(withExtra(kHead + "\n" + fonts, {{u8"k", u8"v"}}),
              one + "{=0}one\n\n[Aegisub Extradata]\nData: 0,k,ev\n\n" + fonts);
}

TEST(Extradata, AddedEntriesFollowTheSectionsLastEntry)
{
    const std::string input = kHead + "\n[Aegisub Extradata]\r\nData: 4,k,ev\r\n\r\n[Hikari]\nLayout=x\n";
    EXPECT_EQ(withExtra(input, {{u8"k", u8"v"}, {u8"n", u8"w"}}),
              kHead.substr(0, kHead.size() - 4) + "{=4=5}one\n\n[Aegisub Extradata]\r\nData: 4,k,ev\r\n"
                                                   "Data: 5,n,ew\r\n\r\n[Hikari]\nLayout=x\n");
}

TEST(Extradata, TheRendererCopyGainsNoIds)
{
    Document document = load(kHead);
    document.editLine(lineWith(document, u8"one"), [](LineRecord &l) { l.extra = {{u8"k", u8"v"}}; });
    EXPECT_EQ(text(encodeAss(document, AssSaveOptions{.renderer = true})), kHead);
}

TEST(Extradata, ATranslationPairKeepsItsIdsOnTheTranslation)
{
    const std::string input = "[Script Info]\nScriptType: v4.00+\nTLMode: Yes\nTLMode Style: O\n\n[Events]\n"
                              "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                              "Comment: 0,0:00:01.00,0:00:02.00,O,,0,0,0,,{=0}source\n"
                              "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,translated\n"
                              "\n[Aegisub Extradata]\nData: 0,k,ev\n";
    Document document = load(input);
    ASSERT_EQ(document.lines().size(), 1u);
    EXPECT_EQ(document.lines()[0]->text, u8"source");
    EXPECT_EQ(document.lines()[0]->extra, (std::vector<Extradata>{{u8"k", u8"v"}}));
    document.editLine(document.lines()[0]->id, [](LineRecord &l) { l.translation = u8"changed"; });
    const std::string saved = save(document);
    EXPECT_NE(saved.find(",,{=0}changed\n"), std::string::npos) << saved;
}

TEST(Extradata, SettingAKeyReplacesOnlyItsOwnEntries)
{
    const std::vector<Extradata> extra{{u8"_hikari_kanji_skip", u8"0"}, {u8"_aegi_folddata", u8"0;0;1"},
                                       {u8"_hikari_kanji_skip", u8"old"}};
    EXPECT_EQ(extradataValue(extra, u8"_hikari_kanji_skip"), u8"old");
    EXPECT_FALSE(extradataValue(extra, u8"_hikari_kara_from"));
    const std::vector<Extradata> set{{u8"_aegi_folddata", u8"0;0;1"}, {u8"_hikari_kanji_skip", u8"1"}};
    EXPECT_EQ(withExtradata(extra, u8"_hikari_kanji_skip", u8"1"), set);
    EXPECT_EQ(withExtradata(set, u8"_hikari_kanji_skip", u8"1"), set);
    EXPECT_EQ(withoutExtradata(extra, u8"_hikari_kanji_skip"), (std::vector<Extradata>{{u8"_aegi_folddata", u8"0;0;1"}}));
}

TEST(Extradata, ClipboardRowsRoundTrip)
{
    const std::vector<std::vector<Extradata>> rows = {{{u8"a,b", u8"c:d|e#f\n"}, {u8"g", u8""}}, {}, {{u8"h", u8"i"}}};
    EXPECT_EQ(parseExtradataClipboard(extradataClipboard(rows)), rows);
    EXPECT_TRUE(parseExtradataClipboard(u8"no pair\n").empty());
}

namespace {

const std::string kKaraoke = "[Script Info]\nScriptType: v4.00+\nPlayResX: 640\nPlayResY: 360\n\n"
                             "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                             "Comment: 0,0:00:00.00,0:00:00.00,Default,,0,0,0,template syl,{\\fad(100,100)}\n"
                             "Comment: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,karaoke,{\\k50}ka{\\k50}ra\n"
                             "Dialogue: 0,0:00:01.00,0:00:01.50,Default,,0,0,0,fx,{\\fad(100,100)}ka\n"
                             "Dialogue: 0,0:00:01.50,0:00:02.00,Default,,0,0,0,fx,{\\fad(100,100)}ra\n"
                             "Comment: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,karaoke,{\\k100}oke\n";

std::function<std::u8string()> tokens(std::vector<std::u8string> given)
{
    return [given = std::move(given), next = std::size_t{0}]() mutable { return given.at(next++); };
}

Document generated(const std::vector<std::pair<std::size_t, std::size_t>> &from, std::string script = kKaraoke)
{
    Document document = load(script);
    const auto lines = document.lines();
    std::vector<LineId> ids;
    for (const auto *line : lines)
        ids.push_back(line->id);
    for (const auto &[line, source] : from)
        EXPECT_TRUE(document.editLine(ids[line], [&](LineRecord &l) { l.generatedFrom = ids[source]; }));
    return document;
}

std::optional<std::u8string> valueAt(const Document &document, std::size_t row, std::u8string_view key)
{
    return extradataValue(document.lines()[row]->extra, key);
}

}

TEST(KaraokeMarks, AGeneratedLineAndItsKaraokeLineShareAToken)
{
    Document document = generated({{2, 1}, {3, 1}});
    markGeneratedLines(document, tokens({u8"abcdefgh"}));
    EXPECT_EQ(valueAt(document, 1, kKaraSourceKey), u8"1;abcdefgh");
    EXPECT_EQ(valueAt(document, 2, kKaraFromKey), u8"1;abcdefgh");
    EXPECT_EQ(valueAt(document, 3, kKaraFromKey), u8"1;abcdefgh");
    for (const std::size_t row : {0u, 4u})
        EXPECT_TRUE(document.lines()[row]->extra.empty()) << row;
    const std::string file = save(document);
    const Document reopened = load(file);
    const auto lines = reopened.lines();
    EXPECT_EQ(lines[2]->generatedFrom, lines[1]->id);
    EXPECT_EQ(lines[3]->generatedFrom, lines[1]->id);
    EXPECT_EQ(lines[1]->generatedFrom.value, 0u);
    Document again = load(file);
    markGeneratedLines(again, tokens({}));
    EXPECT_EQ(save(again), file);
}

TEST(KaraokeMarks, AGeneratedLineLosesTheKaraokeLinesTokenItWasCopiedWith)
{
    Document document = generated({{2, 1}});
    markGeneratedLines(document, tokens({u8"abcdefgh"}));
    ASSERT_TRUE(document.editLine(document.lines()[3]->id, [&](LineRecord &l) {
        l.extra = document.lines()[1]->extra;
        l.generatedFrom = document.lines()[1]->id;
    }));
    markGeneratedLines(document, tokens({}));
    EXPECT_FALSE(valueAt(document, 3, kKaraSourceKey));
    EXPECT_EQ(valueAt(document, 3, kKaraFromKey), u8"1;abcdefgh");
}

TEST(KaraokeMarks, AKaraokeLineCopiedWithItsTokenGetsANewOne)
{
    Document document = generated({{2, 1}});
    markGeneratedLines(document, tokens({u8"abcdefgh"}));
    ASSERT_TRUE(document.editLine(document.lines()[4]->id, [&](LineRecord &l) { l.extra = document.lines()[1]->extra; }));
    ASSERT_TRUE(document.editLine(document.lines()[3]->id, [&](LineRecord &l) { l.generatedFrom = document.lines()[4]->id; }));
    markGeneratedLines(document, tokens({u8"abcdefgh", u8"ijklmnop"}));
    EXPECT_EQ(valueAt(document, 1, kKaraSourceKey), u8"1;abcdefgh");
    EXPECT_EQ(valueAt(document, 4, kKaraSourceKey), u8"1;ijklmnop");
    EXPECT_EQ(valueAt(document, 3, kKaraFromKey), u8"1;ijklmnop");
    const Document reopened = load(save(document));
    EXPECT_EQ(reopened.lines()[2]->generatedFrom, reopened.lines()[1]->id);
    EXPECT_EQ(reopened.lines()[3]->generatedFrom, reopened.lines()[4]->id);
}

TEST(KaraokeMarks, OnlyWellFormedTokensLink)
{
    for (const std::string bad : {"2;abcdefgh", "1;ABCDEFGH", "1;abcdefg", "1;abcdefgh1", "abcdefgh", "1;abcdefg1"}) {
        std::string script = kKaraoke;
        script.replace(script.find(",karaoke,{\\k50}"), 9, ",karaoke,{=0}");
        script.replace(script.find(",fx,{\\fad(100,100)}ka"), 4, ",fx,{=1}");
        script += "\n[Aegisub Extradata]\nData: 0,_hikari_kara_source,e" + bad + "\nData: 1,_hikari_kara_from,e" + bad + "\n";
        const Document marked = load(script);
        ASSERT_EQ(valueAt(marked, 2, kKaraFromKey), std::u8string(bad.begin(), bad.end()));
        EXPECT_EQ(marked.lines()[2]->generatedFrom.value, 0u) << bad;
    }
}

TEST(KaraokeMarks, RandomTokensAreEightBase32Letters)
{
    const auto a = randomKaraToken(), b = randomKaraToken();
    EXPECT_NE(a, b);
    for (const auto &token : {a, b}) {
        ASSERT_EQ(token.size(), 8u);
        for (const char8_t c : token)
            EXPECT_TRUE((c >= u8'a' && c <= u8'z') || (c >= u8'2' && c <= u8'7')) << str(token);
    }
}

TEST(KaraokeMarks, HikarisSaveOfTheMarksIsTheGoldenFile)
{
    Document document = generated({{2, 1}, {3, 1}});
    markGeneratedLines(document, tokens({u8"abcdefgh"}));
    const std::string file = save(document);
    if (file != fixture("kara-marks-hikari.ass"))
        std::ofstream("kara-marks-hikari.actual.ass", std::ios::binary) << file;
    EXPECT_EQ(file, fixture("kara-marks-hikari.ass"));
}

TEST(KaraokeMarks, AegisubKeepsHikarisMarks)
{
    EXPECT_EQ(fixture("kara-marks-hikari.lua-extra.txt"),
              "{\\fad(100,100)}\n{\\k50}ka{\\k50}ra\t_hikari_kara_source=1;abcdefgh\n"
              "{\\fad(100,100)}ka\t_hikari_kara_from=1;abcdefgh\n{\\fad(100,100)}ra\t_hikari_kara_from=1;abcdefgh\n"
              "{\\k100}oke\n");
    const Document document = load(fixture("kara-marks-hikari.aegisub.ass"));
    const auto lines = document.lines();
    ASSERT_EQ(lines.size(), 5u);
    EXPECT_EQ(lines[2]->generatedFrom, lines[1]->id);
    EXPECT_EQ(lines[3]->generatedFrom, lines[1]->id);
}

namespace {

const std::u16string kAmbiguousOriginal = u"a {\\i1}b{\\i0} c {\\b1}d{\\b0} e";

Document withKeptOrder()
{
    Document document = load("[Script Info]\nScriptType: v4.00+\nTLMode: Yes\nTLMode Style: O\n\n[Events]\n"
                             "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,O,,0,0,0,,a {\\i1}b{\\i0} c {\\b1}d{\\b0} e\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,\n");
    const auto tags = readOriginalTags(kAmbiguousOriginal);
    const auto translation = toUtf8(composeTranslation(tags, TranslationDraft{u"{\\i1}x{\\i0} y", {{5, 6, 2}}, true}));
    const auto value = runOrderValue(tags, translation, std::vector<int>{2});
    EXPECT_TRUE(value);
    EXPECT_TRUE(document.editLine(document.lines()[0]->id, [&](LineRecord &l) {
        l.translation = translation;
        l.extra = withExtradata(l.extra, kRunOrderKey, value.value_or(u8""));
    }));
    return document;
}

}

TEST(TranslationOrder, HikarisSaveOfAnOrderIsTheGoldenFile)
{
    const std::string file = save(withKeptOrder());
    if (file != fixture("tm-order-hikari.ass"))
        std::ofstream("tm-order-hikari.actual.ass", std::ios::binary) << file;
    EXPECT_EQ(file, fixture("tm-order-hikari.ass"));
    const Document reopened = load(file);
    const auto *line = reopened.lines()[0];
    EXPECT_EQ(savedRunOrder(extradataValue(line->extra, kRunOrderKey).value_or(u8""), line->translation),
              std::vector<int>{2});
}

TEST(TranslationOrder, AegisubKeepsHikarisOrder)
{
    const std::string dumped = fixture("tm-order-hikari.lua-extra.txt");
    EXPECT_NE(dumped.find("\t_hikari_tm_order=1;"), std::string::npos) << dumped;
    const Document document = load(fixture("tm-order-hikari.aegisub.ass"));
    ASSERT_EQ(document.lines().size(), 1u);
    const auto *line = document.lines()[0];
    EXPECT_EQ(savedRunOrder(extradataValue(line->extra, kRunOrderKey).value_or(u8""), line->translation),
              std::vector<int>{2});
}
