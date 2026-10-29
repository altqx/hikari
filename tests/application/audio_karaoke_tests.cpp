// A5: the audio box's karaoke mode, pinned against legacy KaraokeSplitting.cpp
// and AudioDisplay's karaoke parts (OnMouseEvent, DoUpdateImage's "Draw
// karaoke", MakeDialogueVisible, Commit) and AudioBox::OnKaraoke at 20d647c4.
// The view is A3's: 48 kHz at zoom 50 (1440 samples, 30 ms a column), 1000
// columns by 128 rows. The label font measures 7 pixels a character, as the
// legacy probe's does.

#include "hikari/application/audio_karaoke.h"
#include "hikari/application/audio_timing.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/text_projection.h"

#include <gtest/gtest.h>

#include <functional>

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string_view>

using namespace hikari;
using namespace hikari::application;

namespace {

AudioView minuteView()
{
    AudioView view;
    view.setSamplesPercent(50, false);
    view.setSource(48000, 48000 * 60);
    view.resize(1000, 150, 22, 16);
    return view;
}

KaraokeMeasure sevenPixels()
{
    return [](std::u16string_view text) { return static_cast<int>(text.size()) * 7; };
}

std::u16string u(std::string_view utf8)
{
    return core::toUtf16(std::u8string_view(reinterpret_cast<const char8_t *>(utf8.data()), utf8.size()));
}

std::string s(std::u16string_view text)
{
    const auto utf8 = core::toUtf8(text);
    return {reinterpret_cast<const char *>(utf8.data()), utf8.size()};
}

std::vector<std::string> strings(const std::vector<std::u16string> &values)
{
    std::vector<std::string> out;
    for (const auto &v : values)
        out.push_back(s(v));
    return out;
}

AudioKaraoke split(std::string_view text, int start, int end, bool autoSplit = false, bool everyN = false)
{
    AudioKaraoke k;
    k.split({u(text), start, end}, autoSplit, KaraokeSplitRules{.everyN = everyN});
    return k;
}

core::Document load(std::string_view text)
{
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return core::loadAss(bytes).document;
}

AudioMouse mouse(AudioMouse::Type type, int x, int y, AudioMouse::Button button = AudioMouse::Button::None)
{
    AudioMouse m;
    m.type = type;
    m.button = button;
    m.x = x;
    m.y = y;
    return m;
}

AudioMouse press(int x, int y = 50, AudioMouse::Button button = AudioMouse::Button::Left)
{
    auto m = mouse(AudioMouse::Type::Press, x, y, button);
    m.leftHeld = button == AudioMouse::Button::Left;
    m.rightHeld = button == AudioMouse::Button::Right;
    m.middleHeld = button == AudioMouse::Button::Middle;
    return m;
}

AudioMouse drag(int x, AudioMouse::Button held = AudioMouse::Button::Left, int y = 50)
{
    auto m = mouse(AudioMouse::Type::Move, x, y);
    m.leftHeld = held == AudioMouse::Button::Left;
    m.rightHeld = held == AudioMouse::Button::Right;
    return m;
}

AudioMouse release(int x, AudioMouse::Button button = AudioMouse::Button::Left, int y = 50)
{
    return mouse(AudioMouse::Type::Release, x, y, button);
}

} // namespace

// ---- the legacy probe's observations ---------------------------------------

namespace {

struct Case {
    std::string name;
    std::string text, tl;
    int start = 0, end = 0;
    bool autoSplit = false, everyN = false;
    std::vector<std::string> ops; // the case file's operation lines
};

std::vector<Case> readCases(const char *path)
{
    std::ifstream in(path);
    std::vector<Case> cases;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const auto space = line.find(' ');
        const std::string op = line.substr(0, space);
        const std::string rest = space == std::string::npos ? std::string() : line.substr(space + 1);
        if (op == "case")
            cases.push_back({rest});
        else if (op == "text")
            cases.back().text = rest;
        else if (op == "tl")
            cases.back().tl = rest;
        else if (op == "times")
            std::istringstream(rest) >> cases.back().start >> cases.back().end;
        else if (op == "auto")
            cases.back().autoSplit = rest == "1";
        else if (op == "everyn")
            cases.back().everyN = rest == "1";
        else if (op != "end" && !op.empty())
            cases.back().ops.push_back(line);
    }
    return cases;
}

QJsonObject state(const AudioKaraoke &k, int curStart)
{
    QJsonObject out;
    QJsonArray syls, tags, times, stripped;
    for (int i = 0; i < k.count(); i++) {
        syls.append(QString::fromStdU16String(k.syllables()[static_cast<std::size_t>(i)]));
        stripped.append(QString::fromStdU16String(k.stripped(i)));
    }
    for (const auto &t : k.tags())
        tags.append(QString::fromStdU16String(t));
    for (const int t : k.times())
        times.append(t);
    out["syls"] = syls;
    out["tags"] = tags;
    out["times"] = times;
    out["stripped"] = stripped;
    out["text"] = QString::fromStdU16String(k.text(curStart));
    return out;
}

} // namespace

