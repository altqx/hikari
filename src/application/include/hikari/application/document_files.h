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
#include <span>
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
    MalformedSrv3,
    UnknownDocument,
    StaleTarget,   // the Document changed or was replaced after staging
    NotInEncoding, // the file is not text in the encoding it was reopened with
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

// For MalformedLyrics, `lyrics` says what is wrong and where; for MalformedSrv3, `srv3` is YTSubConverter's reason.
struct StageFailure {
    OpenError reason = OpenError::ReadFailed;
    std::optional<core::LyricsProblem> lyrics;
    std::optional<std::string> srv3;
};

// A prepared replacement. Nothing is published until activate().
struct StagedOpen {
    DestinationKey destination;
    core::LoadResult load;
    std::optional<DocumentId> replaces;
    std::uint64_t expectedGeneration = 0;
    std::uint64_t expectedRevision = 0;
    // Lyrics or YouTube timed text turned into ASS: opens Untitled with unsaved work.
    bool imported = false;
    // The file as read, when the Document was built from it rather than parsed (a plugin's format).
    std::optional<std::vector<std::byte>> fileBytes;
    // Built from the file converted to UTF-8: it starts modified, so its next save writes UTF-8.
    bool converted = false;
};

// Gets a save's bytes later: nullopt when there are none, after the user was told why.
using BytesReady = std::function<void(std::optional<std::vector<std::byte>>)>;
using DeferredBytes = std::function<void(BytesReady)>;
// A save whose bytes something other than HikariSub's own writers make; nullopt: HikariSub writes them.
using DeferredEncoder = std::function<std::optional<DeferredBytes>(DocumentId, const DestinationKey &)>;

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
    // Set: `bytes` are unused, and startSave writes what this gives once it has the permit.
    std::optional<DeferredBytes> deferred;
};

// Every ASS save writes it; the Document takes it after the write, with no undo step.
inline constexpr std::u8string_view kActiveLineKey = u8"Active Line";
// The active Line's 0-based row (0 with none) for an ASS Document; nullopt for
// other formats.
std::optional<std::u8string> activeLineToSave(const EditSession &session);

// An Untitled Document's file: the default Script Info, Style and one empty Line.
std::string_view defaultScript();
// `document` as a save writes it: with "Active Line" set to `row`.
core::Document withActiveLine(core::Document document, const std::optional<std::u8string> &row);
// After a save is written: the row goes into the session's Script Info and Project Garbage, without an undo step.
void amendActiveLine(EditSession &session, const std::u8string &row);

// [Hikari] key a save writes the panel arrangement into, taken like Active Line.
// A LayoutSource's nullopt leaves the section as it is.
inline constexpr std::u8string_view kLayoutKey = u8"Layout";
using LayoutSource = std::function<std::optional<std::u8string>(DocumentId)>;

// An [Aegisub Project Garbage] key a save sets to `value`, or removes when it is nullopt.
struct ProjectGarbageEdit {
    std::u8string key;
    std::optional<std::u8string> value;
    bool operator==(const ProjectGarbageEdit &) const = default;
};
// The edits for a save of the Document to the destination (empty for an Untitled autosave).
using ProjectGarbageSource = std::function<std::vector<ProjectGarbageEdit>(DocumentId, const DestinationKey &)>;

// As Aegisub 3.2 writes a double (std::to_string): six decimals.
std::u8string aegisubDecimal(double value);
// Video AR Mode 4 and Video AR Value for a picture shown at `shown` (width / height); both removed at the video's
// own ratio.
std::vector<ProjectGarbageEdit> aspectGarbage(double shown, double own);

struct SaveStatus {
    PermitId permit;
    std::optional<WriteOutcome> outcome; // empty while the write is running
};

// A lyrics file's bytes as UTF-8, or nullopt when they are not text.
using LyricsDecoder = std::function<std::optional<std::vector<std::byte>>(std::span<const std::byte>)>;
// A file's bytes as UTF-8, or nullopt when they are not text in the encoding it reads.
using ToUtf8 = std::function<std::optional<std::vector<std::byte>>(std::span<const std::byte>)>;
// What a published save replaced at `destination`: the file as it was just before the write.
using SavedOver = std::function<void(const DestinationKey &destination, std::vector<std::byte> previous)>;

class DocumentFiles {
public:
    DocumentFiles(FileReadPort &reader, WriteCoordinator &writes);

    // Without one, lyrics files must be UTF-8.
    void setLyricsDecoder(LyricsDecoder decoder) { m_lyricsDecoder = std::move(decoder); }
    // Told once per published save that replaced a file with different bytes.
    void setSavedOver(SavedOver savedOver) { m_savedOver = std::move(savedOver); }

    std::expected<StagedOpen, StageFailure> stageOpen(const DestinationKey &destination);
    // With `toUtf8`, the file is read through it and the replacement is converted.
    std::expected<StagedOpen, StageFailure> stageReload(DocumentId document, const ToUtf8 &toUtf8 = {});
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
    void setProjectGarbageSource(ProjectGarbageSource source) { m_projectGarbageSource = std::move(source); }
    // The version an ASS save names in its "Script generated by HikariSub v…" line; empty writes none.
    void setGeneratorVersion(std::u8string version) { m_generatorVersion = std::move(version); }
    // Called as prepareSave starts, so a panel's pending edit reaches its Document.
    void setBeforeSave(std::function<void(DocumentId)> before) { m_beforeSave = std::move(before); }
    void setDeferredEncoder(DeferredEncoder encoder) { m_deferredEncoder = std::move(encoder); }
    // An ASS Document's arrangement from the layout source; nullopt for other
    // formats or without a source.
    std::optional<std::u8string> layoutToSave(DocumentId document, const core::Document &saved) const;
    // Applies the project garbage source's edits and the Active Line to an ASS Document saved to `destination`.
    // Keys the file lacks are added in Aegisub 3.2's order.
    void applyProjectGarbage(DocumentId document, const DestinationKey &destination, core::Document &saved,
                             const std::optional<std::u8string> &activeLine) const;

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
        std::optional<std::vector<std::byte>> previous;
    };
    std::expected<StagedOpen, StageFailure> stage(const DestinationKey &destination, const ToUtf8 &toUtf8 = {});
    Entry *find(DocumentId document);
    void writeDeferred(PermitId permit, std::optional<std::vector<std::byte>> bytes);

    FileReadPort &m_reader;
    LyricsDecoder m_lyricsDecoder;
    SavedOver m_savedOver;
    WriteCoordinator &m_writes;
    std::map<DocumentId, Entry> m_documents;
    std::map<PermitId, Pending> m_pending;
    std::uint64_t m_nextDocument = 1;
    core::AssSaveOptions m_saveOptions;
    LayoutSource m_layoutSource;
    ProjectGarbageSource m_projectGarbageSource;
    std::u8string m_generatorVersion;
    std::function<void(DocumentId)> m_beforeSave;
    DeferredEncoder m_deferredEncoder;
};

}
