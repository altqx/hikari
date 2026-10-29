#pragma once

// Staging never touches existing work; only an acknowledged write marks a snapshot saved.

#include "hikari/application/edit_session.h"
#include "hikari/application/write_coordinator.h"
#include "hikari/core/ass_save.h"
#include "hikari/core/lyrics_import.h"

#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

enum class ReadError { NotFound, AccessDenied, Failed };

// Reads a whole file, synchronously, on whichever thread stages.
class FileReadPort {
public:
    virtual ~FileReadPort() = default;
    virtual std::expected<std::vector<std::byte>, ReadError> read(const DestinationKey &destination) = 0;
};

enum class OpenError {
    NotFound,
    AccessDenied,
    ReadFailed,
    InvalidFormat, // no reader found a Line
    MalformedLyrics,
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
    InvalidDraft,   // the pending draft can't be committed
};

// For MalformedLyrics, `lyrics` says what is wrong and where.
struct StageFailure {
    OpenError reason = OpenError::ReadFailed;
    std::optional<core::LyricsProblem> lyrics;
};

// A prepared replacement. Nothing is published until activate().
struct StagedOpen {
    DestinationKey destination;
    core::LoadResult load;
    std::optional<DocumentId> replaces;
    std::uint64_t expectedGeneration = 0;
    std::uint64_t expectedRevision = 0;
    // Lyrics turned into ASS: opens Untitled with unsaved work.
    bool imported = false;
};

struct SavePlan {
    DocumentId document;
    std::uint64_t generation = 0;
    std::uint64_t revision = 0;
    ContentId content;
    DestinationKey destination;
    std::vector<std::byte> bytes;
    bool saveAs = false;
    // The "Active Line" the bytes hold; the Document takes it once they are written.
    std::optional<std::u8string> activeLine;
    // The [Hikari] "Layout" the bytes hold, taken the same way.
    std::optional<std::u8string> layout;
};

// Every ASS save writes it; the Document takes it after the write, with no undo step.
inline constexpr std::u8string_view kActiveLineKey = u8"Active Line";
// The active Line's 0-based row (0 with none) for an ASS Document; nullopt for
// other formats.
std::optional<std::u8string> activeLineToSave(const EditSession &session);
// `document` as a save writes it: with "Active Line" set to `row`.
core::Document withActiveLine(core::Document document, const std::optional<std::u8string> &row);

// [Hikari] key a save writes the panel arrangement into, taken like Active Line.
// A LayoutSource's nullopt leaves the section as it is.
inline constexpr std::u8string_view kLayoutKey = u8"Layout";
using LayoutSource = std::function<std::optional<std::u8string>(DocumentId)>;

struct SaveStatus {
    PermitId permit;
    std::optional<WriteOutcome> outcome; // empty while the write is running
};

class DocumentFiles {
public:
    DocumentFiles(FileReadPort &reader, WriteCoordinator &writes);

    std::expected<StagedOpen, StageFailure> stageOpen(const DestinationKey &destination);
    std::expected<StagedOpen, StageFailure> stageReload(DocumentId document);
    // Publishes a staged open once: a new Document, or the replacement of the
    // one it was staged for when that Document is still exactly as it was.
    std::expected<DocumentId, OpenError> activate(StagedOpen staged);

    // Untitled, with the default Script Info, Style and one empty Line; starts saved.
    DocumentId createNew();
    // Untitled, holding `document` with unsaved work.
    DocumentId createUnsaved(core::Document document);

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
    // The file as it is now becomes what the Document last saw there. False
    // without a destination or when the file can't be read.
    bool acceptExternalChange(DocumentId document);
    // The WriteCoordinator's result listener must forward here.
    void onWriteResult(const WriteResult &result);
    std::optional<SaveStatus> lastSave(DocumentId document) const;
    void setSaveOptions(const core::AssSaveOptions &options) { m_saveOptions = options; }
    const core::AssSaveOptions &saveOptions() const { return m_saveOptions; }
    void setLayoutSource(LayoutSource source) { m_layoutSource = std::move(source); }
    // Called as prepareSave starts, so a panel's pending edit reaches its Document.
    void setBeforeSave(std::function<void()> before) { m_beforeSave = std::move(before); }
    // An ASS Document's arrangement from the layout source; nullopt for other
    // formats or without a source.
    std::optional<std::u8string> layoutToSave(DocumentId document, const core::Document &saved) const;

private:
    struct Entry {
        std::unique_ptr<EditSession> session;
        DestinationKey destination;
        std::vector<std::byte> knownBytes;
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
        std::optional<std::u8string> activeLine;
        std::optional<std::u8string> layout;
    };
    std::expected<StagedOpen, StageFailure> stage(const DestinationKey &destination);
    Entry *find(DocumentId document);

    FileReadPort &m_reader;
    WriteCoordinator &m_writes;
    std::map<DocumentId, Entry> m_documents;
    std::map<PermitId, Pending> m_pending;
    std::uint64_t m_nextDocument = 1;
    core::AssSaveOptions m_saveOptions;
    LayoutSource m_layoutSource;
    std::function<void()> m_beforeSave;
};

}