namespace {

// The approved departures the capture shows (docs/compatibility-decisions.md):
// where the rewrite's state or answer is not legacy's, the outcome approved.
struct Departure {
    const char *name;
    QJsonObject state; // the keys that differ from legacy's, as the rewrite has them
    QJsonValue result; // an answer that differs, as the rewrite gives it
};
QJsonArray strs(std::initializer_list<const char *> values)
{
    QJsonArray out;
    for (const char *v : values)
        out.append(QString::fromUtf8(v));
    return out;
}
std::string json(const QJsonValue &value)
{
    return QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact).toStdString();
}
const std::map<std::pair<std::string, std::string>, Departure> &departures()
{
    static const std::map<std::pair<std::string, std::string>, Departure> table{
        // legacy kept one time for two syllables and its GetText read past it
        {{"k-unclosed-end", "split"},
         {"A5-kara-unclosed", {{"times", QJsonArray{200, 1000}}, {"text", "{\\k20}ka{\\k80}{0"}}, {}}},
        // legacy split "{}o" into "{" and "}o"
        {{"kf-ko-K", "splitsyl 1 1"},
         {"A5-split-last-letter",
          {{"syls", strs({"kara", "{}o", "", "{}ke"})}, {"stripped", strs({"kara", "o", "", "ke"})},
           {"text", "{\\kf50}kara{\\K25}o{\\k25}{\\k40}ke"}},
          {}}},
        {{"letters-quirk", "letters 0 2"}, {"A5-split-last-letter", {}, strs({"{}ka", ""})}},
        {{"letters-quirk", "letters 1 2"}, {"A5-split-last-letter", {}, strs({"{}ra", ""})}},
        // KA1 (#276): the syllables, times and text the card changes
        {{"k-with-tags", "split"},
         {"KA1-k-round-trip", {{"text", "{\\k20\\fs20}ka{\\fs30\\k30}ra{\\k10}{\\i1}o{\\k40}ke{\\i0}"}}, {}}},
        {{"k-with-tags", "splitsyl 0 2"},
         {"KA1-k-round-trip, KA1-time-by-character", {{"times", QJsonArray{240, 600, 1000}}, {"text", "{\\k24\\fs20}ka{\\k36\\fs30}ra{\\i1}o{\\k40}ke{\\i0}"}}, {}}},
        {{"k-leading-text", "split"},
         {"KA1-k-round-trip", {{"text", "ab{\\k20}cd{\\k30}ef"}}, {}}},
        {{"k-leading-text", "splitsyl 0 1"},
         {"KA1-k-round-trip, KA1-time-by-character", {{"times", QJsonArray{550, 700, 1000}}, {"text", "{\\k5}a{\\k15}b{}cd{\\k30}ef"}}, {}}},
        {{"k-drawing", "split"},
         {"KA1-k-round-trip", {{"text", "{\\k20\\p1}m 0 0 l 10 0 10 10{\\p0\\k30}ra"}}, {}}},
        {{"k-drawing", "splitsyl 0 3"},
         {"KA1-k-round-trip, KA1-time-by-character", {{"times", QJsonArray{30, 200, 500}}, {"text", "{\\k3\\p1}m 0{\\k17} 0 l 10 0 10 10{\\p0\\k30}ra"}}, {}}},
        // legacy split "{}ka" into "{}" and "ka", and the next split followed it
        {{"k-split-end", "splitsyl 0 2"},
         {"A5-split-last-letter, KA1-time-by-character", {{"syls", strs({"{}ka", ""})}, {"times", QJsonArray{130, 200}}, {"stripped", strs({"ka", ""})}, {"text", "{\\k13}ka{\\k7}"}}, {}}},
        {{"k-split-end", "splitsyl 0 0"},
         {"A5-split-last-letter, KA1-time-by-character", {{"syls", strs({"", "{}ka", ""})}, {"times", QJsonArray{40, 130, 200}}, {"stripped", strs({"", "ka", ""})}, {"text", "{\\k4}{\\k9}ka{\\k7}"}}, {}}},
        {{"auto-kara", "split"},
         {"KA1-time-by-character", {{"times", QJsonArray{280, 570, 710, 1000}}, {"text", "{\\k28}ka{\\k29}ra{\\k14}o{\\k29}ke"}}, {}}},
        {{"auto-konnichiwa", "split"},
         {"KA1-time-by-character", {{"times", QJsonArray{220, 330, 550, 880, 1110, 1330, 1550, 1770, 2000}}, {"text", "{\\k22}ko{\\k11}n{\\k22}ni{\\k33}chi{\\k23}wa {\\k22}sa{\\k22}yo{\\k22}na{\\k23}ra"}}, {}}},
        {{"auto-n-everyn-off", "split"},
         {"KA1-time-by-character", {{"times", QJsonArray{360, 540, 1090, 1270, 1630, 2000}}, {"text", "{\\k36}ka{\\k18}n{\\k55}tan {\\k18}a{\\k36}ni{\\k37}me"}}, {}}},
        {{"auto-n-everyn-on", "split"},
         {"KA1-time-by-character", {{"times", QJsonArray{540, 1090, 1270, 1630, 2000}}, {"text", "{\\k54}kan{\\k55}tan {\\k18}a{\\k36}ni{\\k37}me"}}, {}}},
        {{"auto-punct", "split"},
         {"KA1-auto-split-language, KA1-time-by-character", {{"syls", strs({"hello, ", "world! ", "\"quoted\" ", "end."})}, {"tags", strs({"k", "k", "k", "k"})}, {"times", QJsonArray{780, 1570, 2520, 3000}}, {"stripped", strs({"hello, ", "world! ", "\"quoted\" ", "end."})}, {"text", "{\\k78}hello, {\\k79}world! {\\k95}\"quoted\" {\\k48}end."}}, {}}},
        {{"auto-breaks", "split"},
         {"KA1-auto-split-language, KA1-time-by-character", {{"syls", strs({"ka\\N", "ra\\h", "o", "ke\\n", "end"})}, {"tags", strs({"k", "k", "k", "k", "k"})}, {"times", QJsonArray{400, 800, 1000, 1400, 2000}}, {"stripped", strs({"ka\\N", "ra\\h", "o", "ke\\n", "end"})}, {"text", "{\\k40}ka\\N{\\k40}ra\\h{\\k20}o{\\k40}ke\\n{\\k60}end"}}, {}}},
        {{"auto-tags", "split"},
         {"KA1-time-by-character", {{"times", QJsonArray{280, 570, 710, 1000}}, {"text", "{\\k28\\b1}ka{\\k29\\b0}ra {\\k14\\i1}o{\\k29}ke{\\i0}"}}, {}}},
        // legacy lost "b{c"; KA1 reads "ab" as no romaji, one word
        {{"auto-unclosed", "split"},
         {"A5-auto-unclosed, KA1-auto-split-language", {{"syls", strs({"ab{c"})}, {"stripped", strs({"abab{c"})}, {"text", "{\\k100}ab{c"}}, {}}},
        {{"auto-uppercase", "split"},
         {"KA1-time-by-character", {{"times", QJsonArray{180, 360, 450, 630, 810, 1000}}, {"text", "{\\k18}KA{\\k18}RA{\\k9}O{\\k18}KE {\\k18}Na{\\k19}ni"}}, {}}},
        {{"manual-spaces", "split"},
         {"KA1-time-by-character", {{"times", QJsonArray{370, 750, 870, 1500}}, {"text", "{\\k37}one {\\k38}two {\\k12} {\\k63}three"}}, {}}},
        {{"manual-breaks", "split"},
         {"KA1-time-by-character", {{"times", QJsonArray{400, 800, 1460, 2000}}, {"text", "{\\k40}one\\N{\\k40}two\\h{\\k66}three\\n{\\k54}four"}}, {}}},
        {{"manual-tags", "split"},
         {"KA1-time-by-character", {{"times", QJsonArray{240, 490, 900}}, {"text", "{\\k24\\b1}one {\\k25\\i1}two{\\i0} {\\k41}three"}}, {}}},
        {{"manual-tags", "splitsyl 1 1"},
         {"KA1-time-by-character", {{"times", QJsonArray{240, 320, 490, 900}}, {"text", "{\\k24\\b1}one {\\k8\\i1}t{\\k17}wo{\\i0} {\\k41}three"}}, {}}},
        {{"manual-nonascii", "split"},
         {"KA1-auto-split-language, KA1-time-by-character", {{"syls", strs({"\xC5\xBC\xC3\xB3\xC5\x82w ", "\xC3\xA9 ", "caf\xC3\xA9 ", "na\xC3\xAFve"})}, {"tags", strs({"k", "k", "k", "k"})}, {"times", QJsonArray{280, 350, 640, 1000}}, {"stripped", strs({"\xC5\xBC\xC3\xB3\xC5\x82w ", "\xC3\xA9 ", "caf\xC3\xA9 ", "na\xC3\xAFve"})}, {"text", "{\\k28}\xC5\xBC\xC3\xB3\xC5\x82w {\\k7}\xC3\xA9 {\\k29}caf\xC3\xA9 {\\k36}na\xC3\xAFve"}}, {}}},
    };
    return table;
}

} // namespace

// Every case of the probe replayed through AudioKaraoke: each state and
// answer is legacy's, but for the approved departures above (the README).
TEST(AudioKaraokeCapture, ReplaysTheLegacyObservations)
{
    const auto cases = readCases(HIKARI_KARAOKE_CASES);
    ASSERT_GE(cases.size(), 30u);
    QFile file(QStringLiteral(HIKARI_KARAOKE_OBSERVATIONS));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    std::map<std::string, QJsonArray> observed;
    for (const QByteArray &line : file.readAll().split('\n'))
        if (!line.trimmed().isEmpty()) {
            const auto object = QJsonDocument::fromJson(line).object();
            observed[object["case"].toString().toStdString()] = object["ops"].toArray();
        }
    ASSERT_EQ(observed.size(), cases.size());
    const AudioView view = minuteView();
    int compared = 0, departed = 0;
    for (const auto &c : cases) {
        SCOPED_TRACE(c.name);
        const auto ops = observed.at(c.name);
        AudioKaraoke k;
        k.split({u(c.tl.empty() ? c.text : c.tl), c.start, c.end}, c.autoSplit, KaraokeSplitRules{.everyN = c.everyN});
        int curStart = c.start;
        auto compare = [&](const QJsonObject &expected, const std::string &op, const QJsonValue &result) {
            SCOPED_TRACE(op);
            auto actual = state(k, curStart);
            QJsonObject legacy = expected["state"].toObject();
            const auto found = departures().find({c.name, op});
            if (found != departures().end()) {
                const Departure &d = found->second;
                SCOPED_TRACE(d.name);
                ++departed;
                for (auto it = d.state.begin(); it != d.state.end(); ++it) {
                    EXPECT_NE(legacy[it.key()], it.value()) << it.key().toStdString(); // the premise
                    EXPECT_EQ(json(actual[it.key()]), json(it.value())) << it.key().toStdString();
                    actual.remove(it.key());
                    legacy.remove(it.key());
                }
                if (!d.result.isNull()) {
                    EXPECT_NE(expected["result"], d.result); // legacy's answer is the departure's premise
                    EXPECT_EQ(result, d.result);
                } else if (expected.contains("result")) {
                    EXPECT_EQ(result, expected["result"]);
                }
            } else if (expected.contains("result")) {
                EXPECT_EQ(result, expected["result"]);
            }
            EXPECT_EQ(QJsonDocument(actual).toJson(QJsonDocument::Compact).toStdString(),
                      QJsonDocument(legacy).toJson(QJsonDocument::Compact).toStdString());
            ++compared;
        };
        ASSERT_EQ(ops.size(), qsizetype(c.ops.size()) + 1);
        compare(ops[0].toObject(), "split", {});
        for (std::size_t i = 0; i < c.ops.size(); i++) {
            std::istringstream in(c.ops[i]);
            std::string op;
            in >> op;
            const auto expected = ops[qsizetype(i) + 1].toObject();
            QJsonValue result;
            if (op == "join") {
                int at = 0;
                in >> at;
                k.join(at);
            } else if (op == "splitsyl") {
                int at = 0, n = 0;
                in >> at >> n;
                result = k.splitSyllable(at, n, curStart);
            } else if (op == "settime") {
                int at = 0, ms = 0;
                in >> at >> ms;
                k.times()[static_cast<std::size_t>(at)] = ms;
            } else if (op == "curstart") {
                in >> curStart;
            } else if (op == "letters") {
                int at = 0, n = 0;
                in >> at >> n;
                const auto [first, second] = k.letters(at, n);
                result = QJsonArray{QString::fromStdU16String(first), QString::fromStdU16String(second)};
            } else if (op == "sylat" || op == "over" || op == "letterat") {
                int x = 0;
                in >> x;
                int syl = -1, r = -1;
                if (op == "sylat")
                    r = k.syllableAt(x, view, curStart);
                else if (op == "over")
                    r = k.boundaryAt(x, view);
                else if (const auto letter = k.letterAt(x, view, curStart, sevenPixels()))
                    std::tie(syl, r) = *letter;
                result = QJsonArray{r >= 0, syl, r};
            } else {
                FAIL() << "unknown op " << op;
            }
            compare(expected, c.ops[i], result);
        }
    }
    EXPECT_GE(compared, 60);
    EXPECT_EQ(departed, static_cast<int>(departures().size())); // each one met
}

