#include "hikari/application/grid_translation.h"
#include "hikari/application/translation_mode.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/ass_save.h"
#include "hikari/core/style.h"
#include "hikari/core/subtitle_load.h"

#include <gtest/gtest.h>

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

using namespace hikari;
using namespace hikari::application;

namespace {

constexpr std::string_view kStyles =
    "[V4+ Styles]\n"
    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
    "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
    "MarginR, MarginV, Encoding\n"
    "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1\n"
    "Style: O,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,8,10,10,10,1\n"
    "\n";

const std::string kPairs = std::string("[Script Info]\nScriptType: v4.00+\nTLMode: Yes\nTLMode Style: O\n\n") +
                           std::string(kStyles) +
                           "[Events]\n"
                           "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                           "Dialogue: 0,0:00:01.00,0:00:02.00,O,,0,0,0,\fD,Gate\n"
                           "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Brama\n"
                           "Dialogue: 0,0:00:03.00,0:00:04.00,O,,0,0,0,,Tower\n"
                           "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,Wieza\n"
                           "Dialogue: 0,0:00:05.00,0:00:06.00,O,,0,0,0,,Wall\n"
                           "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,\n";

const std::string kPlain = std::string("[Script Info]\nScriptType: v4.00+\n\n") + std::string(kStyles) +
                           "[Events]\n"
                           "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                           "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,A\n"
                           "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,B\n";

std::vector<std::byte> bytesOf(std::string_view text)
{
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return bytes;
}

core::Document load(std::string_view text)
{
    return core::loadAss(bytesOf(text)).document;
}

std::string str(const std::u8string &s)
{
    return std::string(s.begin(), s.end());
}

std::u8string str8(const QString &s)
{
    const QByteArray utf8 = s.toUtf8();
    return std::u8string(utf8.begin(), utf8.end());
}

std::string saved(const core::Document &d)
{
    const auto bytes = core::encodeAss(d);
    return std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}

std::string events(const std::string &file)
{
    return file.substr(file.find("Format: Layer"));
}

void selectRows(EditSession &s, std::initializer_list<std::size_t> rows)
{
    Selection sel;
    for (const auto row : rows)
        sel.selected.insert(s.document().lines()[row]->id);
    sel.active = s.document().lines()[*rows.begin()]->id;
    s.setSelection(sel);
}

}

TEST(TranslatorMode, TurningOnIsOneStepAndUndoTakesItBack)
{
    EditSession session{load(kPlain)};
    const auto steps = session.historySize();
    ASSERT_TRUE(turnOnTranslationModeStep(session));
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(session.history().back().name, "Turning on translator mode");
    const auto &d = session.document();
    EXPECT_EQ(d.scriptInfo(u8"TLMode"), u8"Yes");
    EXPECT_EQ(d.scriptInfo(u8"TLMode Style"), u8"TLmode");
    const auto styles = core::decodeStyles(d);
    ASSERT_EQ(styles.size(), 3u);
    EXPECT_EQ(styles.back().name, u8"TLmode");
    EXPECT_EQ(styles.back().alignment, u8"8");
    EXPECT_EQ(styles.back().fontname, u8"Arial");
    EXPECT_EQ(turnOnTranslationModeStep(session).error(), CommandRefusal::Invalid);
    ASSERT_TRUE(session.undo());
    EXPECT_FALSE(session.document().scriptInfo(u8"TLMode"));
    EXPECT_FALSE(session.document().scriptInfo(u8"TLMode Style"));
    EXPECT_EQ(core::decodeStyles(session.document()).size(), 2u);
    EXPECT_EQ(saved(session.document()), kPlain);
}

TEST(TranslatorMode, OnlyForAss)
{
    const auto srt = core::loadSubtitle(bytesOf("1\n00:00:01,000 --> 00:00:02,000\nA\n\n"), u8"srt");
    ASSERT_TRUE(srt);
    EditSession session{srt->document};
    EXPECT_EQ(turnOnTranslationModeStep(session).error(), CommandRefusal::Invalid);
}

