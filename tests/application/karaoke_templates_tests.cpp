#include "hikari/application/karaoke_templates.h"

#include <gtest/gtest.h>

#include <charconv>

using namespace hikari::application::karaoke_templates;

namespace {

std::optional<double> number(std::string_view s)
{
    double v = 0;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    if (s.empty() || r.ec != std::errc() || r.ptr != s.data() + s.size())
        return std::nullopt;
    return v;
}

SubsEntry comment(std::string effect, std::string text, std::string style = "Default")
{
    return {.dialogue = true, .comment = true, .layer = 1, .effect = std::move(effect), .text = std::move(text),
            .style = std::move(style)};
}

SubsEntry dialogue(std::string effect, std::string text)
{
    return {.dialogue = true, .effect = std::move(effect), .text = std::move(text), .style = "Default"};
}

struct Reported {
    int level;
    std::optional<Where> where;
    std::optional<At> at;
    std::string message;
};

struct Recorder {
    std::vector<Reported> reported;
    Report report()
    {
        return [this](int level, const std::optional<Where> &w, const std::optional<At> &at, const std::string &m) {
            reported.push_back({level, w, at, m});
        };
    }
};

Evaluated text(std::string t)
{
    return {Evaluated::Kind::Text, std::move(t)};
}

}

TEST(KaraokeTemplates, HeadtailSplitsAtTheFirstRunOfSpaces)
{
    EXPECT_EQ(headtail("syl  loop 3"), (std::pair<std::string_view, std::string_view>{"syl", "loop 3"}));
    EXPECT_EQ(headtail("syl"), (std::pair<std::string_view, std::string_view>{"syl", ""}));
    EXPECT_EQ(headtail(" syl"), (std::pair<std::string_view, std::string_view>{"", "syl"}));
    EXPECT_EQ(headtail("syl\t"), (std::pair<std::string_view, std::string_view>{"syl", ""}));
    EXPECT_EQ(lower("Template LINE"), "template line");
}

TEST(KaraokeTemplates, ParsingFindsEachClassAndRemovesGeneratedLines)
{
    const std::vector<SubsEntry> subs = {
        {},
        dialogue("fx", "old output"),
        comment("code once", "x = 1"),
        comment("Template Syl noblank", "{\\pos($x,$y)}"),
        dialogue("fx", "more old output"),
        comment("template pre-line jumper", "{\\org(0,0)}"),
        comment("template line", "{\\be1}"),
        comment("template line jumper", "{\\fad(1,1)}"),
        comment("template line other", "{\\b1}"),
        comment("template furi", "{\\an5}"),
        dialogue("", "{\\k10}ka"),
    };
    const Parsed p = parseTemplates(subs, false, number);
    EXPECT_EQ(p.removed, (std::vector<int>{2, 5}));
    EXPECT_TRUE(p.problems.empty());
    ASSERT_EQ(p.templates.once.size(), 1u);
    EXPECT_TRUE(p.templates.once[0]->code);
    EXPECT_EQ(p.templates.once[0]->where, (Where{2, 1, "code", "code once", "Default"}));
    ASSERT_EQ(p.templates.syl.size(), 1u);
    EXPECT_TRUE(p.templates.syl[0]->noblank);
    EXPECT_EQ(p.templates.syl[0]->text, "{\\pos($x,$y)}");
    EXPECT_EQ(p.templates.syl[0]->where->index, 3);
    ASSERT_EQ(p.templates.line.size(), 3u);
    EXPECT_FALSE(p.templates.line[0]->id);
    EXPECT_EQ(p.templates.line[0]->text, "{\\be1}");
    EXPECT_EQ(p.templates.line[1]->id, "jumper");
    EXPECT_EQ(p.templates.line[1]->pre, "{\\org(0,0)}");
    EXPECT_EQ(p.templates.line[1]->text, "{\\fad(1,1)}");
    EXPECT_EQ(p.templates.line[1]->preWhere->index, 4);
    EXPECT_EQ(p.templates.line[1]->textWhere->index, 6);
    EXPECT_EQ(p.templates.line[2]->id, "other");
    EXPECT_EQ(p.templates.furi.size(), 1u);
}