// ---- the syllable model ------------------------------------------------------

TEST(AudioKaraoke, KTagsGiveTheTimesAndTagKinds)
{
    auto k = split("{\\kf20}ka{\\ko30}ra{\\K50}o{\\k40}ke", 1000, 3000);
    EXPECT_EQ(strings(k.syllables()), (std::vector<std::string>{"{}ka", "{}ra", "{}o", "{}ke"}));
    EXPECT_EQ(strings(k.tags()), (std::vector<std::string>{"kf", "ko", "K", "k"}));
    EXPECT_EQ(k.times(), (std::vector<int>{1200, 1500, 2000, 2400})); // from the Line's start, not its end
    EXPECT_EQ(s(k.text(1000)), "{\\kf20}ka{\\ko30}ra{\\K50}o{\\k40}ke");
    // GetText measures the first syllable from the display's start
    EXPECT_EQ(s(k.text(900)), "{\\kf30}ka{\\ko30}ra{\\K50}o{\\k40}ke");
    // \K in capitals counts (the regex reads the lower-cased text)
    EXPECT_EQ(split("{\\K15}ka", 0, 500).times(), std::vector<int>{150});
}

TEST(AudioKaraoke, TagsAndDrawingsStayWithTheirSyllables)
{
    auto k = split("{\\k20\\p1}m 0 0 l 10 0 10 10{\\p0\\k30}ra", 0, 1000);
    EXPECT_EQ(strings(k.syllables()), (std::vector<std::string>{"{\\p1}m 0 0 l 10 0 10 10", "{\\p0}ra"}));
    EXPECT_EQ(s(k.stripped(0)), "m 0 0 l 10 0 10 10"); // a drawing is text to the splitter
    // KA1-k-round-trip (legacy put each \k first in its block)
    EXPECT_EQ(s(k.text(0)), "{\\k20\\p1}m 0 0 l 10 0 10 10{\\p0\\k30}ra");
    EXPECT_EQ(s(split("{\\fs30\\k30}ra", 0, 1000).text(0)), "{\\fs30\\k30}ra");
}

TEST(AudioKaraoke, JoinTakesTheNextSyllableAndDropsEmptyBlocks)
{
    auto k = split("{\\kf20}ka{\\ko30}ra{\\K50}o", 0, 1000);
    ASSERT_TRUE(k.join(0));
    EXPECT_EQ(strings(k.syllables()), (std::vector<std::string>{"kara", "{}o"}));
    EXPECT_EQ(strings(k.tags()), (std::vector<std::string>{"kf", "K"})); // the first keeps its tag
    EXPECT_EQ(k.times(), (std::vector<int>{500, 1000}));
    EXPECT_EQ(s(k.text(0)), "{\\kf50}kara{\\K50}o");
    // A5-join-last (proposed): the last syllable has nothing after it
    EXPECT_FALSE(k.join(1));
    EXPECT_FALSE(k.join(-1));
    EXPECT_EQ(k.count(), 2);
}

// KA8 (#354): a kanji Line with the Kanji timer's # syllables. Each is a
// syllable of its own time, labelled "#" in the strip as the templater reads
// it (one more highlight of the one before), and the Line reads back as it was.
TEST(AudioKaraoke, MultiHighlightHashSyllablesAreLabelledAsWritten)
{
    const std::string line = "{\\k9}撃、{\\k7}#{\\k6}腐{\\kf9}#{\\K9}＃";
    auto k = split(line, 0, 400);
    ASSERT_EQ(k.count(), 5);
    std::vector<std::string> labels;
    for (int i = 0; i < k.count(); ++i)
        labels.push_back(s(k.stripped(i)));
    EXPECT_EQ(labels, (std::vector<std::string>{"撃、", "#", "腐", "#", "＃"}));
    EXPECT_EQ(strings(k.tags()), (std::vector<std::string>{"k", "k", "k", "kf", "K"}));
    EXPECT_EQ(k.times(), (std::vector<int>{90, 160, 220, 310, 400}));
    EXPECT_EQ(s(k.text(0)), line);
}

// KA8-join-multi-highlight: a joined syllable is a # syllable only when
// both halves were; otherwise the highlight folds back into the other one
TEST(AudioKaraoke, JoinWithAHashSyllableFoldsItsHighlightBack)
{
    const std::string line = "{\\k9}撃{\\k7}#{\\k6}腐{\\k9}{\\1c&H0000FF&}＃{\\k9}#{\\k12}蝕";
    auto k = split(line, 0, 520);
    ASSERT_EQ(k.count(), 6);
    ASSERT_TRUE(k.join(0)); // 撃 + #
    EXPECT_EQ(strings(k.syllables()), (std::vector<std::string>{"撃", "{}腐", "{}{\\1c&H0000FF&}＃", "{}#", "{}蝕"}));
    EXPECT_EQ(k.times(), (std::vector<int>{160, 220, 310, 400, 520})); // times as Join does them
    ASSERT_TRUE(k.join(2)); // ＃ + #: one # syllable
    EXPECT_EQ(strings(k.syllables()), (std::vector<std::string>{"撃", "{}腐", "{\\1c&H0000FF&}＃", "{}蝕"}));
    ASSERT_TRUE(k.join(2)); // ＃ + 蝕, the # syllable's block kept
    EXPECT_EQ(strings(k.syllables()), (std::vector<std::string>{"撃", "{}腐", "{\\1c&H0000FF&}蝕"}));
    EXPECT_EQ(k.times(), (std::vector<int>{160, 220, 520}));
    EXPECT_EQ(s(k.text(0)), "{\\k16}撃{\\k6}腐{\\k30}{\\1c&H0000FF&}蝕");
    // a # among other text is no # syllable: joined as it was
    auto plain = split("{\\k10}a{\\k10}#b", 0, 200);
    ASSERT_TRUE(plain.join(0));
    EXPECT_EQ(strings(plain.syllables()), (std::vector<std::string>{"a#b"}));
}

// KA2's taps replace the syllables: their tags go in their own places,
// not where the Line's read ones were (KA1-k-round-trip)
// Codex: a hovered letter is the reading's it was found in; another Line's
// (or the same Line read again) has none until the pointer moves
TEST(AudioKaraoke, ANewReadingHasNoHoveredLetter)
{
    auto k = split("{\\k30}hana{\\k20}ka", 1000, 2000);
    k.hover = 0;
    k.character = 1;
    k.split({u"{\\k30}h{\\k20}ana", 1000, 2000}, false, {});
    EXPECT_EQ(k.hover, -1);
    EXPECT_EQ(k.character, -1);
}