TEST(TranslatorMode, AnAssFileWithoutScriptInfoGetsTheSection)
{
    const std::string sectionless = std::string(kStyles) + kPlain.substr(kPlain.find("[Events]"));
    EditSession session{load(sectionless)};
    ASSERT_TRUE(turnOnTranslationModeStep(session));
    EXPECT_EQ(session.document().scriptInfo(u8"TLMode"), u8"Yes");
    const std::string file = saved(session.document());
    EXPECT_EQ(file.substr(0, file.find("[V4+ Styles]")), "[Script Info]\nTLMode Style: TLmode\nTLMode: Yes\n\n");
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(saved(session.document()), sectionless);

    EditSession pasted{load(sectionless)};
    ASSERT_TRUE(pasteTranslation(pasted, u8"a\nb\n", u8"txt"));
    EXPECT_EQ(pasted.document().scriptInfo(u8"TLMode Showtl"), u8"Yes");
    EXPECT_EQ(pasted.document().lines()[0]->translation, u8"a");
    EXPECT_TRUE(saved(pasted.document()).starts_with("[Script Info]\nTLMode Style: TLmode\nTLMode: Yes\nTLMode Showtl: Yes\n\n"));
}

TEST(TranslatorMode, PlainTextIsRefused)
{
    const auto text = core::loadSubtitle(bytesOf("first line\nsecond line\n"), u8"txt");
    ASSERT_TRUE(text);
    ASSERT_EQ(text->document.format(), core::SubtitleFormat::PlainText);
    EditSession session{text->document};
    const auto steps = session.historySize();
    EXPECT_EQ(turnOnTranslationModeStep(session).error(), CommandRefusal::Invalid);
    EXPECT_EQ(pasteTranslation(session, u8"a\nb\n", u8"txt").error(), CommandRefusal::Invalid);
    EXPECT_EQ(session.historySize(), steps);
    EXPECT_FALSE(session.document().scriptInfo(u8"TLMode"));
    core::Document plain = text->document;
    EXPECT_FALSE(plain.setScriptInfo(u8"TLMode", u8"Yes"));
}

TEST(TranslatorMode, TurningOffAndOnRoundTripsTheTLModeFile)
{
    EditSession session{load(kPairs)};
    auto lines = session.document().lines();
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_TRUE(lines[0]->unconfirmed);
    EXPECT_EQ(lines[1]->translation, u8"Wieza");
    EXPECT_TRUE(lines[2]->translation.empty());
    EXPECT_EQ(saved(session.document()), kPairs);

    const auto steps = session.historySize();
    ASSERT_TRUE(turnOffTranslationMode(session));
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(session.history().back().name, "Turning off translator mode");
    const std::string off = saved(session.document());
    EXPECT_EQ(events(off), "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                           "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Brama\n"
                           "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,Wieza\n"
                           "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,Wall\n");
    EXPECT_EQ(off.find("TLMode"), std::string::npos);
    EXPECT_EQ(off.find("Style: O,"), std::string::npos);
    {
        const auto reloaded = load(off);
        ASSERT_EQ(reloaded.lines().size(), 3u);
        for (const auto *l : reloaded.lines()) {
            EXPECT_FALSE(l->unconfirmed);
            EXPECT_TRUE(l->translation.empty());
        }
    }

    ASSERT_TRUE(session.undo());
    EXPECT_EQ(saved(session.document()), kPairs);
    lines = session.document().lines();
    EXPECT_TRUE(lines[0]->unconfirmed);

    selectRows(session, {1});
    ASSERT_TRUE(toggleUnconfirmed(session));
    const std::string marked = saved(session.document());
    EXPECT_NE(marked.find("Dialogue: 0,0:00:03.00,0:00:04.00,O,,0,0,0,\fD,Tower\n"
                          "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,Wieza\n"),
              std::string::npos)
        << marked;
    const auto reloaded = load(marked);
    ASSERT_EQ(reloaded.lines().size(), 3u);
    EXPECT_TRUE(reloaded.lines()[0]->unconfirmed);
    EXPECT_TRUE(reloaded.lines()[1]->unconfirmed);
    EXPECT_FALSE(reloaded.lines()[2]->unconfirmed);
}

