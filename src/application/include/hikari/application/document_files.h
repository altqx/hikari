#pragma once

// Open, save and reopen (A2; docs/proposals/application-lifecycle.md).
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
    MalformedLyrics, // KA6: a lyrics file that can't be imported
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

// Why staging failed; for MalformedLyrics, what is wrong and where.
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
    // KA6: lyrics turned into an ASS Document. It opens Untitled with
    // unsaved work, so the lyrics file is never overwritten with ASS.
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
    // S4: the "Active Line" the bytes hold, which the Document takes once
    // they are written (activeLineToSave).
    std::optional<std::u8string> activeLine;
    // WS2: the [Hikari] "Layout" the bytes hold, taken the same way.
    std::optional<std::u8string> layout;
};

// S4: legacy SubsGrid::SaveFile, autosave included, wrote the active Line's
// row into the open Document's Script Info before writing the header
// (GetAssHeader with forFile, `file->AddSInfo(L"Active Line",
// std::to_wstring(currentLine), false)`, HikariSub/SubsGridBase.cpp:1470 at
// 20d647c4): the key's value replaced in place, or the key added after the
// last property; no undo step, nothing marked modified. Macros run after a
// save see it, and the next load starts on that Line. Legacy restores the
// Script Info when the write fails (SubsGridBase.cpp:433 on main), so here
// the Document takes it only once the write succeeded.
inline constexpr std::u8string_view kActiveLineKey = u8"Active Line";
// The value a save of this session writes: the active Line's 0-based row
// among all Lines (legacy currentLine, 0 with none), for an ASS Document;
// nullopt for the other formats, which have no Script Info (legacy wrote the
// header only for subsFormat < SRT).
std::optional<std::u8string> activeLineToSave(const EditSession &session);
// `document` as a save writes it: with "Active Line" set to `row`.
core::Document withActiveLine(core::Document document, const std::optional<std::u8string> &row);

// WS2: a save of an ASS Document writes its Document tab's panel arrangement
// into the [Hikari] section ("Layout="), like the active Line: changing the
// arrangement marks nothing modified, the next save writes it, and the
// Document takes it once the write succeeded. The application gives the
// value (nullopt: the section is left as it is, the setting being off or the
// Document having no tab).
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

    // A new Untitled Document (P1; legacy SubsGrid::LoadDefault): the default
    // Script Info, the Default Style and one empty Line, with no destination,
    // so its first save needs Save As. It starts saved: an untouched new
    // Document never asks to be saved.
    DocumentId createNew();
    // An Untitled Document holding `document` with unsaved work (P3: a
    // recovered copy, L58-recovery-copy).
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
    // PU1-disk-change-bar: the user kept this copy over the file another
    // program changed (Keep mine, Save over it). The file as it is now
    // becomes what the Document last saw there, so its next save writes over
    // that change, and a later one is refused again. False without a
    // destination or when the file can't be read.
    bool acceptExternalChange(DocumentId document);
    // The WriteCoordinator's result listener must forward here.
    void onWriteResult(const WriteResult &result);
    std::optional<SaveStatus> lastSave(DocumentId document) const;
    // E5: the ASS save options prepareSave encodes with (legacy SaveFile reads
    // TL_MODE_HIDE_ORIGINAL_ON_VIDEO at every save).
    void setSaveOptions(const core::AssSaveOptions &options) { m_saveOptions = options; }
    const core::AssSaveOptions &saveOptions() const { return m_saveOptions; }
    // WS2: where prepareSave takes the panel arrangement an ASS save writes.
    void setLayoutSource(LayoutSource source) { m_layoutSource = std::move(source); }
    // Called as prepareSave starts: a panel's edit still pending goes into
    // its Document first (KE2's Template editor), as the Line editor's draft
    // does.
    void setBeforeSave(std::function<void()> before) { m_beforeSave = std::move(before); }
    // The arrangement a save of the Document writes now: an ASS Document's,
    // from the layout source; none for other formats or without a source.
    std::optional<std::u8string> layoutToSave(DocumentId document, const core::Document &saved) const;

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

} // namespace hikari::application