TEST(AudioKaraoke, AssignedSyllablesTakeTheirOwnPlaces)
{
    AudioKaraoke k;
    k.split({u"{\\b1}ki{\\k30}mi{\\k40}no", 0, 1000}, false, {});
    k.assign({u"ki", u"mi", u"no"}, {u"k", u"k", u"kf"}, {150, 400, 1000});
    EXPECT_EQ(s(k.text(0)), "{\\k15}ki{\\k25}mi{\\kf60}no");
}

TEST(AudioKaraoke, SplittingASyllableSharesItsTimeByCharacter)
{
    auto k = split("{\\k25}kara", 0, 1000);
    ASSERT_TRUE(k.splitSyllable(0, 2, 0));
    EXPECT_EQ(strings(k.syllables()), (std::vector<std::string>{"{}ka", "ra"}));
    EXPECT_EQ(k.times(), (std::vector<int>{120, 250})); // ZEROIT(0 + 250 * 2 / 4)
    EXPECT_EQ(s(k.text(0)), "{\\k12}ka{\\k13}ra");
    // KA1-time-by-character (legacy: half way, 120)
    auto by = split("{\\k30}sakura", 0, 1000);
    ASSERT_TRUE(by.splitSyllable(0, 2, 0));
    EXPECT_EQ(by.times(), (std::vector<int>{100, 300})); // "sa" of "sakura": 2 of 6
    auto spaces = split("{\\k40}ka, ra", 0, 1000); // spaces and punctuation take none
    ASSERT_TRUE(spaces.splitSyllable(0, 4, 0));
    EXPECT_EQ(strings(spaces.syllables()), (std::vector<std::string>{"{}ka, ", "ra"}));
    EXPECT_EQ(spaces.times(), (std::vector<int>{200, 400}));
    // the letter count skips blocks; nothing to split in an empty syllable
    ASSERT_TRUE(k.splitSyllable(0, 0, 0));
    EXPECT_EQ(s(k.syllables()[0]), "");
    EXPECT_FALSE(k.splitSyllable(0, 0, 0));
    // A5-split-last-letter (approved): after the last letter the split is
    // right after it (legacy split at the raw position: "{\k10}{{\k10}}o")
    auto last = split("{\\k20}o", 0, 1000);
    ASSERT_TRUE(last.splitSyllable(0, 1, 0));
    EXPECT_EQ(s(last.text(0)), "{\\k10}o{\\k10}");
    auto tags = split("{\\k20}ke{\\i0}", 0, 1000);
    EXPECT_EQ(tags.letters(0, 2), std::pair(std::u16string(u"{}ke"), std::u16string(u"{\\i0}")));
    EXPECT_EQ(tags.letters(0, 5), std::pair(std::u16string(u"{}ke"), std::u16string(u"{\\i0}")));
}

TEST(AudioKaraoke, RetimingAndTheGetTextRounding)
{
    auto k = split("{\\k20}ka{\\k30}ra", 1000, 2000);
    k.times()[0] = 1055;
    EXPECT_EQ(s(k.text(1000)), "{\\k5}ka{\\k44}ra"); // each length in whole centiseconds, truncated
    k.times()[0] = 990;                                // before the display's start
    EXPECT_EQ(s(k.text(1000)), "{\\k-1}ka{\\k51}ra");
}

TEST(AudioKaraoke, AutomaticAndSpaceSplits)
{
    EXPECT_EQ(strings(split("karaoke", 0, 1000, true).syllables()), (std::vector<std::string>{"ka", "ra", "o", "ke"}));
    // n before a vowel opens the next syllable; vowel+n stays one with every n merged
    EXPECT_EQ(strings(split("kantan anime", 0, 2000, true).syllables()),
              (std::vector<std::string>{"ka", "n", "tan ", "a", "ni", "me"}));
    EXPECT_EQ(strings(split("kantan anime", 0, 2000, true, true).syllables()),
              (std::vector<std::string>{"kan", "tan ", "a", "ni", "me"}));
    // spaces, and \N \h \n, end a syllable; the last ends at the Line's end
    const auto manual = split("one two\\Nthree", 0, 1000);
    EXPECT_EQ(strings(manual.syllables()), (std::vector<std::string>{"one ", "two\\N", "three"}));
    // KA1-time-by-character (legacy: a third each, 330 660)
    EXPECT_EQ(manual.times(), (std::vector<int>{270, 540, 1000}));
    // a Line without a split is one syllable
    EXPECT_EQ(strings(split("x", 0, 500).syllables()), std::vector<std::string>{"x"});
    // \k without a digit is not karaoke
    EXPECT_EQ(strings(split("{\\k}a b", 0, 500).syllables()), (std::vector<std::string>{"{\\k}a ", "b"}));
}

TEST(AudioKaraoke, TheCharacterClassesComeFromTheCaller)
{
    // "a·b": the middle dot is punctuation to Windows (C1_PUNCT), not to ASCII
    const auto ascii = split("ka\xC2\xB7ra", 0, 1000, true);
    EXPECT_EQ(strings(ascii.syllables()), (std::vector<std::string>{"ka", "\xC2\xB7ra"}));
    AudioKaraoke k;
    auto classes = KaraokeCharClass::ascii();
    classes.punct = [](char16_t c) { return c == u'·' || KaraokeCharClass::ascii().punct(c); };
    k.split({u("ka\xC2\xB7ra"), 0, 1000}, true, {}, classes);
    EXPECT_EQ(strings(k.syllables()), (std::vector<std::string>{"ka\xC2\xB7", "ra"}));
}

TEST(AudioKaraoke, DeparturesAndKeptLosses)
{
    // A5-kara-unclosed (approved): the missing time ends at the Line's end
    const auto unclosed = split("{\\k20}ka{\\k30", 0, 1000);
    EXPECT_EQ(unclosed.times(), (std::vector<int>{200, 1000}));
    EXPECT_EQ(unclosed.tags().size(), 2u);
    // A5-auto-unclosed (approved): an unclosed block stays in the last
    // syllable (legacy lost it: "ab{c" gave "{\k100}a"; KA1 reads "ab" as no
    // romaji, one syllable)
    EXPECT_EQ(s(split("ab{c", 0, 1000, true).text(0)), "{\\k100}ab{c");
    EXPECT_EQ(s(split("kara{c", 0, 1000, true).text(0)), "{\\k50}ka{\\k50}ra{c");
    EXPECT_EQ(s(split("ka{c", 0, 1000, true).text(0)), "{\\k100}ka{c"); // no split: the whole text, as legacy
    EXPECT_EQ(s(split("ab{c", 0, 1000, false).text(0)), "{\\k100}ab{c");
}

TEST(AudioKaraoke, TheSplitComparesTheCallersLowerCase)
{
    // U1-unicode-case: the UI's lower case is Unicode's; ASCII's folds A-Z only
    EXPECT_EQ(strings(split("KARA", 0, 1000, true).syllables()), (std::vector<std::string>{"KA", "RA"}));
    const std::string text = "K\xC3\x81RA"; // KÁRA: neither lower case makes Á an "a"
    EXPECT_EQ(strings(split(text, 0, 1000, true).syllables()), (std::vector<std::string>{"K\xC3\x81RA"}));
    auto classes = KaraokeCharClass::ascii();
    classes.lower = [](char16_t c) { return c == u'\u0130' ? u'i' : KaraokeCharClass::ascii().lower(c); };
    AudioKaraoke k; // "İKİ": İ lowers to i, a vowel
    k.split({u"\u0130K\u0130", 0, 900}, true, {}, classes);
    EXPECT_EQ(strings(k.syllables()), (std::vector<std::string>{"\xC4\xB0", "K\xC4\xB0"}));
    k.split({u"\u0130K\u0130", 0, 900}, true, {});
    EXPECT_EQ(k.count(), 1);
}

TEST(AudioKaraoke, LegacyHelpers)
{
    EXPECT_EQ(legacyAtoi(u"20"), 20);
    EXPECT_EQ(legacyAtoi(u" -5x"), -5);
    EXPECT_EQ(legacyAtoi(u"x5"), 0);
    EXPECT_EQ(legacyAtoi(u""), 0);
    EXPECT_EQ(legacyAtoi(u"99999999999"), 2147483647);
    EXPECT_EQ(legacyTextExtent(sevenPixels(), u"ab"), 14);
    EXPECT_EQ(legacyTextExtent(sevenPixels(), u" ab "), 36);
    EXPECT_EQ(legacyTextExtent(sevenPixels(), u" "), 15);
    // the label measure leaves the edge spaces to the +4s
    const auto label = karaokeLabelMeasure([](AudioShape::Font, std::string_view text) {
        return static_cast<int>(text.size()) * 10;
    });
    EXPECT_EQ(label(u"  ab "), 20);
    EXPECT_EQ(label(u"   "), 0);
}

