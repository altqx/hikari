// V1-S: editing target and protected reference guards (docs/qt/ux/workspaces.md).

#include "hikari/application/workspace.h"

#include <gtest/gtest.h>

using namespace hikari::application;

TEST(Workspace, FirstDocumentIsTheEditingTarget)
{
    Workspace w;
    EXPECT_FALSE(w.editingTarget());
    const auto a = w.add("a.ass");
    const auto b = w.add("b.ass");
    EXPECT_EQ(w.editingTarget(), a);
    EXPECT_EQ(*w.title(b), "b.ass");
    EXPECT_TRUE(w.checkContentCommand(a));
    EXPECT_EQ(w.checkContentCommand(b).error(), TargetRefusal::NotEditingTarget);
    EXPECT_EQ(w.checkContentCommand(DocumentId{99}).error(), TargetRefusal::NoDocument);
}

TEST(Workspace, ProtectedReferenceRefusesContentCommands)
{
    Workspace w;
    const auto a = w.add("a.ass");
    const auto ref = w.add("reference.ass");
    ASSERT_TRUE(w.setReference(ref));
    EXPECT_EQ(w.checkContentCommand(ref).error(), TargetRefusal::ProtectedReference);
    // The reference can't become the editing target implicitly, and the
    // editing target can't become the reference.
    EXPECT_FALSE(w.setEditingTarget(ref));
    EXPECT_FALSE(w.setReference(a));
    EXPECT_EQ(w.editingTarget(), a);
}

TEST(Workspace, PromotingTheReferenceIsExplicit)
{
    Workspace w;
    const auto a = w.add("a.ass");
    const auto ref = w.add("reference.ass");
    ASSERT_TRUE(w.setReference(ref));
    ASSERT_TRUE(w.promoteReference());
    EXPECT_EQ(w.editingTarget(), ref);
    EXPECT_FALSE(w.reference());
    EXPECT_TRUE(w.checkContentCommand(ref));
    EXPECT_TRUE(w.title(a)); // the previous target stays open
    EXPECT_FALSE(w.promoteReference());
}

TEST(Workspace, RemovingTheTargetSkipsTheReference)
{
    Workspace w;
    const auto a = w.add("a.ass");
    const auto ref = w.add("reference.ass");
    const auto c = w.add("c.ass");
    ASSERT_TRUE(w.setReference(ref));
    ASSERT_TRUE(w.remove(a));
    EXPECT_EQ(w.editingTarget(), c);
    ASSERT_TRUE(w.remove(c));
    EXPECT_FALSE(w.editingTarget()); // only the protected reference is left
    EXPECT_EQ(w.reference(), ref);
}

// P6: the tabs are the Documents but the protected reference, in order.
TEST(Workspace, TabsLeaveOutTheReference)
{
    Workspace w;
    const auto a = w.add("a.ass");
    const auto ref = w.add("reference.ass");
    const auto c = w.add("c.ass");
    ASSERT_TRUE(w.setReference(ref));
    EXPECT_EQ(w.tabs(), (std::vector<DocumentId>{a, c}));
}

// Legacy Notebook::DeletePage: closing the active tab shows the one that
// takes its place, or the last one when it was the last.
TEST(Workspace, ClosingTheTargetShowsTheNextTabOrTheLast)
{
    Workspace w;
    const auto a = w.add("a.ass");
    const auto b = w.add("b.ass");
    const auto c = w.add("c.ass");
    const auto d = w.add("d.ass");
    ASSERT_TRUE(w.setEditingTarget(b));
    ASSERT_TRUE(w.remove(b));
    EXPECT_EQ(w.editingTarget(), c);
    ASSERT_TRUE(w.setEditingTarget(d));
    ASSERT_TRUE(w.remove(d));
    EXPECT_EQ(w.editingTarget(), c); // the last tab
    ASSERT_TRUE(w.remove(a));        // not the target: it stays
    EXPECT_EQ(w.editingTarget(), c);
}

// P6: new content loads into the same tab (legacy OpenFile into the tab).
TEST(Workspace, AReplacementTakesTheTabsPlace)
{
    Workspace w;
    const auto a = w.add("a.ass");
    const auto b = w.add("b.ass");
    const auto c = w.add("c.ass");
    ASSERT_TRUE(w.setEditingTarget(b));
    const auto n = w.add("new.ass");
    ASSERT_TRUE(w.replace(b, n));
    EXPECT_EQ(w.tabs(), (std::vector<DocumentId>{a, n, c}));
    EXPECT_EQ(w.editingTarget(), n);
    EXPECT_FALSE(w.title(b));
    EXPECT_FALSE(w.replace(b, n));
}