TEST(KaraokeTemplates, AModifierAppliesToEveryListTheTemplateIsIn)
{
    const Parsed p = parseTemplates({comment("code once line all", "y = 2")}, false, number);
    ASSERT_EQ(p.templates.once.size(), 1u);
    ASSERT_EQ(p.templates.line.size(), 1u);
    EXPECT_EQ(p.templates.once[0], p.templates.line[0]);
    EXPECT_FALSE(p.templates.once[0]->style);
}

TEST(KaraokeTemplates, ProblemsKeepTwoOneSevensWording)
{
    const Parsed p =
        parseTemplates({comment("template syl loop abc", "t"), comment("template syl fxgroup", "u")}, true, number);
    ASSERT_EQ(p.problems.size(), 3u);
    EXPECT_EQ(p.problems[0].message,
              "Failed reading this repeat-count to a number: abc\nIn template line: t\nEffect field: template syl "
              "loop abc\n");
    EXPECT_EQ(p.problems[1].message,
              "Unknown modifier in template: abc\nIn template line: t\nEffect field: template syl loop abc\n");
    EXPECT_EQ(p.problems[2].message,
              "No fxgroup name following fxgroup modifier\nIn template linee: u\nEffect field: template syl "
              "fxgroup\n");
    EXPECT_EQ(p.problems[2].where->number, 2);
    EXPECT_EQ(p.templates.syl[0]->loops, 1);
}

TEST(KaraokeTemplates, VariablesAreReplacedAndUnknownOnesPlaced)
{
    const Variables vars{{"x", 10.5}, {"y", std::string("a")}, {"start", 300.0}};
    Recorder run;
    const std::optional<Where> where = Where{7, 5, "template", "template syl", "Default"};
    EXPECT_EQ(runTextTemplate("{$X,$y,$start,$NoSuch,$}", &vars, where, nullptr,
                              [](const std::string &) { return Evaluated{}; }, run.report()),
              "{10.5,a,300,$nosuch,$}");
    ASSERT_EQ(run.reported.size(), 1u);
    EXPECT_EQ(run.reported[0].where, where);
    EXPECT_EQ(run.reported[0].at, (At{15, 7}));
    EXPECT_EQ(run.reported[0].message, "Unknown variable name: nosuch\nIn karaoke template: {$X,$y,$start,$NoSuch,$}\n");
}

TEST(KaraokeTemplates, ExpressionsAreEvaluatedAndProblemsPlacedInTheWrittenText)
{
    const Variables vars{{"x", 100.0}, {"actor", std::string("a!b")}};
    Recorder run;
    const auto evaluate = [](const std::string &e) {
        if (e == "1+1")
            return text("2");
        if (e == "nil")
            return Evaluated{};
        return Evaluated{Evaluated::Kind::RuntimeError, "boom"};
    };
    EXPECT_EQ(runTextTemplate("{\\pos($x,!1+1!)!nil!}{!bad!}", &vars, Where{}, nullptr, evaluate, run.report()),
              "{\\pos(100,2)!nil!}{!bad!}");
    ASSERT_EQ(run.reported.size(), 1u);
    EXPECT_EQ(run.reported[0].at, (At{23, 5}));
    EXPECT_EQ(run.reported[0].message, "Runtime error in template expression: boom\nExpression producing error: bad\n"
                                       "Template with expression: {\\pos($x,!1+1!)!nil!}{!bad!}\n");

    run.reported.clear();
    runTextTemplate("$actor!z!", &vars, Where{}, nullptr, evaluate, run.report());
    ASSERT_EQ(run.reported.size(), 1u);
    EXPECT_FALSE(run.reported[0].at);
}

TEST(KaraokeTemplates, ANamedTemplatesProblemNamesThePartItIsIn)
{
    const Where first{3, 3, "template", "template line j", "Default"};
    const Where second{4, 4, "template", "template line j", "Default"};
    const std::vector<Part> parts{{"{\\t($start,$end)}", first}, {"{$nosuch}", second}};
    const Variables vars{{"start", 0.0}, {"end", 1000.0}};
    Recorder run;
    runTextTemplate(parts[0].text + parts[1].text, &vars, first, &parts,
                    [](const std::string &) { return Evaluated{}; }, run.report());
    ASSERT_EQ(run.reported.size(), 1u);
    EXPECT_EQ(run.reported[0].where, second);
    EXPECT_EQ(run.reported[0].at, (At{2, 7}));
}