TEST(AudioKaraoke, TheKaraokeButtonsZoom)
{
    int last = -1;
    EXPECT_EQ(legacyKaraokeZoom(false, 50, last), 70); // never on in this box: out by 20
    EXPECT_EQ(legacyKaraokeZoom(false, 60, last), 70); // not past 70
    EXPECT_EQ(legacyKaraokeZoom(true, 64, last), 44);
    EXPECT_EQ(last, 64);
    EXPECT_EQ(legacyKaraokeZoom(false, 44, last), 64); // back to where it was
    EXPECT_EQ(legacyKaraokeZoom(true, 40, last), 30);  // not below 30
}

TEST(AudioKaraoke, SyllableTimesForPlayingAndShowing)
{
    const auto k = split("{\\k60}ka{\\k90}ra{\\k150}oke", 3000, 6000);
    EXPECT_EQ(k.syllableTimes(0, 3000), std::pair(3000, 3600));
    EXPECT_EQ(k.syllableTimes(0, 2500), std::pair(2500, 3600)); // the display's start
    EXPECT_EQ(k.syllableTimes(2, 3000), std::pair(4500, 6000));
    EXPECT_EQ(k.visibleTimes(0, 3000, 7000), std::pair(3000, 4500)); // to the next one's end
    EXPECT_EQ(k.visibleTimes(2, 3000, 7000), std::pair(4500, 7000)); // the last: the display's end
}

TEST(AudioKaraoke, MakeVisibleCentresTheSyllable)
{
    auto view = minuteView(); // 1000 columns of 30 ms
    view.makeVisibleKaraoke(3000, 4500, 16);
    EXPECT_EQ(view.position(), 0); // columns 100-150: in view
    view.makeVisibleKaraoke(30000, 30600, 16);
    // centred: (start + end - w * samples) / 2 samples
    EXPECT_EQ(view.positionSample(), (1440000 + 1468800 - 1000 * 1440) / 2);
    const auto before = view.positionSample();
    view.makeVisibleKaraoke(30000, 30600, 16); // now in view: nothing moves
    EXPECT_EQ(view.positionSample(), before);
}

// ---- the drawing -------------------------------------------------------------

TEST(AudioKaraoke, TheSyllablesAreDrawnWithTheBoundaries)
{
    const auto view = minuteView();
    auto k = split("{\\k60}ka{\\k90}ra{\\k150}oke", 3000, 6000);
    AudioKaraokeMarks marks;
    marks.times = k.times();
    for (int i = 0; i < k.count(); i++)
        marks.stripped.push_back(k.stripped(i));
    marks.current = 1;
    marks.hover = 2;
    marks.character = 1;
    marks.curStartMs = 3000;
    std::vector<AudioShape> out;
    karaokeShapes(out, view, 100, marks, sevenPixels(), 13, 0xFF202225, 0xFF8791FD);
    auto at = [&](std::size_t i) { return out.at(i); };
    // syllable 0: its boundary at column 120, its bar and text centred in 100-120
    EXPECT_EQ(at(0).kind, AudioShape::Kind::Line);
    EXPECT_FLOAT_EQ(at(0).x1, 120);
    EXPECT_FLOAT_EQ(at(0).y2, 128);
    EXPECT_EQ(at(0).colour, 0xFF202225u);
    // center = ((120 - 100) - 14) / 2 = 3: the bar from 102 to 119 at y 7, 13 wide
    EXPECT_FLOAT_EQ(at(1).x1, 102);
    EXPECT_FLOAT_EQ(at(1).x2, 119);
    EXPECT_FLOAT_EQ(at(1).y1, 7);
    EXPECT_FLOAT_EQ(at(1).width, 13);
    EXPECT_EQ(at(2).kind, AudioShape::Kind::Text);
    EXPECT_EQ(at(2).text, "ka");
    EXPECT_FLOAT_EQ(at(2).x1, 103);
    EXPECT_EQ(at(2).font, AudioShape::Font::Label);
    EXPECT_EQ(at(2).colour, 0xFF8791FD);
    // syllable 1 is current: its frame from 122 to 148
    EXPECT_FLOAT_EQ(at(3).x1, 150); // its boundary
    EXPECT_EQ(at(5).text, "ra");
    EXPECT_FLOAT_EQ(at(6).x1, 122);
    EXPECT_FLOAT_EQ(at(6).y1, 1);
    EXPECT_FLOAT_EQ(at(6).y2, 126);
    EXPECT_FLOAT_EQ(at(8).x1, 148);
    // syllable 2 ("oke", 21 wide) with the hovered letter's mark after "o"
    EXPECT_FLOAT_EQ(at(10).x1, 200);
    EXPECT_EQ(at(12).text, "oke");
    EXPECT_FLOAT_EQ(at(12).x1, 164); // ((200 - 150) - 21) / 2 = 14
    EXPECT_FLOAT_EQ(at(13).x1, 171); // 150 + (50 - 21) / 2 + 7
    EXPECT_FLOAT_EQ(at(13).y2, 13);
    EXPECT_EQ(out.size(), 14u);
}

TEST(AudioKaraoke, TheCursorTimeMovesUnderTheSyllables)
{
    const auto view = minuteView();
    const AudioDisplayOptions options;
    EXPECT_FLOAT_EQ(audioCursor(view, 300, false, options).at(1).y1, 5);
    EXPECT_FLOAT_EQ(audioCursor(view, 300, false, options, true).at(1).y1, 20);
}

TEST(AudioKaraoke, TheSceneDrawsKaraokeOnlyInKaraokeMode)
{
    const auto view = minuteView();
    AudioMarks marks;
    marks.startMs = 3000;
    marks.endMs = 6000;
    marks.markTextHeight = 13;
    const auto width = [](AudioShape::Font, std::string_view text) { return static_cast<int>(text.size()) * 7; };
    auto texts = [&] {
        std::vector<std::string> out;
        for (const auto &shape : audioScene(view, {}, marks, AudioDisplayOptions{}, width))
            if (shape.kind == AudioShape::Kind::Text && shape.font == AudioShape::Font::Label)
                out.push_back(shape.text);
        return out;
    };
    EXPECT_TRUE(texts().empty());
    auto karaoke = std::make_shared<AudioKaraokeMarks>();
    karaoke->times = {3600, 6000};
    karaoke->stripped = {u"ka", u"ra"};
    marks.karaoke = karaoke;
    EXPECT_EQ(texts(), (std::vector<std::string>{"ka", "ra"}));
}

// ---- the mouse ---------------------------------------------------------------

namespace {

// The active Line 3000-6000 (columns 100-200) split into ka 3000-3600
// (100-120), ra 3600-4500 (120-150) and oke 4500-6000 (150-200).
struct KaraokeMouseTest : ::testing::Test {
    AudioView view = minuteView();
    AudioTiming timing;
    AudioTiming::Selection selection{3000, 6000, false};
    AudioTimingOptions options;
    std::vector<AudioLineSpan> lines{{3000, 6000}};
    AudioSnapContext snap{&view, false, false, true, 1, {}, {}, lines, 0};
    AudioKaraoke karaoke = split("{\\k60}ka{\\k90}ra{\\k150}oke", 3000, 6000);
    bool on = true;
    bool moveOnClick = false;
    std::function<int(int)> fieldTime;
    std::vector<int> onsets; // KA5: snapping on with these onsets

    void SetUp() override { timing.drawn(view, selection); }
    AudioMouseResult send(const AudioMouse &event)
    {
        AudioKaraokeMouse kara;
        if (on)
            kara = {&karaoke, moveOnClick, sevenPixels(), fieldTime, onsets};
        auto result = timing.mouse(event, view, selection, snap, options, 16, kara);
        timing.drawn(view, selection);
        return result;
    }
};

} // namespace

