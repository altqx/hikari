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
    EXPECT_TRUE(w.title(a));
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
    EXPECT_FALSE(w.editingTarget());
    EXPECT_EQ(w.reference(), ref);
}

TEST(Workspace, TabsLeaveOutTheReference)
{
    Workspace w;
    const auto a = w.add("a.ass");
    const auto ref = w.add("reference.ass");
    const auto c = w.add("c.ass");
    ASSERT_TRUE(w.setReference(ref));
    EXPECT_EQ(w.tabs(), (std::vector<DocumentId>{a, c}));
}

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
    EXPECT_EQ(w.editingTarget(), c);
    ASSERT_TRUE(w.remove(a));
    EXPECT_EQ(w.editingTarget(), c);
}

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

TEST(Workspace, ATabReferenceStaysATabAndIsProtected)
{
    Workspace w;
    const auto a = w.add("a.ass");
    const auto b = w.add("b.ass");
    const auto c = w.add("c.ass");
    EXPECT_FALSE(w.setTabReference(a));
    ASSERT_TRUE(w.setTabReference(b));
    EXPECT_EQ(w.reference(), b);
    EXPECT_TRUE(w.referenceIsTab());
    EXPECT_EQ(w.tabs(), (std::vector<DocumentId>{a, b, c}));
    EXPECT_EQ(w.checkContentCommand(b).error(), TargetRefusal::ProtectedReference);
    EXPECT_EQ(w.editingTarget(), a);
    ASSERT_TRUE(w.setTabReference(c));
    EXPECT_EQ(w.reference(), c);
    EXPECT_TRUE(w.checkContentCommand(b).error() == TargetRefusal::NotEditingTarget);
}

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
    const auto ref = w.add("reference.ass");
    ASSERT_TRUE(w.setReference(ref));
    EXPECT_FALSE(w.referenceIsTab());
    EXPECT_FALSE(w.setEditingTarget(ref));
    EXPECT_EQ(w.tabs(), (std::vector<DocumentId>{a, b}));
}

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
    ASSERT_TRUE(w.setTabReference(c));
    ASSERT_TRUE(w.remove(c));
    EXPECT_FALSE(w.reference());
    EXPECT_EQ(w.editingTarget(), b);
}

TEST(Workspace, EndingAReferenceKeepsItsDocumentOpen)
{
    Workspace w;
    const auto a = w.add("a.ass");
    const auto ref = w.add("reference.ass");
    const auto c = w.add("c.ass");
    ASSERT_TRUE(w.setReference(ref));
    EXPECT_EQ(w.tabs(), (std::vector<DocumentId>{a, c}));
    ASSERT_TRUE(w.setTabReference(c));
    EXPECT_EQ(w.tabs(), (std::vector<DocumentId>{a, ref, c}));
    ASSERT_TRUE(w.setReference(std::nullopt));
    EXPECT_EQ(w.tabs(), (std::vector<DocumentId>{a, ref, c}));
    EXPECT_FALSE(w.reference());
}

TEST(Workspace, SwappingTabsKeepsTheRoles)
{
    Workspace w;
    const auto a = w.add("a.ass");
    const auto r = w.add("reference.ass");
    ASSERT_TRUE(w.setReference(r));
    const auto b = w.add("b.ass");
    const auto c = w.add("c.ass");
    ASSERT_TRUE(w.setEditingTarget(c));
    ASSERT_TRUE(w.swapTabs(0, 2));
    EXPECT_EQ(w.tabs(), (std::vector<DocumentId>{c, b, a}));
    EXPECT_EQ(w.editingTarget(), c);
    EXPECT_EQ(w.reference(), r);
    EXPECT_TRUE(w.swapTabs(1, 1));
    EXPECT_EQ(w.tabs(), (std::vector<DocumentId>{c, b, a}));
    EXPECT_FALSE(w.swapTabs(0, 3));
    EXPECT_EQ(w.documents().size(), 4u);
}