TEST(TranslatorMode, TurnedOffThenOnAgainMakesANewTLModeStyle)
{
    EditSession session{load(kPairs)};
    ASSERT_TRUE(turnOffTranslationMode(session));
    ASSERT_TRUE(turnOnTranslationModeStep(session));
    EXPECT_EQ(session.document().scriptInfo(u8"TLMode Style"), u8"TLmode");
    EXPECT_EQ(session.document().scriptInfo(u8"TLMode"), u8"Yes");
    EXPECT_EQ(events(saved(session.document())),
              "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
              "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Brama\n"
              "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,Wieza\n"
              "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,Wall\n");
}

TEST(NotConfirmed, FlipsEverySelectedLineAsOneStep)
{
    EditSession session{load(kPairs)};
    selectRows(session, {0, 2});
    const auto steps = session.historySize();
    ASSERT_TRUE(toggleUnconfirmed(session));
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(session.history().back().name, "Mark unconfirmed");
    auto lines = session.document().lines();
    EXPECT_FALSE(lines[0]->unconfirmed);
    EXPECT_FALSE(lines[1]->unconfirmed);
    EXPECT_TRUE(lines[2]->unconfirmed);
    EXPECT_NE(saved(session.document()).find("Dialogue: 0,0:00:05.00,0:00:06.00,O,,0,0,0,\fD,Wall\n"
                                             "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,\n"),
              std::string::npos);
    ASSERT_TRUE(session.undo());
    lines = session.document().lines();
    EXPECT_TRUE(lines[0]->unconfirmed);
    EXPECT_FALSE(lines[2]->unconfirmed);
}

TEST(NotConfirmed, IsItsOwnStepNotJoinedToTheNextEdit)
{
    EditSession session{load(kPairs)};
    selectRows(session, {1});
    const auto id = session.document().lines()[1]->id;
    const auto steps = session.historySize();
    ASSERT_TRUE(toggleUnconfirmed(session));
    ASSERT_TRUE(session.editDraftText(id, u8"Turret"));
    ASSERT_TRUE(session.commitDraft());
    ASSERT_EQ(session.historySize(), steps + 2);
    EXPECT_EQ(session.history()[steps].name, "Mark unconfirmed");
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(session.document().lines()[1]->text, u8"Tower");
    EXPECT_TRUE(session.document().lines()[1]->unconfirmed);
    ASSERT_TRUE(session.undo());
    EXPECT_FALSE(session.document().lines()[1]->unconfirmed);

    EditSession drafted{load(kPairs)};
    selectRows(drafted, {1});
    const auto draftedId = drafted.document().lines()[1]->id;
    const auto before = drafted.historySize();
    ASSERT_TRUE(drafted.editDraftText(draftedId, u8"Turret"));
    ASSERT_TRUE(toggleUnconfirmed(drafted));
    ASSERT_EQ(drafted.historySize(), before + 2);
    EXPECT_NE(drafted.history()[before].name, "Mark unconfirmed");
    EXPECT_EQ(drafted.history()[before + 1].name, "Mark unconfirmed");
    EXPECT_FALSE(drafted.draftLine());
    ASSERT_TRUE(drafted.undo());
    EXPECT_FALSE(drafted.document().lines()[1]->unconfirmed);
    EXPECT_EQ(drafted.document().lines()[1]->text, u8"Turret");
}

TEST(NotConfirmed, OnlyInTranslationMode)
{
    EditSession plain{load(kPlain)};
    selectRows(plain, {0});
    EXPECT_EQ(toggleUnconfirmed(plain).error(), CommandRefusal::Invalid);
    EditSession pairs{load(kPairs)};
    pairs.setSelection({});
    EXPECT_EQ(toggleUnconfirmed(pairs).error(), CommandRefusal::Invalid);
}