TEST_F(KaraokeMouseTest, ABoundaryIsGrabbedDraggedAndCommitted)
{
    auto r = send(drag(121, AudioMouse::Button::None));
    EXPECT_EQ(timing.grabbed(), 0);
    EXPECT_EQ(r.sizeCursor, std::optional(true));
    send(press(121));
    EXPECT_EQ(timing.hold(), 5);
    r = send(drag(130));
    EXPECT_EQ(karaoke.times()[0], 3900);
    EXPECT_EQ(r.playEnd, std::optional<std::int64_t>(3900 * 48)); // the player's end follows the boundary
    EXPECT_FALSE(selection.modified); // the selection is not "Modified"
    EXPECT_FALSE(r.commit);
    send(drag(160)); // not past the next boundary
    EXPECT_EQ(karaoke.times()[0], 4500);
    r = send(release(131));
    EXPECT_EQ(karaoke.times()[0], 3930);
    EXPECT_EQ(karaoke.current, 0);
    EXPECT_EQ(r.commit, std::optional(false)); // Commit()
    EXPECT_EQ(timing.hold(), 0);
    EXPECT_EQ(timing.grabbed(), 0); // legacy keeps Grabbed
    EXPECT_EQ(s(karaoke.text(3000)), "{\\k93}ka{\\k57}ra{\\k150}oke");
}

TEST_F(KaraokeMouseTest, TheLastBoundaryTakesTheEndWithIt)
{
    send(press(200));
    EXPECT_EQ(timing.hold(), 5);
    EXPECT_EQ(timing.grabbed(), 2);
    send(drag(220));
    EXPECT_EQ(karaoke.times()[2], 6600);
    EXPECT_EQ(selection.endMs, 6600);
    send(release(220));
    EXPECT_EQ(karaoke.times()[2], 6600);
}

TEST_F(KaraokeMouseTest, TheRightButtonMovesTheLaterSyllablesToo)
{
    send(press(120, 50, AudioMouse::Button::Right));
    send(drag(125, AudioMouse::Button::Right));
    EXPECT_EQ(karaoke.times(), (std::vector<int>{3750, 4650, 6000})); // all but the last
    EXPECT_EQ(selection.endMs, 6000);
    // the start dragged with the right button moves all but the last syllable
    send(release(125, AudioMouse::Button::Right));
    send(press(100, 50, AudioMouse::Button::Right));
    EXPECT_EQ(timing.hold(), 1);
    send(drag(110, AudioMouse::Button::Right));
    EXPECT_EQ(selection.startMs, 3300);
    EXPECT_EQ(karaoke.times(), (std::vector<int>{4050, 4950, 6000}));
}

TEST_F(KaraokeMouseTest, MiddleOrShiftClickJoinsAtABoundary)
{
    auto r = send(press(150, 50, AudioMouse::Button::Middle));
    EXPECT_EQ(strings(karaoke.syllables()), (std::vector<std::string>{"{}ka", "raoke"}));
    EXPECT_EQ(r.commit, std::optional(false));
    EXPECT_TRUE(r.keepCursor);
    EXPECT_EQ(timing.hold(), 0);
    auto shift = press(120);
    shift.shift = true;
    send(shift);
    EXPECT_EQ(strings(karaoke.syllables()), std::vector<std::string>{"karaoke"});
    // A5-join-last (proposed): at the last boundary nothing happens
    shift.x = 200;
    r = send(shift);
    EXPECT_FALSE(r.commit);
    EXPECT_EQ(karaoke.count(), 1);
}

TEST_F(KaraokeMouseTest, AClickOnTheLettersSplitsTheSyllable)
{
    // "ra" (14 wide) in 120-150: centred at 128, letters change at 131 and 137
    auto r = send(drag(130, AudioMouse::Button::None, 10));
    EXPECT_EQ(karaoke.hover, 1);
    EXPECT_EQ(karaoke.character, 0);
    EXPECT_TRUE(r.hideCursor); // no mouse cursor over the letters
    send(drag(134, AudioMouse::Button::None, 10));
    EXPECT_EQ(karaoke.character, 1);
    r = send(press(134, 10));
    EXPECT_EQ(strings(karaoke.syllables()), (std::vector<std::string>{"{}ka", "{}r", "a", "{}oke"}));
    EXPECT_EQ(karaoke.times(), (std::vector<int>{3600, 4050, 4500, 6000}));
    EXPECT_EQ(karaoke.current, 1);
    EXPECT_EQ(r.commit, std::optional(false));
    EXPECT_TRUE(r.keepView); // KA1-split-keeps-view: the view stays where it is
    // below the letters the cursor is drawn again
    r = send(drag(134, AudioMouse::Button::None, 50));
    EXPECT_FALSE(r.hideCursor);
    EXPECT_EQ(karaoke.hover, -1);
    EXPECT_EQ(karaoke.character, -1);
}

// KA1 (Codex): after a start the format moves (3010 committed as 3040), a
// click on a letter divides the syllable's time from the committed start,
// as the text is written
TEST_F(KaraokeMouseTest, AClickSplitsFromTheCommittedStart)
{
    fieldTime = [](int ms) { return (ms + 39) / 40 * 40; };
    karaoke = split("{\\k56}ka{\\k90}ra{\\k150}oke", 3040, 6000);
    selection.startMs = 3010;
    timing.drawn(view, selection);
    int x = 100;
    for (; x < 120; x++) {
        send(drag(x, AudioMouse::Button::None, 10));
        if (karaoke.hover == 0 && karaoke.character == 1)
            break;
    }
    ASSERT_LT(x, 120);
    send(press(x, 10));
    EXPECT_EQ(karaoke.times(), (std::vector<int>{3320, 3600, 4500, 6000})); // half of 3040-3600
    EXPECT_EQ(s(karaoke.text(fieldTime(selection.startMs))), "{\\k28}k{\\k28}a{\\k90}ra{\\k150}oke");
}

TEST_F(KaraokeMouseTest, AClickChoosesTheSyllableARightClickPlaysIt)
{
    auto r = send(press(170));
    EXPECT_EQ(karaoke.current, 2);
    EXPECT_EQ(timing.hold(), 0);
    EXPECT_EQ(r.playEnd, std::optional<std::int64_t>(view.sampleAtX(200)));
    send(release(170));
    r = send(press(130, 50, AudioMouse::Button::Right));
    EXPECT_EQ(r.playMs, std::optional(std::pair(3600, 4500)));
    EXPECT_EQ(karaoke.current, 1);
    EXPECT_TRUE(r.focus);
    // in the letters' rows too
    send(release(130, AudioMouse::Button::Right));
    r = send(press(110, 10, AudioMouse::Button::Right));
    EXPECT_EQ(r.playMs, std::optional(std::pair(3000, 3600)));
}

TEST_F(KaraokeMouseTest, MoveOnClickMovesTheCurrentSyllablesNearerBoundary)
{
    moveOnClick = true;
    send(press(135)); // ra, next to ka (current): the boundary after ka moves here
    EXPECT_EQ(timing.hold(), 5);
    EXPECT_EQ(timing.grabbed(), 0);
    EXPECT_EQ(karaoke.times()[0], 4050);
    send(release(135));
    karaoke.current = 2;
    send(press(142)); // ra (now 135-150), before oke (current): the boundary before oke
    EXPECT_EQ(timing.grabbed(), 1);
    EXPECT_EQ(karaoke.times()[1], 4260);
    send(release(142));
    EXPECT_EQ(karaoke.current, 1);
    // two syllables away nothing moves; the click chooses it
    karaoke.current = 0;
    send(press(180));
    EXPECT_EQ(timing.hold(), 0);
    EXPECT_EQ(karaoke.current, 2);
}

TEST_F(KaraokeMouseTest, TheEndIsGrabbedPastTheLastSyllable)
{
    // the syllables end before the selection (sum 1500 of 3000 ms)
    karaoke = split("{\\k60}ka{\\k90}ra", 3000, 6000);
    // the left button near the end drags the end itself
    auto r = send(press(198));
    EXPECT_TRUE(r.keepCursor);
    EXPECT_EQ(timing.hold(), 2);
    send(release(198));
    // the right button takes the last syllable's end
    r = send(press(198, 50, AudioMouse::Button::Right));
    EXPECT_TRUE(r.keepCursor);
    EXPECT_EQ(timing.hold(), 5);
    EXPECT_EQ(timing.grabbed(), 1);
    EXPECT_EQ(karaoke.current, 1);
    send(drag(205, AudioMouse::Button::Right));
    EXPECT_EQ(karaoke.times()[1], 6150); // no limit while dragging
    EXPECT_EQ(selection.endMs, 6000);
    send(release(205, AudioMouse::Button::Right));
    EXPECT_EQ(karaoke.times()[1], 6000); // the release keeps it in the selection
}