// R2: the subtitles preview shows another tab's Document as the protected
// reference; it stays a tab (legacy's preview drew another tab's grid, the
// tab stayed in the notebook: SubsGrid::ShowPreviewWindow, SubsGridWindow.cpp:2082-2106).
TEST(Workspace, ATabReferenceStaysATabAndIsProtected)
{
    Workspace w;
    const auto a = w.add("a.ass");
    const auto b = w.add("b.ass");
    const auto c = w.add("c.ass");
    EXPECT_FALSE(w.setTabReference(a)); // never the editing target
    ASSERT_TRUE(w.setTabReference(b));
    EXPECT_EQ(w.reference(), b);
    EXPECT_TRUE(w.referenceIsTab());
    EXPECT_EQ(w.tabs(), (std::vector<DocumentId>{a, b, c}));
    EXPECT_EQ(w.checkContentCommand(b).error(), TargetRefusal::ProtectedReference);
    EXPECT_EQ(w.editingTarget(), a);
    // Another tab becomes the reference instead.
    ASSERT_TRUE(w.setTabReference(c));
    EXPECT_EQ(w.reference(), c);
    EXPECT_TRUE(w.checkContentCommand(b).error() == TargetRefusal::NotEditingTarget);
}

// R2: choosing the tab of a tab reference is the explicit operation that
// makes it the editing target; it stops being the reference.
TEST(Workspace, ChoosingATabReferencesTabEndsTheReference)
{
    Workspace w;
    const auto a = w.add("a.ass");
    const auto b = w.add("b.ass");
    ASSERT_TRUE(w.setTabReference(b));
    ASSERT_TRUE(w.setEditingTarget(b));
    EXPECT_EQ(w.editingTarget(), b);
    EXPECT_FALSE(w.reference());
    EXPECT_FALSE(w.referenceIsTab());
    EXPECT_TRUE(w.checkContentCommand(b));
    EXPECT_EQ(w.tabs(), (std::vector<DocumentId>{a, b}));
    // An opened reference (not a tab) still can't be chosen that way.
    const auto ref = w.add("reference.ass");
    ASSERT_TRUE(w.setReference(ref));
    EXPECT_FALSE(w.referenceIsTab());
    EXPECT_FALSE(w.setEditingTarget(ref));
    EXPECT_EQ(w.tabs(), (std::vector<DocumentId>{a, b}));
}

// R2: closing the editing target hands it to the tab now in its place (legacy
// DeletePage); when that is the tab reference, it stops being the reference.
TEST(Workspace, ClosingTheTargetBeforeATabReferenceEndsTheReference)
{
    Workspace w;
    const auto a = w.add("a.ass");
    const auto b = w.add("b.ass");
    const auto c = w.add("c.ass");
    ASSERT_TRUE(w.setTabReference(b));
    ASSERT_TRUE(w.remove(a));
    EXPECT_EQ(w.editingTarget(), b);
    EXPECT_FALSE(w.reference());
    // Closing the tab reference itself ends it too; the target stays.
    ASSERT_TRUE(w.setTabReference(c));
    ASSERT_TRUE(w.remove(c));
    EXPECT_FALSE(w.reference());
    EXPECT_EQ(w.editingTarget(), b);
}

// R2: ending a tab reference keeps it where it was among the tabs; an
// opened reference that stops being the reference becomes a tab in its order.
TEST(Workspace, EndingAReferenceKeepsItsDocumentOpen)
{
    Workspace w;
    const auto a = w.add("a.ass");
    const auto ref = w.add("reference.ass");
    const auto c = w.add("c.ass");
    ASSERT_TRUE(w.setReference(ref));
    EXPECT_EQ(w.tabs(), (std::vector<DocumentId>{a, c}));
    ASSERT_TRUE(w.setTabReference(c)); // the opened reference becomes a tab
    EXPECT_EQ(w.tabs(), (std::vector<DocumentId>{a, ref, c}));
    ASSERT_TRUE(w.setReference(std::nullopt));
    EXPECT_EQ(w.tabs(), (std::vector<DocumentId>{a, ref, c}));
    EXPECT_FALSE(w.reference());
}
