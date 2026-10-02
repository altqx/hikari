// F1: destination and write coordination against a controllable file port.
// These prove coordination only; filesystem durability is F2's evidence.

#include "hikari/application/write_coordinator.h"

#include <gtest/gtest.h>

#include <vector>

using namespace hikari::application;

namespace {

struct FakePort : FilePort {
    struct Started {
        PermitId permit;
        DestinationKey destination;
        std::size_t bytes;
    };
    std::vector<Started> started;
    std::vector<PermitId> cancelRequests;
    void startWrite(PermitId permit, const DestinationKey &destination, std::vector<std::byte> bytes) override
    {
        started.push_back({permit, destination, bytes.size()});
    }
    void requestCancel(PermitId permit) override { cancelRequests.push_back(permit); }
};

struct Fixture : ::testing::Test {
    FakePort port;
    std::vector<WriteResult> results;
    WriteCoordinator coordinator{port, [this](const WriteResult &r) { results.push_back(r); }};
    const DocumentId a{1}, b{2};
    const DestinationKey fileA{"/subs/a.ass"}, fileB{"/subs/b.ass"}, fresh{"/subs/new.ass"};

    std::vector<std::byte> snapshot(std::size_t n = 4) { return std::vector<std::byte>(n, std::byte{'x'}); }
};

} // namespace

TEST_F(Fixture, SaveToOwnDestinationPublishesOneTerminalResult)
{
    coordinator.associate(a, fileA);
    const auto permit = coordinator.requestPermit(a, fileA, 7);
    ASSERT_TRUE(permit);
    ASSERT_TRUE(coordinator.write(*permit, snapshot(10)));
    ASSERT_EQ(port.started.size(), 1u);
    EXPECT_EQ(port.started[0].bytes, 10u);
    EXPECT_TRUE(results.empty()); // nothing is published before the adapter reports
    coordinator.complete(*permit, WriteOutcome::Written);
    coordinator.complete(*permit, WriteOutcome::Failed); // a second report is ignored
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].outcome, WriteOutcome::Written);
    EXPECT_EQ(results[0].revision, 7u);
    EXPECT_EQ(coordinator.ignoredReports(), 1u);
}

TEST_F(Fixture, SaveAsOntoAnotherOpenDocumentIsBlocked)
{
    coordinator.associate(a, fileA);
    coordinator.associate(b, fileB);
    EXPECT_EQ(coordinator.requestPermit(a, fileB, 1).error(), PermitRefusal::DestinationOwnedByOtherDocument);
    EXPECT_TRUE(coordinator.requestPermit(a, fresh, 1)); // an unassociated destination is fine
}

TEST_F(Fixture, OverlappingWritesToOneDestinationAreRefused)
{
    coordinator.associate(a, fileA);
    const auto first = coordinator.requestPermit(a, fileA, 1);
    ASSERT_TRUE(first);
    EXPECT_EQ(coordinator.requestPermit(a, fileA, 2).error(), PermitRefusal::WriteInProgress);
    ASSERT_TRUE(coordinator.write(*first, snapshot()));
    EXPECT_EQ(coordinator.requestPermit(a, fileA, 2).error(), PermitRefusal::WriteInProgress);
    coordinator.complete(*first, WriteOutcome::Written);
    EXPECT_TRUE(coordinator.requestPermit(a, fileA, 2)); // free again after the terminal result
}

TEST_F(Fixture, RevokedPermitNeverWrites)
{
    coordinator.associate(a, fileA);
    const auto permit = coordinator.requestPermit(a, fileA, 1);
    coordinator.revoke(*permit);
    EXPECT_EQ(coordinator.write(*permit, snapshot()).error(), WriteRefusal::Revoked);
    EXPECT_TRUE(port.started.empty());
    EXPECT_FALSE(coordinator.hasActivePermit(fileA));
}

TEST_F(Fixture, StaleDestinationIsRefused)
{
    // Another Document becomes associated with the destination after the grant.
    const auto permit = coordinator.requestPermit(a, fresh, 1);
    coordinator.associate(b, fresh);
    EXPECT_EQ(coordinator.write(*permit, snapshot()).error(), WriteRefusal::StaleDestination);
    // The requester's own association changes after the grant.
    coordinator.associate(a, fileA);
    const auto own = coordinator.requestPermit(a, fileA, 2);
    coordinator.associate(a, fileB);
    EXPECT_EQ(coordinator.write(*own, snapshot()).error(), WriteRefusal::StaleDestination);
    EXPECT_TRUE(port.started.empty());
}

TEST_F(Fixture, PermitWritesAtMostOnce)
{
    const auto permit = coordinator.requestPermit(a, fresh, 1);
    ASSERT_TRUE(coordinator.write(*permit, snapshot()));
    EXPECT_EQ(coordinator.write(*permit, snapshot()).error(), WriteRefusal::AlreadyStarted);
    EXPECT_EQ(coordinator.write(PermitId{99}, snapshot()).error(), WriteRefusal::UnknownPermit);
}

TEST_F(Fixture, CancelIsResolvedByTheAdapter)
{
    // Cancelled in time: the adapter acknowledges cancellation.
    const auto early = coordinator.requestPermit(a, fresh, 1);
    ASSERT_TRUE(coordinator.write(*early, snapshot()));
    coordinator.cancel(*early);
    ASSERT_EQ(port.cancelRequests.size(), 1u);
    coordinator.complete(*early, WriteOutcome::Cancelled);
    // Cancelled too late: the write had already published, so it is Written.
    const auto late = coordinator.requestPermit(a, fileB, 1);
    ASSERT_TRUE(coordinator.write(*late, snapshot()));
    coordinator.cancel(*late);
    coordinator.complete(*late, WriteOutcome::Written);
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0].outcome, WriteOutcome::Cancelled);
    EXPECT_EQ(results[1].outcome, WriteOutcome::Written);
}

TEST_F(Fixture, CancelBeforeStartNeedsNoAdapter)
{
    const auto permit = coordinator.requestPermit(a, fresh, 1);
    coordinator.cancel(*permit);
    EXPECT_TRUE(port.cancelRequests.empty());
    EXPECT_EQ(coordinator.write(*permit, snapshot()).error(), WriteRefusal::Revoked);
    EXPECT_TRUE(results.empty());
}

TEST_F(Fixture, ReportsForUnstartedOrUnknownPermitsAreIgnored)
{
    const auto permit = coordinator.requestPermit(a, fresh, 1);
    coordinator.complete(*permit, WriteOutcome::Written);
    coordinator.complete(PermitId{42}, WriteOutcome::Written);
    EXPECT_TRUE(results.empty());
    EXPECT_EQ(coordinator.ignoredReports(), 2u);
}

TEST_F(Fixture, UncertainDurabilityIsItsOwnOutcome)
{
    const auto permit = coordinator.requestPermit(a, fresh, 3);
    ASSERT_TRUE(coordinator.write(*permit, snapshot()));
    coordinator.complete(*permit, WriteOutcome::DurabilityUncertain);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].outcome, WriteOutcome::DurabilityUncertain);
}