TEST_F(KaraokeMouseTest, TheStartIsGrabbedBeforeTheSyllables)
{
    send(press(102));
    EXPECT_EQ(timing.hold(), 1);
    send(drag(90));
    EXPECT_EQ(selection.startMs, 2700);
    EXPECT_EQ(karaoke.times()[0], 3600); // the left button leaves the syllables
    // KA1-lead-in-keeps-syllables: on the release the time before them is a
    // lead syllable (legacy: the first syllable took it)
    auto r = send(release(90));
    EXPECT_TRUE(r.commit);
    EXPECT_EQ(strings(karaoke.syllables()), (std::vector<std::string>{"", "{}ka", "{}ra", "{}oke"}));
    EXPECT_EQ(karaoke.times(), (std::vector<int>{3000, 3600, 4500, 6000}));
    EXPECT_EQ(s(karaoke.text(selection.startMs)), "{\\k30}{\\k60}ka{\\k90}ra{\\k150}oke");
    // and later again: the lead syllable goes
    send(press(90));
    send(drag(100));
    send(release(100));
    EXPECT_EQ(s(karaoke.text(selection.startMs)), "{\\k60}ka{\\k90}ra{\\k150}oke");
}

TEST_F(KaraokeMouseTest, TheRightButtonCarriesTheSyllablesWithTheStart)
{
    send(press(100, 50, AudioMouse::Button::Right));
    send(drag(90, AudioMouse::Button::Right));
    send(release(90, AudioMouse::Button::Right));
    EXPECT_EQ(selection.startMs, 2700);
    EXPECT_EQ(karaoke.count(), 3); // no lead syllable: they moved with it
    EXPECT_EQ(karaoke.times(), (std::vector<int>{3300, 4200, 6000}));
}

// KA1: the syllables move by the start the field commits, not by the
// dragged one (here a field of whole seconds, and one of tenths).
TEST_F(KaraokeMouseTest, TheSyllablesMoveByTheCommittedStart)
{
    fieldTime = [](int ms) { return ms / 1000 * 1000; };
    send(press(100));
    send(drag(132));
    send(release(132));
    EXPECT_EQ(selection.startMs, 3960); // committed as 3000: the syllables stay
    EXPECT_EQ(karaoke.times(), (std::vector<int>{3600, 4500, 6000}));
    EXPECT_EQ(s(karaoke.text(fieldTime(selection.startMs))), "{\\k60}ka{\\k90}ra{\\k150}oke");
    send(press(132));
    send(drag(140));
    send(release(140));
    EXPECT_EQ(karaoke.times(), (std::vector<int>{4000, 4500, 6000})); // committed as 4000
    fieldTime = [](int ms) { return ms / 100 * 100; };
    karaoke = split("{\\k60}ka{\\k90}ra{\\k150}oke", 3000, 6000);
    selection.startMs = 3000;
    timing.drawn(view, selection);
    send(press(100, 50, AudioMouse::Button::Right));
    send(drag(101, AudioMouse::Button::Right)); // 3030, committed as 3000
    EXPECT_EQ(karaoke.times(), (std::vector<int>{3600, 4500, 6000}));
    send(drag(104, AudioMouse::Button::Right)); // 3120, committed as 3100
    EXPECT_EQ(karaoke.times(), (std::vector<int>{3700, 4600, 6000}));
    send(release(104, AudioMouse::Button::Right));
    EXPECT_EQ(s(karaoke.text(fieldTime(selection.startMs))), "{\\k60}ka{\\k90}ra{\\k140}oke");
}

// KA1 (Codex): a start off the 10 ms grid (a 24000/1001 frame's) carried by
// the right button keeps the syllables' lengths: the boundaries move by the
// committed difference and are not cut to whole centiseconds
TEST_F(KaraokeMouseTest, TheRightButtonKeepsTheLengthsOffTheCentisecondGrid)
{
    fieldTime = [](int ms) { return ms / 42 * 42; };
    karaoke = split("{\\k60}ka{\\k90}ra{\\k150}oke", 3024, 6024);
    selection.startMs = 3024;
    timing.drawn(view, selection);
    send(press(101, 50, AudioMouse::Button::Right));
    send(drag(103, AudioMouse::Button::Right)); // 3090, committed as 3066
    send(release(103, AudioMouse::Button::Right));
    EXPECT_EQ(karaoke.times(), (std::vector<int>{3666, 4566, 6024}));
    EXPECT_EQ(s(karaoke.text(fieldTime(selection.startMs))).substr(0, 19), "{\\k60}ka{\\k90}ra{\\k");
}

// KA1 (Codex): a start drag cut short (the capture lost) leaves the
// syllables at their times, as its release does
TEST_F(KaraokeMouseTest, ALostCaptureKeepsTheSyllablesAsAReleaseDoes)
{
    send(press(102));
    send(drag(130));
    EXPECT_EQ(selection.startMs, 3900);
    EXPECT_EQ(karaoke.times()[0], 3600);
    EXPECT_FALSE(timing.lostCapture(selection.startMs, {&karaoke, moveOnClick, sevenPixels(), fieldTime}));
    EXPECT_EQ(timing.hold(), 0);
    EXPECT_EQ(karaoke.times(), (std::vector<int>{3900, 4500, 6000}));
    EXPECT_EQ(s(karaoke.text(selection.startMs)), "{\\k0}ka{\\k60}ra{\\k150}oke");
    // the right button's: the syllables stay carried, and the caller is told
    // to commit them as the release would
    karaoke = split("{\\k60}ka{\\k90}ra{\\k150}oke", 3000, 6000);
    selection.startMs = 3000;
    timing.drawn(view, selection);
    send(press(100, 50, AudioMouse::Button::Right));
    send(drag(104, AudioMouse::Button::Right));
    EXPECT_EQ(karaoke.times(), (std::vector<int>{3720, 4620, 6000}));
    EXPECT_TRUE(timing.lostCapture(selection.startMs, {&karaoke, moveOnClick, sevenPixels(), fieldTime}));
    EXPECT_EQ(timing.hold(), 0);
    EXPECT_EQ(karaoke.times(), (std::vector<int>{3720, 4620, 6000}));
    EXPECT_FALSE(timing.lostCapture(selection.startMs, {&karaoke, moveOnClick, sevenPixels(), fieldTime}));
}

TEST_F(KaraokeMouseTest, AStaleGrabKeepsTheSelectionAsItIs)
{
    send(drag(150, AudioMouse::Button::None)); // over boundary 1
    EXPECT_EQ(timing.grabbed(), 1);
    // A5-stale-syllable (proposed): the next Line has one syllable
    karaoke = split("x", 3000, 6000);
    send(press(100));
    auto r = send(drag(-5));
    EXPECT_EQ(r.playEnd, std::optional<std::int64_t>(view.sampleAtX(200))); // not syllable 1's end
    auto up = release(-5);
    up.alt = true;
    r = send(up);
    EXPECT_EQ(selection.startMs, -150); // legacy: no ZEROIT, no clamp, no Alt neighbour
    EXPECT_EQ(r.adjacent, AudioAdjacent::None);
    // KA1-lead-in-keeps-syllables: the time before the old start is a lead syllable
    EXPECT_EQ(karaoke.times(), (std::vector<int>{3000, 6000}));
    // and without karaoke too, until the karaoke rows see no boundary
    auto restart = [&] {
        selection.startMs = 3000;
        timing.drawn(view, selection);
    };
    on = false;
    restart();
    send(press(100));
    send(drag(-10));
    send(release(-10));
    EXPECT_EQ(selection.startMs, -300);
    on = true;
    send(drag(300, AudioMouse::Button::None));
    EXPECT_EQ(timing.grabbed(), -1);
    restart();
    send(press(100));
    send(drag(-10));
    send(release(-10));
    EXPECT_EQ(selection.startMs, 0);
}