TEST(MovingTags, SplitsTheOverrideBlocksFromTheText)
{
    struct Case {
        std::u8string text, original, translation;
        std::size_t caret;
    };
    const Case cases[] = {
        {u8"{\\i1}Gate keeper", u8"Gate keeper", u8"{\\i1}", 5},
        {u8"Gate {\\i1}keeper{\\i0}", u8"Gate keeper", u8"{\\i1}{\\i0}", 0},
        {u8"{\\b1}{\\i1}A}{B", u8"AB", u8"{\\b1\\i1}", 8},
        {u8"a}b", u8"a}b", u8"", 0},
        {u8"a}b{c}", u8"a}bb", u8"{c}{c}", 0},
        {u8"{\\an8}top {open", u8"top {open", u8"{\\an8}", 6},
        {u8"{\\fn\U0001D400}x", u8"x", u8"{\\fn\U0001D400}", 7},
    };
    for (const auto &c : cases) {
        const auto moved = moveTagsFromOriginal(c.text);
        ASSERT_TRUE(moved) << str(c.text);
        EXPECT_EQ(str(moved->original), str(c.original)) << str(c.text);
        EXPECT_EQ(str(moved->translation), str(c.translation)) << str(c.text);
        EXPECT_EQ(moved->caret, c.caret) << str(c.text);
    }
    EXPECT_FALSE(moveTagsFromOriginal(u8"Gate keeper"));
    EXPECT_FALSE(moveTagsFromOriginal(u8"{\\i1 never closed"));
    EXPECT_FALSE(moveTagsFromOriginal(u8""));
}

namespace {

struct TagMoveCall {
    bool tl = true, move = true;
    int visual = 0;
};

struct TagMoveCase {
    std::string name;
    std::u8string text, translation;
    std::vector<TagMoveCall> calls;
};

std::vector<TagMoveCase> readTagMoveCases(const char *path)
{
    std::ifstream in(path, std::ios::binary);
    std::vector<TagMoveCase> cases;
    TagMoveCall state;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        std::vector<std::string> fields;
        for (std::size_t from = 0;;) {
            const auto at = line.find('\t', from);
            fields.push_back(line.substr(from, at == std::string::npos ? std::string::npos : at - from));
            if (at == std::string::npos)
                break;
            from = at + 1;
        }
        if (line.starts_with("case ")) {
            cases.push_back({line.substr(5), {}, {}, {}});
            state = {};
        } else if (fields[0] == "line") {
            cases.back().text = std::u8string(fields.at(1).begin(), fields.at(1).end());
            if (fields.size() > 2)
                cases.back().translation = std::u8string(fields[2].begin(), fields[2].end());
        } else if (fields[0] == "state") {
            state = {fields.at(1) == "1", fields.at(2) == "1", std::stoi(fields.at(3))};
        } else if (fields[0] == "call") {
            cases.back().calls.push_back(state);
        }
    }
    return cases;
}

}

TEST(MovingTagsCapture, ReplaysTheLegacyObservations)
{
    const auto cases = readTagMoveCases(HIKARI_TAG_MOVE_CASES);
    ASSERT_GE(cases.size(), 34u);
    QFile file(QStringLiteral(HIKARI_TAG_MOVE_OBSERVATIONS));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    std::map<std::string, QJsonObject> observed;
    for (const QByteArray &line : file.readAll().split('\n'))
        if (!line.trimmed().isEmpty()) {
            const auto object = QJsonDocument::fromJson(line).object();
            observed[object["case"].toString().toStdString()] = object;
        }
    ASSERT_EQ(observed.size(), cases.size());

    int splits = 0, widthsDiffer = 0;
    for (const auto &c : cases) {
        SCOPED_TRACE(c.name);
        const QJsonObject &legacy = observed.at(c.name);
        ASSERT_EQ(str8(legacy["text"].toString()), c.text);
        ASSERT_EQ(str8(legacy["translation"].toString()), c.translation);
        const QJsonArray onLinux = legacy["linux"].toArray();
        const QJsonArray onWindows = legacy["windows"].isString() ? onLinux : legacy["windows"].toArray();
        widthsDiffer += legacy["windows"].isString() ? 0 : 1;
        ASSERT_EQ(onLinux.size(), qsizetype(c.calls.size()));
        ASSERT_EQ(onWindows.size(), qsizetype(c.calls.size()));
        for (std::size_t n = 0; n < c.calls.size(); ++n) {
            SCOPED_TRACE("call " + std::to_string(n));
            const TagMoveCall &call = c.calls[n];
            const bool eligible = call.tl && call.move && call.visual <= 0 && c.translation.empty();
            const auto moved = eligible ? moveTagsFromOriginal(c.text) : std::nullopt;
            for (const auto &[utf16Units, build] : {std::pair{false, onLinux[qsizetype(n)].toObject()},
                                                    std::pair{true, onWindows[qsizetype(n)].toObject()}}) {
                SCOPED_TRACE(utf16Units ? "windows" : "linux");
                ASSERT_EQ(build["split"].toBool(), moved.has_value());
                if (!moved)
                    continue;
                ++splits;
                const QString edit = build["edit"].toString();
                EXPECT_EQ(str(moved->translation), str(str8(edit)));
                EXPECT_EQ(str(moved->original), str(str8(build["orig"].toString())));
                EXPECT_TRUE(build["focus"].toBool());
                const QJsonArray selection = build["selection"].toArray();
                ASSERT_EQ(selection.size(), 2);
                EXPECT_EQ(selection[0].toInt(), selection[1].toInt());
                // wx counts the caret in wchar_t: code points on wxGTK, UTF-16 units on wxMSW.
                const QString before = edit.left(qsizetype(moved->caret));
                EXPECT_EQ(selection[1].toInt(), utf16Units ? before.size() : before.toUcs4().size());
            }
        }
    }
    EXPECT_GE(splits, 50);
    EXPECT_EQ(widthsDiffer, 2);
}