TEST(KaraokeTemplates, TheModeLineChoosesTheModeAndOtherwiseKaraOKOnlyWordsDo)
{
    const std::vector<SubsEntry> stock = {comment("template char", "{\\an5}"), comment("template syl style Main", "a"),
                                          comment("code syl", "tenv.wordy = 1")};
    EXPECT_EQ(modeOf(std::nullopt, stock), Mode::Stock);
    EXPECT_EQ(modeOf(" KARAOK ", stock), Mode::KaraOK);
    EXPECT_EQ(modeOf("Aegisub 3.2.2", {comment("template lword", "a")}), Mode::Stock);
    EXPECT_EQ(modeOf("something else", {comment("template lword", "a")}), Mode::KaraOK);
    for (const SubsEntry &l : {comment("template lsyl", "a"), comment("template furichar", "a"),
                               comment("template word", "a"), comment("code char", "x = 1"),
                               comment("template syl", "!k_retime(\"syl\", 0, 0)!"), comment("code line", "y = tenv.syll"),
                               comment("template syl", "{\\pos($ci,0)}"), comment("template syl", "$LLEN")})
        EXPECT_EQ(modeOf(std::nullopt, {l}), Mode::KaraOK) << l.effect << " " << l.text;
    EXPECT_EQ(modeOf(std::nullopt, {dialogue("template lsyl", "a")}), Mode::Stock);
    EXPECT_EQ(modeOf(std::nullopt, {comment("template syl", "$cia")}), Mode::Stock);
}

TEST(KaraokeTemplates, KaraOKLineClassesWriteTheirPartsAndKeepTheirUnit)
{
    const Parsed p = parseTemplates({comment("template line j", "{\\pre}"), comment("template lword j", "{\\w}"),
                                     comment("template lchar j", "{\\c}"), comment("template lsyl", "{\\s}")},
                                    false, number, Mode::KaraOK);
    ASSERT_EQ(p.templates.line.size(), 2u);
    EXPECT_EQ(p.templates.line[0]->unit, Unit::Syl);
    EXPECT_EQ(p.templates.line[1]->id, "j");
    EXPECT_EQ(p.templates.line[1]->pre, "{\\pre}");
    EXPECT_EQ(p.templates.line[1]->text, "{\\w}");
    EXPECT_EQ(p.templates.line[1]->unit, Unit::Word);
    ASSERT_EQ(p.problems.size(), 1u);
    EXPECT_EQ(p.problems[0].message, "Unable to combine lchar class templates with the lword class this template has\n");
    EXPECT_EQ(p.problems[0].where->number, 3);
}

TEST(KaraokeTemplates, KaraOKUnitClassesFillTheirListsAndSylCharIsACharTemplate)
{
    const Parsed p = parseTemplates({comment("template word", "w"), comment("template syl char", "c"),
                                     comment("template furichar style Main", "f"), comment("template syl furi", "s"),
                                     comment("code word char", "x = 1")},
                                    false, number, Mode::KaraOK);
    ASSERT_EQ(p.templates.word.size(), 2u);
    ASSERT_EQ(p.templates.chr.size(), 2u);
    EXPECT_EQ(p.templates.chr[0]->text, "c");
    EXPECT_EQ(p.templates.chr[1], p.templates.word[1]);
    ASSERT_EQ(p.templates.furichar.size(), 1u);
    EXPECT_EQ(p.templates.furichar[0]->stylegroup, "Main");
    ASSERT_EQ(p.templates.syl.size(), 1u);
    EXPECT_EQ(p.templates.syl[0]->text, "s");
    EXPECT_TRUE(p.templates.furi.empty());
    ASSERT_EQ(p.problems.size(), 1u);
    EXPECT_EQ(p.problems[0].message, "Unable to combine furi class templates with the syl class this template has\n");
}

TEST(KaraokeTemplates, StockModeKeepsCharAsAModifierAndLineAsTheText)
{
    const Parsed p = parseTemplates({comment("template syl char", "c"), comment("template line", "l"),
                                     comment("template lword", "w")},
                                    false, number);
    ASSERT_EQ(p.templates.syl.size(), 1u);
    EXPECT_TRUE(p.templates.syl[0]->perchar);
    EXPECT_TRUE(p.templates.chr.empty());
    ASSERT_EQ(p.templates.line.size(), 2u);
    EXPECT_EQ(p.templates.line[0]->text, "l");
    EXPECT_EQ(p.templates.line[1]->unit, Unit::Word);
}
