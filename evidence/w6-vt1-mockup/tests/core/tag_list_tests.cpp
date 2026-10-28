// E6: the Line editor's tag list against legacy TextEditorTagList.cpp and
// DialogueTextEditor.cpp at 20d647c4. Expected values were traced by hand
// through PopupTagList, PopupWindow, TextEditor::OnCharPress (426-492),
// OnAccelerator (563-567, 722-750, 802-808) and PutTag (2829-2852).

#include "hikari/core/tag_list.h"

#include <gtest/gtest.h>

#include <string>

using namespace hikari::core::taglist;

namespace {

std::string s8(std::u16string_view s)
{
    return std::string(s.begin(), s.end()); // the fixtures are ASCII
}

// Types `typed` into `text` at `caret` one character at a time, as
// OnCharPress sees each WM_CHAR.
struct Field {
    std::u16string text;
    std::size_t caret = 0;
    Completion completion;
    int options = 0;

    void type(std::u16string_view typed)
    {
        for (const char16_t ch : typed) {
            text.insert(caret, 1, ch);
            ++caret;
            completion.typed(text, caret, ch, options);
        }
    }
    bool enter()
    {
        const auto put = completion.put(text, caret);
        if (!put)
            return false;
        text = put->text;
        caret = put->caret;
        return true;
    }
};

std::vector<std::string> shownTags(const TagList &list)
{
    std::vector<std::string> out;
    for (const auto i : list.shownEntries())
        out.push_back(s8(entries()[i].tag));
    return out;
}

} // namespace

TEST(TagList, SeventySevenEntriesInLegacyOrder)
{
    ASSERT_EQ(entries().size(), 77u);
    EXPECT_EQ(s8(entries().front().tag), "1a");
    EXPECT_EQ(s8(entries()[20].tag), "a");
    EXPECT_EQ(s8(entries()[48].tag), "k");
    EXPECT_EQ(s8(entries()[49].tag), "K"); // after "k": legacy keeps InitList's order, not sorted
    EXPECT_EQ(s8(entries().back().tag), "z");
    int normal = 0, visual = 0, vsfilter = 0, brackets = 0;
    for (const auto &e : entries()) {
        normal += e.type == TypeNormal;
        visual += e.type == TypeUsedInVisual;
        vsfilter += e.type == TypeVsfilterMod;
        brackets += e.needBrackets;
    }
    EXPECT_EQ(normal, 20);
    EXPECT_EQ(visual, 30);
    EXPECT_EQ(vsfilter, 27);
    EXPECT_EQ(brackets, 26);
}

TEST(TagList, OptionsDecideWhichEntriesShow)
{
    // ShowItem(int): type 0 always; "Show all tags" (1) adds the visual
    // tools' tags, "Show VSFiltermod tags" (2) the VSFilterMod ones; the
    // description bit (4) shows nothing more.
    const std::size_t expected[8] = {20, 50, 47, 77, 20, 50, 47, 77};
    for (int options = 0; options < 8; ++options) {
        TagList list(options);
        EXPECT_EQ(list.count(), expected[options]) << options;
        EXPECT_EQ(list.showDescription(), (options & ShowDescription) != 0);
    }
    TagList none(0);
    EXPECT_EQ(shownTags(none).front(), "alpha");
    EXPECT_EQ(shownTags(none).back(), "yshad");
}

// Each entry under each option: whether it shows, its row, and what choosing
// it after "{\" writes (GetTag: "()" for the 26 with arguments, the caret
// between them).
TEST(TagList, EveryEntryUnderEveryOptionCompletes)
{
    for (int options = 0; options < 8; ++options) {
        int row = 0;
        for (std::size_t e = 0; e < entries().size(); ++e) {
            const Entry &entry = entries()[e];
            const bool shown = shownByOptions(entry, options);
            EXPECT_EQ(shown, entry.type == TypeNormal || (entry.type & options) != 0) << e;
            if (!shown)
                continue;
            Field field{u"ab", 1, {}, options};
            field.type(u"{\\");
            ASSERT_TRUE(field.completion.open());
            ASSERT_TRUE(field.completion.list()->popupShown());
            for (int k = 0; k < row; ++k)
                field.completion.move(1);
            EXPECT_EQ(field.completion.list()->entryAt(field.completion.list()->selection()), e);
            ASSERT_TRUE(field.enter()) << e;
            const std::string tag = s8(entry.tag);
            const std::string written = entry.needBrackets ? tag + "()" : tag;
            EXPECT_EQ(s8(field.text), "a{\\" + written + "b") << options << " " << e;
            EXPECT_EQ(field.caret, 3 + tag.size() + (entry.needBrackets ? 1u : 0u));
            EXPECT_FALSE(field.completion.open());
            ++row;
        }
    }
}