// KA5-onset-snap: with snapping on, the boundary being dragged lands on the
// nearest vocal onset within 100 ms; the others stay where they are.
TEST_F(KaraokeMouseTest, ADraggedBoundarySnapsToTheNearestOnset)
{
    onsets = {3870, 4530, 5000};
    send(press(121));
    auto r = send(drag(130)); // 3900
    EXPECT_EQ(karaoke.times(), (std::vector<int>{3870, 4500, 6000}));
    EXPECT_EQ(r.playEnd, std::optional<std::int64_t>(3870 * 48));
    send(drag(132)); // 3960: still within 100 ms of it
    EXPECT_EQ(karaoke.times()[0], 3870);
    send(drag(136)); // 4080: none near enough, so where the mouse is
    EXPECT_EQ(karaoke.times()[0], 4080);
    r = send(release(132)); // 3960
    EXPECT_EQ(karaoke.times(), (std::vector<int>{3870, 4500, 6000}));
    EXPECT_EQ(r.commit, std::optional(false));
    EXPECT_EQ(s(karaoke.text(3000)), "{\\k87}ka{\\k63}ra{\\k150}oke");
}

// A click on a boundary, or a move-on-click, places it without snapping.
TEST_F(KaraokeMouseTest, AClickedBoundaryDoesNotSnap)
{
    onsets = {3650, 4020};
    send(press(120));
    send(release(120));
    EXPECT_EQ(karaoke.times()[0], 3600);
    moveOnClick = true;
    send(press(135)); // ra, next to ka (current): the boundary after ka moves here
    EXPECT_EQ(timing.grabbed(), 0);
    EXPECT_EQ(karaoke.times()[0], 4050);
    send(release(135));
    EXPECT_EQ(karaoke.times()[0], 4050);
}

// The onset must lie between the boundary's neighbours, and the last
// boundary takes the end with it.
TEST_F(KaraokeMouseTest, SnappingKeepsBetweenTheNeighbours)
{
    onsets = {4520};
    send(press(121));
    send(drag(149)); // 4470: the onset is past ra's end
    EXPECT_EQ(karaoke.times()[0], 4470);
    send(release(149));
    EXPECT_EQ(karaoke.times()[0], 4470);
    onsets = {6280};
    send(press(200));
    send(drag(210)); // 6300
    EXPECT_EQ(karaoke.times()[2], 6280);
    EXPECT_EQ(selection.endMs, 6280);
    send(release(210));
    EXPECT_EQ(karaoke.times()[2], 6280);
}

// With the right button the later syllables (all but the last) move by the
// snapped amount, and are not snapped themselves.
TEST_F(KaraokeMouseTest, TheRightButtonMovesTheLaterOnesByTheSnap)
{
    onsets = {3700, 4620};
    send(press(120, 50, AudioMouse::Button::Right));
    send(drag(123, AudioMouse::Button::Right)); // 3690
    EXPECT_EQ(karaoke.times(), (std::vector<int>{3700, 4600, 6000}));
}

// Snapping off (no onsets): the boundary goes where the mouse does.
TEST_F(KaraokeMouseTest, WithoutOnsetsNothingSnaps)
{
    send(press(121));
    send(drag(130));
    send(release(131));
    EXPECT_EQ(karaoke.times()[0], 3930);
}

// ---- the commit --------------------------------------------------------------

namespace {

constexpr std::string_view kTwo = "[Events]\n"
                                  "Dialogue: 0,0:00:03.00,0:00:06.00,Default,,0,0,0,,karaoke\n"
                                  "Dialogue: 0,0:00:07.00,0:00:08.00,Default,,0,0,0,,other\n";

struct KaraokeCommitTest : ::testing::Test {
    EditSession session{load(kTwo)};
    core::LineId a{1}, b{2};
    LineVisible shown = [](core::LineId) { return true; };
    void select(std::set<core::LineId> lines, core::LineId active)
    {
        session.setSelection(Selection{active, std::move(lines), active, {}});
    }
    const core::LineRecord &line(std::size_t i) const { return *session.document().lines()[i]; }
};

} // namespace

TEST_F(KaraokeCommitTest, TheSyllablesTextIsOneStepWithTheTimes)
{
    select({a}, a);
    const auto steps = session.historySize();
    AudioCommitRequest request{3000, 6000, true, false};
    request.karaokeText = u"{\\k25}ka{\\k25}ra{\\k25}o{\\k25}ke";
    const auto outcome = commitAudioTimes(session, request, shown);
    ASSERT_TRUE(outcome);
    EXPECT_TRUE(outcome->stepped);
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(session.history().back().name, "Changing time on audio spectrum");
    EXPECT_EQ(line(0).text, u8"{\\k25}ka{\\k25}ra{\\k25}o{\\k25}ke");
    // the same text again is still a step (legacy marks the field modified)
    ASSERT_TRUE(commitAudioTimes(session, request, shown));
    EXPECT_EQ(session.historySize(), steps + 2);
    // without AUDIO_AUTO_COMMIT the text waits in the editor with the times
    request.save = false;
    request.karaokeText = u"{\\k300}karaoke";
    ASSERT_TRUE(commitAudioTimes(session, request, shown));
    EXPECT_EQ(session.historySize(), steps + 2);
    EXPECT_EQ(session.draftText(), std::optional<std::u8string>(u8"{\\k300}karaoke"));
}

TEST_F(KaraokeCommitTest, SeveralSelectedLinesAllTakeTheText)
{
    select({a, b}, a);
    AudioCommitRequest request{3000, 6000, true, false};
    request.karaokeText = u"{\\k300}karaoke";
    ASSERT_TRUE(commitAudioTimes(session, request, shown));
    EXPECT_EQ(line(0).text, u8"{\\k300}karaoke");
    EXPECT_EQ(line(1).text, u8"{\\k300}karaoke"); // SubsGrid::ChangeLine over the selection
}

// KA1-lead-in-keeps-syllables in Line mode: a start moved on the audio keeps
// the \k syllables of the one Line at their audio times.
TEST(KaraokeCommit, AMovedStartKeepsTheLinesSyllables)
{
    EditSession session{load("[Events]\n"
                             "Dialogue: 0,0:00:03.00,0:00:06.00,Default,,0,0,0,,{\\k100}ka{\\k200}ra\n"
                             "Dialogue: 0,0:00:07.00,0:00:08.00,Default,,0,0,0,,{\\k100}ka\n")};
    const auto a = session.document().lines()[0]->id, b = session.document().lines()[1]->id;
    const LineVisible shown = [](core::LineId) { return true; };
    session.setSelection(Selection{a, {a}, a, {}});
    AudioCommitRequest request{2500, 6000, true, false};
    ASSERT_TRUE(commitAudioTimes(session, request, shown));
    EXPECT_EQ(session.document().lines()[0]->text, u8"{\\k50}{\\k100}ka{\\k200}ra");
    request.startMs = 3500; // later: the lead syllable goes, ka shortens
    ASSERT_TRUE(commitAudioTimes(session, request, shown));
    EXPECT_EQ(session.document().lines()[0]->text, u8"{\\k50}ka{\\k200}ra");
    request.endMs = 6500; // the end alone leaves the text
    ASSERT_TRUE(commitAudioTimes(session, request, shown));
    EXPECT_EQ(session.document().lines()[0]->text, u8"{\\k50}ka{\\k200}ra");
    // with several Lines selected their texts stay as they are
    session.setSelection(Selection{a, {a, b}, a, {}});
    request.startMs = 3000;
    ASSERT_TRUE(commitAudioTimes(session, request, shown));
    EXPECT_EQ(session.document().lines()[0]->text, u8"{\\k50}ka{\\k200}ra");
    EXPECT_EQ(session.document().lines()[1]->text, u8"{\\k100}ka");
}

TEST(KaraokeCommit, ATranslatedFileTakesItInTheTranslation)
{
    EditSession session{load("[Script Info]\nTLMode: Yes\n\n[Events]\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,TLMode Style,,0,0,0,,original\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,translated\n")};
    const auto id = session.document().lines()[0]->id;
    session.setSelection(Selection{id, {id}, id, {}});
    AudioCommitRequest request{1000, 2000, true, false};
    request.karaokeText = u"{\\k100}translated";
    ASSERT_TRUE(commitAudioTimes(session, request, [](core::LineId) { return true; }));
    const auto &line = *session.document().lines()[0];
    EXPECT_EQ(line.text, u8"original");
    EXPECT_EQ(line.translation, u8"{\\k100}translated");
}
