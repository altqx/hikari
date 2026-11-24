#include "hikari/application/problems.h"
#include "hikari/core/ass_load.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace hikari;
using namespace hikari::application;

namespace {

core::Document load(std::string_view text)
{
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return core::loadAss(bytes).document;
}

constexpr std::string_view kScript = "[Script Info]\n"
                                     "ScriptType: v4.00+\n"
                                     "\n"
                                     "[Events]\n"
                                     "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                                     "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,first\n"
                                     "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,second\n";

Problem on(const core::Document &document, std::uint64_t line, std::string message)
{
    Problem p;
    p.line = core::LineId{line};
    p.message = std::move(message);
    for (const core::LineRecord *record : document.lines())
        if (record->id.value == line)
            p.against = record->text;
    return p;
}

struct Changed {
    std::uint64_t document;
    std::vector<std::string> sources;
    bool operator==(const Changed &) const = default;
};

class ProblemsTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        problems.setObserver([this](DocumentId d, std::vector<std::string> sources) {
            changes.push_back(Changed{d.value, std::move(sources)});
        });
    }

    core::Document document = load(kScript);
    Problems problems;
    std::vector<Changed> changes;
    const DocumentId a{1}, b{2};
};

}

TEST_F(ProblemsTest, SetReplacesOneSetAndKeepsTheOthers)
{
    ASSERT_TRUE(problems.set("plugin:x", "x", "X", a, "default", {on(document, 1, "one"), on(document, 2, "two")}));
    ASSERT_TRUE(problems.set("plugin:x", "x", "X", a, "grammar", {on(document, 1, "three")}));
    ASSERT_TRUE(problems.set("plugin:x", "x", "X", a, "default", {on(document, 2, "four")}));
    const auto held = problems.of(a);
    ASSERT_EQ(held.size(), 2u);
    EXPECT_EQ(held[0].set, "default");
    EXPECT_EQ(held[0].problem.message, "four");
    EXPECT_EQ(held[1].set, "grammar");
    EXPECT_EQ(problems.name("x"), "X");
    EXPECT_EQ(changes, (std::vector<Changed>{{1, {"x"}}, {1, {"x"}}, {1, {"x"}}}));
}

TEST_F(ProblemsTest, IdsStayWhileTheProblemLivesAndAreNeverReused)
{
    ASSERT_TRUE(problems.set("plugin:x", "x", "X", a, "default", {on(document, 1, "one")}));
    const std::string first = problems.of(a).at(0).id;
    EXPECT_LE(first.size(), 64u);
    ASSERT_TRUE(problems.set("plugin:y", "y", "Y", a, "default", {on(document, 2, "two")}));
    EXPECT_EQ(problems.of(a).at(0).id, first);
    EXPECT_NE(problems.find(a, first), nullptr);
    EXPECT_EQ(problems.find(b, first), nullptr);
    ASSERT_TRUE(problems.set("plugin:x", "x", "X", a, "default", {on(document, 1, "one")}));
    EXPECT_NE(problems.of(a).at(0).id, first);
    EXPECT_EQ(problems.find(a, first), nullptr);
    EXPECT_TRUE(problems.gave(first));
    EXPECT_FALSE(problems.gave("p999"));
    EXPECT_FALSE(problems.gave("nope"));
}

TEST_F(ProblemsTest, ClearBySetDocumentOrAllAndCount)
{
    ASSERT_TRUE(problems.set("plugin:x", "x", "X", a, "default", {on(document, 1, "one"), on(document, 2, "two")}));
    ASSERT_TRUE(problems.set("plugin:x", "x", "X", a, "grammar", {on(document, 1, "three")}));
    ASSERT_TRUE(problems.set("plugin:x", "x", "X", b, "default", {on(document, 1, "four")}));
    changes.clear();
    EXPECT_EQ(problems.clear("x", a, "grammar"), 1u);
    EXPECT_EQ(problems.clear("x", a, "grammar"), 0u);
    EXPECT_EQ(problems.clear("x", std::nullopt, std::nullopt), 3u);
    EXPECT_TRUE(problems.of(a).empty());
    EXPECT_TRUE(problems.of(b).empty());
    EXPECT_EQ(problems.name("x"), "");
    EXPECT_EQ(changes, (std::vector<Changed>{{1, {"x"}}, {1, {"x"}}, {2, {"x"}}}));
}

TEST_F(ProblemsTest, AnEmptySetClearsAndTellsOnlyWhenSomethingWent)
{
    ASSERT_TRUE(problems.set("plugin:x", "x", "X", a, "default", {}));
    EXPECT_TRUE(changes.empty());
    ASSERT_TRUE(problems.set("plugin:x", "x", "X", a, "default", {on(document, 1, "one")}));
    ASSERT_TRUE(problems.set("plugin:x", "x", "X", a, "default", {}));
    EXPECT_TRUE(problems.of(a).empty());
    EXPECT_EQ(changes.size(), 2u);
}