TEST(TagList, OpensForABackslashInsideABlockOnly)
{
    Field inBlock{u"", 0, {}, 3};
    inBlock.type(u"{\\");
    ASSERT_TRUE(inBlock.completion.open());
    EXPECT_EQ(inBlock.completion.list()->count(), 77u);
    EXPECT_EQ(inBlock.completion.list()->selection(), 0);
    EXPECT_TRUE(inBlock.completion.list()->keyword().empty());

    // Plain text: someone writing \N or \h gets no list.
    Field plain{u"", 0, {}, 3};
    plain.type(u"a\\");
    EXPECT_FALSE(plain.completion.open());
    // After a closed block.
    Field closed{u"{}", 2, {}, 3};
    closed.type(u"\\");
    EXPECT_FALSE(closed.completion.open());
}

TEST(TagList, TypedLettersNarrowCaseSensitively)
{
    Field field{u"", 0, {}, 3};
    field.type(u"{\\k");
    EXPECT_EQ(shownTags(*field.completion.list()), (std::vector<std::string>{"k", "ko", "kt"}));
    Field upper{u"", 0, {}, 3};
    upper.type(u"{\\K");
    EXPECT_EQ(shownTags(*upper.completion.list()), (std::vector<std::string>{"K"}));
    field.type(u"o");
    EXPECT_EQ(field.completion.list()->keyword(), u"ko");
    EXPECT_EQ(shownTags(*field.completion.list()), (std::vector<std::string>{"ko"}));
    // A character no tag continues with closes the list.
    field.type(u"1");
    EXPECT_FALSE(field.completion.open());
}

TEST(TagList, OpensOnTheSecondCharacterOrBeforeABlockEnd)
{
    // After "\" the next character opens it with that letter as the keyword.
    Field second{u"{\\", 2, {}, 0};
    second.type(u"f");
    ASSERT_TRUE(second.completion.open());
    EXPECT_EQ(second.completion.list()->keyword(), u"f");
    EXPECT_EQ(shownTags(*second.completion.list()),
              (std::vector<std::string>{"fad", "fade", "fax", "fay", "fe", "fsp"}));
    // The third character at the end of the text does not open it (none of
    // the three conditions holds) ...
    Field third{u"{\\f", 3, {}, 0};
    third.type(u"s");
    EXPECT_FALSE(third.completion.open());
    // ... but before a "}" the whole partial tag is the keyword.
    Field beforeEnd{u"{\\f}", 3, {}, 3};
    beforeEnd.type(u"s");
    ASSERT_TRUE(beforeEnd.completion.open());
    EXPECT_EQ(beforeEnd.completion.list()->keyword(), u"fs");
    EXPECT_EQ(shownTags(*beforeEnd.completion.list()),
              (std::vector<std::string>{"fs", "fsc", "fscx", "fscy", "fsp", "fsvp"}));
    // And before another tag's backslash.
    Field beforeTag{u"{\\b\\i1}", 3, {}, 3};
    beforeTag.type(u"o");
    ASSERT_TRUE(beforeTag.completion.open());
    EXPECT_EQ(beforeTag.completion.list()->keyword(), u"bo");
    EXPECT_EQ(shownTags(*beforeTag.completion.list()), (std::vector<std::string>{"bord"}));
    // Without a backslash after the "{" the typed run is the keyword.
    Field noTag{u"{a}", 2, {}, 3};
    noTag.type(u"b");
    ASSERT_TRUE(noTag.completion.open());
    EXPECT_EQ(noTag.completion.list()->keyword(), u"ab");
    EXPECT_FALSE(noTag.completion.list()->popupShown()); // nothing matches: no popup, the list stays
}

TEST(TagList, AListWithoutAPopupSwallowsEnterAndClosesOnTheNextCharacter)
{
    // "{" typed before a "}": the keyword is "{", which nothing starts with.
    Field field{u"}", 0, {}, 3};
    field.type(u"{");
    ASSERT_TRUE(field.completion.open());
    EXPECT_FALSE(field.completion.list()->popupShown());
    EXPECT_EQ(field.completion.list()->selection(), -1);
    EXPECT_TRUE(field.completion.move(1)); // Down is taken and does nothing
    EXPECT_FALSE(field.enter());           // Enter is taken too (PutTag finds no item)
    EXPECT_TRUE(field.completion.open());
    field.type(u"a");
    EXPECT_FALSE(field.completion.open());
}

