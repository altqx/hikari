#pragma once

#include "hikari/application/edit_session.h"
#include "hikari/application/write_coordinator.h"
#include "hikari/core/document.h"

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

// The changes AI callers applied through the Hikari API, kept per Document
// until the user keeps or undoes them (ADR 0024, AI2 #430). Each change is one
// history step; each turn begins with a checkpoint.
class AiReview {
public:
    enum class ItemKind { Line, Style, Info };

    // One Line, Style or Script Info property a change touched.
    struct Item {
        ItemKind kind = ItemKind::Line;
        core::LineId line;
        // Absent before: the change inserted it. Absent after: it deleted it.
        std::optional<core::LineRecord> before, after;
        // Where the Line stood before: the Line it preceded, nullopt at the end.
        std::optional<core::LineId> beforeNext;
        std::size_t beforeRow = 0;
        bool moved = false;
        std::u8string key; // the Style's name or the Script Info key
        std::optional<std::vector<std::u8string>> styleBefore, styleAfter;
        std::optional<std::u8string> infoBefore, infoAfter;
        bool settled = false; // kept or undone
        bool undone = false;
    };

    struct Change {
        std::uint64_t id = 0;
        std::uint64_t turn = 0;
        DocumentId document;
        std::string caller; // the caller's label, "AI" or an agent's name
        std::string name;
        ContentId step;
        std::vector<Item> items;
    };

    struct Turn {
        std::uint64_t id = 0;
        std::uint64_t number = 0; // 1-based, in the order turns began
        std::string caller;       // the caller's id
        std::string label;        // the user's message, or the caller's label
    };

    // A change still waiting: its item differs from what it was before the change.
    struct Pending {
        std::uint64_t change = 0;
        std::size_t item = 0;
        bool editedSince = false; // the user changed it after the AI did
    };

    // A turn per user message; a write with no open turn for its caller opens one.
    std::uint64_t beginTurn(const std::string &caller, std::string label);

    // `previous` is the history step before the change, `before` its Document.
    void record(DocumentId document, const std::string &callerId, const std::string &callerLabel, std::string name,
                const core::Document &before, ContentId previous, const EditSession &session);

    const std::vector<Turn> &turns() const { return m_turns; }
    const Turn *turn(std::uint64_t id) const;
    const Change *change(std::uint64_t id) const;
    // Every change on the Document, oldest first, settled ones too.
    std::vector<const Change *> changes(DocumentId document) const;

    // What still waits on the Document as it is now, oldest change first.
    std::vector<Pending> pending(DocumentId document, const core::Document &current) const;
    // The Lines those items name, each once.
    std::size_t pendingLines(DocumentId document, const core::Document &current) const;
    // The turn whose first change on the Document made `step`: its checkpoint comes before it.
    std::optional<std::uint64_t> turnStartingAt(DocumentId document, ContentId step) const;
    bool isAiStep(DocumentId document, ContentId step) const;

    // No change (0) means every change on the Document; no item, the whole change;
    // a Line, only the items on it.
    struct Target {
        std::uint64_t change = 0;
        std::optional<std::size_t> item;
        std::optional<core::LineId> line;
    };
    void keep(DocumentId document, const core::Document &current, Target target);
    // The waiting items in `target` the user changed after the AI did.
    std::vector<Pending> editedSince(DocumentId document, const core::Document &current, Target target) const;
    // One history step. Items the user changed since stay unless `force`.
    // False: nothing was waiting.
    std::expected<bool, CommandRefusal> undo(DocumentId document, EditSession &session, Target target, bool force);

    // Restoring a checkpoint undoes every change from that turn on, kept ones
    // too, with one history step per Document.
    struct CheckpointDocument {
        DocumentId document;
        std::size_t lines = 0;
        std::size_t others = 0; // Styles and Script Info properties
        std::vector<core::LineId> editedSince;
    };
    using Sessions = std::function<EditSession *(DocumentId)>;
    std::vector<CheckpointDocument> checkpoint(std::uint64_t turn, const Sessions &sessions) const;
    std::expected<std::size_t, CommandRefusal> restore(std::uint64_t turn, const Sessions &sessions, bool force);

    // The Documents with a change, in no particular order.
    std::vector<DocumentId> documents() const;
    // The Document closed: its changes go.
    void forget(DocumentId document);

    // A Project file an AI caller wrote. It waits here, not on the disk, until the user keeps it.
    struct FileChange {
        std::uint64_t id = 0;
        std::uint64_t turn = 0;
        std::string caller;                // the caller's label
        std::string path;                  // absolute, UTF-8
        std::string shown;                 // relative to the Project, '/'-separated
        std::optional<std::string> before; // the file on disk when first staged; absent: a new file
        std::string after;
        std::uint64_t replacedBy = 0;      // a later write to the same path in this review
        bool settled = false;              // kept or undone
        bool undone = false;
    };
    // Whole-file reads and writes for Keep and Restore checkpoint.
    struct Disk {
        std::function<std::optional<std::string>(const std::string &path)> read;
        std::function<bool(const std::string &path, std::string_view bytes)> write;
        std::function<bool(const std::string &path)> remove;
    };

    std::uint64_t stageFile(const std::string &callerId, const std::string &callerLabel, std::string path,
                            std::string shown, std::optional<std::string> before, std::string after);
    const FileChange *file(std::uint64_t id) const;
    // The staged files still waiting, oldest first: one per path.
    std::vector<const FileChange *> pendingFiles() const;
    // What a read of `path` sees while a write to it waits.
    const std::string *staged(const std::string &path) const;
    // The file on disk is no longer what it was when the AI first staged it.
    bool changedOnDisk(const FileChange &change, const Disk &disk) const;

    enum class KeepFile { Kept, ChangedOnDisk, WriteFailed, NotWaiting };
    // Unless `force`, a file changed on disk since is left waiting.
    KeepFile keepFile(std::uint64_t id, const Disk &disk, bool force);
    // Drops the staged copy; the disk is never touched.
    bool undoFile(std::uint64_t id);
    // Every waiting file; answers the paths that changed on disk and stayed (without `force`) or failed.
    std::vector<std::string> keepAllFiles(const Disk &disk, bool force);
    void undoAllFiles();

    // What restoring the checkpoint before `turn` does to files: staged ones from that turn on go, and kept ones
    // get their content from before the turn back.
    struct CheckpointFiles {
        std::size_t staged = 0;
        std::size_t kept = 0;
        std::vector<std::string> changedSince; // kept files that changed on disk after they were kept
    };
    CheckpointFiles checkpointFiles(std::uint64_t turn, const Disk &disk) const;
    // Kept files that changed since stay unless `force`. Answers the paths it couldn't write back.
    std::vector<std::string> restoreFiles(std::uint64_t turn, const Disk &disk, bool force);

    // Runs after a file is staged, kept, undone or restored.
    std::function<void()> filesChanged;

private:
    struct Open {
        std::uint64_t turn = 0;
        std::vector<std::pair<DocumentId, ContentId>> last; // the turn's newest step per Document
    };

    Change *find(std::uint64_t id);
    FileChange *findFile(std::uint64_t id);
    std::uint64_t openTurn(const std::string &callerId, const std::string &callerLabel);
    std::vector<std::pair<Change *, std::size_t>> waiting(DocumentId document, const core::Document &current,
                                                          Target target);

    std::vector<Turn> m_turns;
    std::vector<Change> m_changes;
    std::vector<FileChange> m_files;
    std::vector<std::pair<std::string, Open>> m_open; // by caller id
    std::uint64_t m_nextId = 1;
};

}
