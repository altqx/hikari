#pragma once

// Never touches the filesystem (a FilePort writes) nor Document content.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace hikari::application {

struct DocumentId {
    std::uint64_t value = 0;
    auto operator<=>(const DocumentId &) const = default;
};

// Platform-normalized identity of a destination (the adapter resolves aliases).
struct DestinationKey {
    std::string value;
    auto operator<=>(const DestinationKey &) const = default;
};

struct PermitId {
    std::uint64_t value = 0;
    auto operator<=>(const PermitId &) const = default;
};

enum class WriteOutcome {
    Written,             // the adapter reports a complete, acknowledged write
    Failed,              // nothing new was published at the destination
    Cancelled,           // the adapter acknowledged cancellation before publishing
    DurabilityUncertain, // published, but the adapter cannot confirm it persisted
};

enum class PermitRefusal {
    DestinationOwnedByOtherDocument, // another open Document is associated with it
    WriteInProgress,                 // another permit for this destination is still active
};

enum class WriteRefusal {
    UnknownPermit,
    Revoked,           // the permit was revoked before writing started
    StaleDestination,  // the destination's association changed after authorization
    AlreadyStarted,    // a permit writes at most once
};

struct WriteResult {
    PermitId permit;
    DocumentId document;
    DestinationKey destination;
    std::uint64_t revision = 0; // the revision whose snapshot was written
    WriteOutcome outcome = WriteOutcome::Failed;
};

// Adapter performing the actual write. Implementations report each started
// write exactly once through WriteCoordinator::complete.
class FilePort {
public:
    virtual ~FilePort() = default;
    virtual void startWrite(PermitId permit, const DestinationKey &destination, std::vector<std::byte> bytes) = 0;
    // Asks the adapter to stop; it still reports the actual outcome.
    virtual void requestCancel(PermitId permit) = 0;
};

class WriteCoordinator {
public:
    using ResultListener = std::function<void(const WriteResult &)>;

    WriteCoordinator(FilePort &port, ResultListener onResult);

    void associate(DocumentId document, const DestinationKey &destination);
    void dissociate(DocumentId document);
    std::optional<DestinationKey> association(DocumentId document) const;

    // Authorizes one write of a revision to a destination.
    std::expected<PermitId, PermitRefusal> requestPermit(DocumentId document, const DestinationKey &destination,
                                                         std::uint64_t revision);
    // Revokes an unstarted permit (Document closed, destination changed).
    void revoke(PermitId permit);
    std::expected<void, WriteRefusal> write(PermitId permit, std::vector<std::byte> snapshot);
    void cancel(PermitId permit);

    // Called by the FilePort. The first report for a started permit becomes its
    // terminal result; later reports are ignored and counted.
    void complete(PermitId permit, WriteOutcome outcome);

    bool hasActivePermit(const DestinationKey &destination) const;
    std::size_t ignoredReports() const { return m_ignoredReports; }

private:
    struct Permit {
        DocumentId document;
        DestinationKey destination;
        std::uint64_t revision = 0;
        std::optional<DestinationKey> associationAtGrant;
        bool revoked = false;
        bool started = false;
        bool finished = false;
    };

    std::optional<DocumentId> ownerOf(const DestinationKey &destination) const;

    FilePort &m_port;
    ResultListener m_onResult;
    std::map<DocumentId, DestinationKey> m_associations;
    std::map<PermitId, Permit> m_permits;
    std::uint64_t m_nextPermit = 1;
    std::size_t m_ignoredReports = 0;
};

} // namespace hikari::application