TEST(TagList, ChoosingReplacesThePartialTagUpToTheCaret)
{
    Field field{u"{\\f}", 3, {}, 3};
    field.type(u"s");
    field.completion.move(1);
    field.completion.move(1);
    field.completion.move(1);
    field.completion.move(1); // fs, fsc, fscx, fscy, fsp
    ASSERT_TRUE(field.enter());
    EXPECT_EQ(s8(field.text), "{\\fsp}");
    EXPECT_EQ(field.caret, 5u);

    Field brackets{u"", 0, {}, 3};
    brackets.type(u"{\\po");
    ASSERT_TRUE(brackets.completion.open()); // opened by the backslash, narrowed by "po"
    EXPECT_EQ(shownTags(*brackets.completion.list()), (std::vector<std::string>{"pos"}));
    ASSERT_TRUE(brackets.enter());
    EXPECT_EQ(s8(brackets.text), "{\\pos()");
    EXPECT_EQ(brackets.caret, 6u); // between the brackets
}

TEST(TagList, SelectionWrapsAndKeepsTenRowsInView)
{
    Field field{u"", 0, {}, 3};
    field.type(u"{\\");
    TagList &list = *field.completion.list();
    field.completion.move(-1); // from the first row to the last
    EXPECT_EQ(list.selection(), 76);
    EXPECT_EQ(list.scrollPosition(), 67);
    field.completion.move(1); // and back
    EXPECT_EQ(list.selection(), 0);
    EXPECT_EQ(list.scrollPosition(), 0);
    for (int i = 0; i < 10; ++i)
        field.completion.move(1);
    EXPECT_EQ(list.selection(), 10);
    EXPECT_EQ(list.scrollPosition(), 1);
    // A narrower keyword makes the popup again, its first row selected.
    field.type(u"f");
    EXPECT_EQ(list.selection(), 0);
    EXPECT_EQ(list.scrollPosition(), 0);
}

TEST(TagList, OptionsChangedFromTheMenuDropTheKeywordNarrowing)
{
    Field field{u"", 0, {}, 0};
    field.type(u"{\\f");
    TagList &list = *field.completion.list();
    EXPECT_EQ(list.count(), 6u);
    // "Show all tags": the options alone decide again, keyword or not.
    list.filterByOptions(ShowAllTags);
    EXPECT_EQ(list.count(), 50u);
    EXPECT_EQ(list.keyword(), u"f");
    EXPECT_EQ(list.selection(), 0);
    // The next character narrows by the whole keyword again.
    field.type(u"s");
    EXPECT_EQ(shownTags(list), (std::vector<std::string>{"fs", "fscx", "fscy", "fsp"}));
    // The options bring back what the keyword hid, and the popup with them.
    TagList empty(0);
    empty.filterByKeyword(u"w");
    empty.filterByOptions(0);
    EXPECT_TRUE(empty.popupShown());
}

TEST(TagList, PointerSelectsShownRowsAfterTheFirstEvent)
{
    TagList list(3);
    list.popup(0);
    EXPECT_FALSE(list.pointerAt(4)); // blockMouseEvent: the first event is ignored
    EXPECT_EQ(list.selection(), 0);
    EXPECT_TRUE(list.pointerAt(4));
    EXPECT_EQ(list.selection(), 4);
    EXPECT_FALSE(list.pointerAt(10)); // below the ten shown rows
    EXPECT_EQ(list.selection(), 4);
    list.scrollBy(3); // one wheel notch down
    EXPECT_EQ(list.scrollPosition(), 3);
    EXPECT_TRUE(list.pointerAt(0));
    EXPECT_EQ(list.selection(), 3);
    list.scrollBy(-6);
    EXPECT_EQ(list.scrollPosition(), 0);
    list.scrollBy(300);
    EXPECT_EQ(list.scrollPosition(), 67);
    TagList few(0);
    few.filterByKeyword(u"f");
    few.popup(0);
    few.scrollBy(3); // six rows: nothing to scroll
    EXPECT_EQ(few.scrollPosition(), 0);
}