TEST(OriginalColumns, LoadTakesTheDocumentAndTheOption)
{
    const auto doc = [](const char *extra) {
        return load(std::string("[Script Info]\n") + extra + "[Events]\n");
    };
    EXPECT_TRUE(OriginalColumns{}.observe(doc("TLMode: Yes\nTLMode Showtl: Yes\n"), false));
    EXPECT_TRUE(OriginalColumns{}.observe(doc("TLMode: Yes\n"), true));
    EXPECT_FALSE(OriginalColumns{}.observe(doc("TLMode: Yes\n"), false));
    EXPECT_FALSE(OriginalColumns{}.observe(doc("TLMode Showtl: Yes\n"), true));
    OriginalColumns columns;
    const auto on = doc("TLMode: Yes\n");
    EXPECT_FALSE(columns.observe(on, false));
    EXPECT_FALSE(columns.observe(on, true));
}

TEST(OriginalColumns, FollowsTheSwitchPasteAndUndo)
{
    EditSession session{load(kPlain)};
    OriginalColumns columns;
    EXPECT_FALSE(columns.observe(session.document(), false));
    ASSERT_TRUE(turnOnTranslationModeStep(session));
    columns.turnedOn(session.document(), false);
    EXPECT_FALSE(columns.observe(session.document(), false));
    ASSERT_TRUE(pasteTranslation(session, u8"a\nb\n", u8"txt"));
    columns.pasted(session.document());
    EXPECT_TRUE(columns.observe(session.document(), false));
    ASSERT_TRUE(turnOffTranslationMode(session));
    columns.turnedOff(session.document());
    EXPECT_FALSE(columns.observe(session.document(), false));
    ASSERT_TRUE(turnOnTranslationModeStep(session));
    columns.turnedOn(session.document(), false);
    EXPECT_FALSE(columns.observe(session.document(), false));
    ASSERT_TRUE(session.undo());
    EXPECT_FALSE(session.document().scriptInfo(u8"TLMode"));
    EXPECT_TRUE(columns.observe(session.document(), false));
    ASSERT_TRUE(session.undo());
    EXPECT_TRUE(columns.observe(session.document(), false));
    EXPECT_TRUE(columns.observe(session.document(), false));
}

TEST(OriginalColumns, UndoWithoutShowtlFollowsTheOption)
{
    EditSession session{load(kPlain)};
    OriginalColumns columns;
    columns.observe(session.document(), true);
    ASSERT_TRUE(turnOnTranslationModeStep(session));
    columns.turnedOn(session.document(), true);
    EXPECT_TRUE(columns.shown());
    ASSERT_TRUE(session.undo());
    EXPECT_FALSE(columns.observe(session.document(), true));
    ASSERT_TRUE(session.redo());
    EXPECT_TRUE(columns.observe(session.document(), true));
    ASSERT_TRUE(session.undo());
    EXPECT_FALSE(columns.observe(session.document(), false));
    ASSERT_TRUE(session.redo());
    EXPECT_FALSE(columns.observe(session.document(), false));
}
