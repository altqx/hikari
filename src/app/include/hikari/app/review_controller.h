#pragma once

#include "hikari/application/ai_review.h"
#include "hikari/application/document_files.h"
#include "hikari/application/workspace.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <absl/container/flat_hash_map.h>

#include <functional>
#include <map>
#include <optional>

namespace hikari::app {

// The Review panel, the line editor's review strip and the Grid's review
// marks over the AI changes in AiReview (AI2 #430).
/// The word diff as rich text: removed words struck in the danger colour, added ones on a success tint.
QString diffHtml(const QString &before, const QString &after);

class ReviewController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool on READ on NOTIFY changed FINAL)
    // Lines with a waiting change in the editing target, and the staged Project files.
    Q_PROPERTY(int waiting READ waiting NOTIFY changed FINAL)
    // The staged Project files' paths relative to their Project, oldest first.
    Q_PROPERTY(QStringList waitingFiles READ waitingFiles NOTIFY changed FINAL)
    // Newest turn first: {turn, number, label, restorable, changes: [{change, caller, name, document, title,
    // target, waiting, items: [{item, title, detail, waiting, editedSince}]}], files: [{file, caller, path, isNew,
    // detail, changedOnDisk}]}.
    Q_PROPERTY(QVariantList turns READ turns NOTIFY changed FINAL)
    // The active Line's waiting change, empty when none: {caller, name, index, count, editedSince, inserted,
    // before (rich text)}.
    Q_PROPERTY(QVariantMap current READ current NOTIFY currentChanged FINAL)

public:
    struct Wiring {
        application::AiReview &review;
        application::DocumentFiles &files;
        application::Workspace &workspace;
        std::function<bool()> aiOn;
        std::function<bool(application::Selection)> selectInTarget;
        std::function<void(application::DocumentId)> committed;
        std::function<void(application::CommandRefusal)> refused;
        std::function<void()> refresh;
        application::AiReview::Disk disk;
    };
    struct Mark {
        bool editedSince = false;
        std::optional<core::LineRecord> before; // before the oldest waiting change
        std::uint64_t newest = 0;               // the newest waiting change
    };

    explicit ReviewController(Wiring wiring, QObject *parent = nullptr);

    bool on() const { return m_wiring.aiOn(); }
    int waiting() const;
    QStringList waitingFiles() const;
    QVariantList turns() const;
    QVariantMap current() const;

    // Rebuilds the marks after the Documents or the selection changed.
    void refreshed();
    const Mark *mark(application::DocumentId document, core::LineId line) const;
    int waitingIn(application::DocumentId document) const;
    /// "<child>: <count>" for the sub-agents' Lines still to review in a Document.
    QString waitingByChild(application::DocumentId document) const;

    // change 0: every change in the target; item -1: the whole change.
    Q_INVOKABLE void keep(qulonglong change, int item);
    Q_INVOKABLE int editedSince(qulonglong change, int item) const;
    Q_INVOKABLE bool undo(qulonglong change, int item, bool force);
    Q_INVOKABLE void keepLine();
    Q_INVOKABLE int lineEditedSince() const;
    Q_INVOKABLE bool undoLine(bool force);
    Q_INVOKABLE bool step(int direction);
    Q_INVOKABLE void show(qulonglong change, int item);
    // [{title, lines, others, editedSince}] per Document the restore would change, then {files, filesChangedSince}
    // for the Project files when it changes any.
    Q_INVOKABLE QVariantList checkpoint(qulonglong turn) const;
    Q_INVOKABLE bool restore(qulonglong turn, bool force, bool forceFiles = false);
    // 0: kept; 1: the file changed on disk since, so it waits unless `force`; 2: it could not be written.
    Q_INVOKABLE int keepFile(qulonglong file, bool force);
    Q_INVOKABLE void undoFile(qulonglong file);
    // {text, lines: [{kind: "same"|"removed"|"added"|"gap", text}]}; text false: only the sizes are known.
    Q_INVOKABLE QVariantMap fileDiff(qulonglong file) const;
    // The quit question: false when some files stayed waiting, because they changed on disk or couldn't be written.
    Q_INVOKABLE bool keepAllFiles();
    Q_INVOKABLE void discardAllFiles();
    Q_INVOKABLE bool isAiStep(int step) const;
    Q_INVOKABLE int turnBefore(int step) const;

signals:
    void changed();
    void currentChanged();

private:
    using Target = application::AiReview::Target;
    std::optional<application::DocumentId> target() const { return m_wiring.workspace.editingTarget(); }
    application::EditSession *targetSession() const;
    std::optional<core::LineId> activeLine() const;
    std::vector<core::LineId> waitingLinesInRowOrder() const;
    bool undoIn(application::DocumentId document, Target target, bool force);

    Wiring m_wiring;
    absl::flat_hash_map<std::uint64_t, absl::flat_hash_map<std::uint64_t, Mark>> m_marks;
    absl::flat_hash_map<std::uint64_t, int> m_waiting;
    absl::flat_hash_map<std::uint64_t, std::vector<std::pair<std::string, int>>> m_byChild;
    std::map<std::pair<std::uint64_t, std::size_t>, bool> m_pending; // (change, item) -> edited since
};

}