TEST_F(ProblemsTest, ClearOwnerDropsEverySourceItHolds)
{
    ASSERT_TRUE(problems.set("plugin:x", "x", "X", a, "default", {on(document, 1, "one")}));
    ASSERT_TRUE(problems.set("plugin:x", "plugin:x/ltex", "X", a, "default", {on(document, 1, "two")}));
    ASSERT_TRUE(problems.set("plugin:y", "y", "Y", a, "default", {on(document, 2, "three")}));
    changes.clear();
    problems.clearOwner("plugin:x");
    ASSERT_EQ(problems.of(a).size(), 1u);
    EXPECT_EQ(problems.of(a)[0].source, "y");
    EXPECT_EQ(changes, (std::vector<Changed>{{1, {"plugin:x/ltex", "x"}}}));
}

TEST_F(ProblemsTest, RefreshDropsChangedFieldsAndDeletedLines)
{
    Problem translation = on(document, 2, "tl");
    translation.field = ProblemField::Translation;
    translation.against.clear();
    ASSERT_TRUE(problems.set("plugin:x", "x", "X", a, "default",
                             {on(document, 1, "one"), on(document, 2, "two"), translation}));
    ASSERT_TRUE(problems.set("plugin:y", "y", "Y", a, "default", {on(document, 1, "three")}));
    problems.refresh(a, document);
    changes.clear();
    problems.refresh(a, document);
    EXPECT_TRUE(changes.empty());

    ASSERT_TRUE(document.setLineText(core::LineId{2}, u8"changed"));
    problems.refresh(a, document);
    ASSERT_EQ(problems.of(a).size(), 3u);
    EXPECT_EQ(changes, (std::vector<Changed>{{1, {"x"}}}));

    changes.clear();
    ASSERT_TRUE(document.removeLine(core::LineId{1}));
    problems.refresh(a, document);
    ASSERT_EQ(problems.of(a).size(), 1u);
    EXPECT_EQ(problems.of(a)[0].problem.message, "tl");
    EXPECT_EQ(changes, (std::vector<Changed>{{1, {"x", "y"}}}));
}

TEST_F(ProblemsTest, RefreshTellsWhenTheOpenQcNotesChange)
{
    problems.refresh(a, document);
    EXPECT_EQ(changes, (std::vector<Changed>{}));
    ASSERT_TRUE(setDocumentQcNotes(document, {QcNote{1500, u8"TLC", false, u8"odd"}}));
    problems.refresh(a, document);
    EXPECT_EQ(changes, (std::vector<Changed>{{1, {"qc"}}}));
    ASSERT_TRUE(setDocumentQcNotes(document, {QcNote{1500, u8"TLC", true, u8"odd"}}));
    problems.refresh(a, document);
    EXPECT_EQ(changes.size(), 2u);
}

TEST_F(ProblemsTest, AnOwnerHoldsAtMostTwentyThousandPerDocument)
{
    std::vector<Problem> many(Problems::kMostPerDocument - 1, on(document, 1, "m"));
    ASSERT_TRUE(problems.set("plugin:x", "x", "X", a, "default", many));
    EXPECT_TRUE(problems.set("plugin:x", "x", "X", a, "other", {on(document, 1, "m")}));
    EXPECT_FALSE(problems.set("plugin:x", "x", "X", a, "third", {on(document, 1, "m")}));
    EXPECT_TRUE(problems.set("plugin:x", "x", "X", a, "other", {on(document, 1, "m")}));
    EXPECT_TRUE(problems.set("plugin:x", "x", "X", b, "third", {on(document, 1, "m")}));
    EXPECT_TRUE(problems.set("plugin:y", "y", "Y", a, "default", {on(document, 1, "m")}));
    EXPECT_EQ(problems.of(a).size(), Problems::kMostPerDocument + 1);
}

TEST_F(ProblemsTest, KeepOnlyForgetsClosedDocumentsQuietly)
{
    ASSERT_TRUE(problems.set("plugin:x", "x", "X", a, "default", {on(document, 1, "one")}));
    ASSERT_TRUE(problems.set("plugin:x", "x", "X", b, "default", {on(document, 1, "two")}));
    changes.clear();
    problems.keepOnly({b});
    EXPECT_TRUE(problems.of(a).empty());
    EXPECT_EQ(problems.of(b).size(), 1u);
    EXPECT_TRUE(changes.empty());
}
