#pragma once

// Open, save and reopen (A2; docs/qt/proposals/application-lifecycle.md).
// Composes staged loading with acknowledged writing:
// - Opening and reloading stage a replacement first; a failed, cancelled or
//   stale replacement never touches existing work (L58-staged-replacement).
// - Saving commits the pending draft first, then writes one revision-bound
//   snapshot. Only an acknowledged write marks that snapshot saved; Save As
//   moves the association only after it (C43, L58-write-close).
// - Before overwriting its own file, a Document checks that the bytes there
//   are still the ones it loaded or last wrote; timestamps are not evidence.

#include "hikari/application/edit_session.h"
#include "hikari/application/write_coordinator.h"

#include <expected>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace hikari::application {

enum class ReadError { NotFound, AccessDenied, Failed };

// Adapter reading a whole file. Synchronous: staging keeps it off the
// published state, whichever thread runs it.
class FileReadPort {
public:
    virtual ~FileReadPort() = default;
    virtual std::expected<std::vector<std::byte>, ReadError> read(const DestinationKey &destination) = 0;
};

enum class OpenError {
    NotFound,
    AccessDenied,
    ReadFailed,
    InvalidFormat, // no reader found a Line (legacy "Invalid format")
    UnknownDocument,
    StaleTarget,   // the Document changed or was replaced after staging
};

enum class SaveRefusal {
    UnknownDocument,
    NoDestination,
    StalePlan,      // edits, a reload or a close happened after the plan was made
    ExternalChange, // the file changed or vanished since it was loaded or last written
    Collision,      // another open Document is associated with that destination
    WriteInProgress,
    InvalidDraft,   // the pending draft can't be committed (E63-invalid-commit)
};

// A prepared replacement. Nothing is published until activate().
struct StagedOpen {
    DestinationKey destination;
    core::LoadResult load;
    std::optional<DocumentId> replaces;
    std::uint64_t expectedGeneration = 0;
    std::uint64_t expectedRevision = 0;
};

struct SavePlan {
    DocumentId document;
    std::uint64_t generation = 0;
    std::uint64_t revision = 0;
    ContentId content;
    DestinationKey destination;
    std::vector<std::byte> bytes;
    bool saveAs = false;
};

struct SaveStatus {
    PermitId permit;
    std::optional<WriteOutcome> outcome; // empty while the write is running
};

class DocumentFiles {
public:
    DocumentFiles(FileReadPort &reader, WriteCoordinator &writes);

    std::expected<StagedOpen, OpenError> stageOpen(const DestinationKey &destination);
    std::expected<StagedOpen, OpenError> stageReload(DocumentId document);
    // Publishes a staged open once: a new Document, or the replacement of the
    // one it was staged for when that Document is still exactly as it was.
    std::expected<DocumentId, OpenError> activate(StagedOpen staged);

    // A new Untitled Document (P1; legacy SubsGrid::LoadDefault): the default
    // Script Info, the Default Style and one empty Line, with no destination,
    // so its first save needs Save As. It starts saved: an untouched new
    // Document never asks to be saved.
    DocumentId createNew();

    EditSession *session(DocumentId document);
    std::optional<DestinationKey> destination(DocumentId document) const;
    std::uint64_t generation(DocumentId document) const; // 0 for an unknown Document
    bool close(DocumentId document);

    // Commits the draft, encodes the snapshot and binds it to the revision.
    std::expected<SavePlan, SaveRefusal> prepareSave(DocumentId document,
                                                     std::optional<DestinationKey> saveAs = std::nullopt);
    // Starts the write. overwriteExternalChange is the user's explicit choice
    // after being shown an ExternalChange refusal.
    std::expected<PermitId, SaveRefusal> startSave(SavePlan plan, bool overwriteExternalChange = false);
    // The WriteCoordinator's result listener must forward here.
    void onWriteResult(const WriteResult &result);
    std::optional<SaveStatus> lastSave(DocumentId document) const;

private:
    struct Entry {
        std::unique_ptr<EditSession> session;
        DestinationKey destination;
        std::vector<std::byte> knownBytes; // what this Document last saw at its destination
        std::uint64_t generation = 1;
        std::optional<SaveStatus> lastSave;
    };
    struct Pending {
        DocumentId document;
        std::uint64_t generation = 0;
        ContentId content;
        DestinationKey destination;
        std::vector<std::byte> bytes;
        bool saveAs = false;
    };
    std::expected<StagedOpen, OpenError> stage(const DestinationKey &destination);
    Entry *find(DocumentId document);

    FileReadPort &m_reader;
    WriteCoordinator &m_writes;
    std::map<DocumentId, Entry> m_documents;
    std::map<PermitId, Pending> m_pending;
    std::uint64_t m_nextDocument = 1;
};

} // namespace hikari::application
