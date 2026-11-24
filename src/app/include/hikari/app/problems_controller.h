#pragma once

#include "hikari/application/edit_session.h"
#include "hikari/application/problems.h"
#include "hikari/app/qc_notes_controller.h"
#include "line_table_model.h"

#include <QObject>
#include <QStringList>
#include <QVariantList>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace hikari::app {

// One Grid mark per Line with plugins' problems, in its worst severity's colour.
void addProblemMarks(ui::LineTableModel::RowMarks &marks, const application::Problems &problems,
                     application::DocumentId id, const core::Document &document);

// The Problems list: the editing target's QC notes beside the problems plugins set on it.
class ProblemsController : public QObject {
    Q_OBJECT
    // By Line in Document order, notes before problems, "No line" last; filtered by source and severity.
    // Every entry has {key, kind ("note" or "problem"), group, row, severity}; a note adds the fields of
    // QcNotesController::notes, a problem {id, message, source, sourceName, field, code, fixes (titles)}.
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    // {id, name, count}: "qc" for the QC notes, then each plugin source.
    Q_PROPERTY(QVariantList sources READ sources NOTIFY changed)
    // {id, name, count} of each severity present, worst first; QC notes count as info.
    Q_PROPERTY(QVariantList severities READ severities NOTIFY changed)
    // "" for every source or severity.
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY changed)
    Q_PROPERTY(QString severity READ severity WRITE setSeverity NOTIFY changed)
    Q_PROPERTY(int problemCount READ problemCount NOTIFY changed)
public:
    struct Hooks {
        std::function<std::optional<application::DocumentId>()> document;
        std::function<application::EditSession *()> session;
        std::function<void(core::LineId)> selectLine;
        std::function<bool(int ms)> seek;
        std::function<bool(application::DocumentId, const std::string &problem, int fix)> fix;
        std::function<std::optional<std::u16string>(int role)> editorRaw; // the Line editor's Line, draft included
        std::function<std::pair<int, int>(int role, int from, int to)> displaySpan;
    };
    ProblemsController(application::Problems &problems, QcNotesController &notes, Hooks hooks, QObject *parent = nullptr);

    QVariantList entries() const { return m_entries; }
    QVariantList sources() const { return m_sources; }
    QVariantList severities() const { return m_severities; }
    QString source() const { return m_source; }
    void setSource(const QString &source);
    QString severity() const { return m_severity; }
    void setSeverity(const QString &severity);
    int problemCount() const { return static_cast<int>(m_held.size()); }

    // Selects the problem's Line and moves the video to its start.
    Q_INVOKABLE bool goTo(const QString &id);
    Q_INVOKABLE QStringList fixesOf(const QString &id) const;
    Q_INVOKABLE bool applyFix(const QString &id, int fix);
    // The Line editor Line's ranged problems in `role`'s field: {from, to, severity, message, source} in shown offsets.
    Q_INVOKABLE QVariantList editorMarks(int role) const;
    // {problem, fix, title, source}: the fixes of the field's problems at `position`, then of its whole-Line ones.
    Q_INVOKABLE QVariantList editorFixesAt(int role, int position) const;

    void documentEdited();
    void problemsChanged(application::DocumentId document);

signals:
    void changed();

private:
    struct Held {
        application::HeldProblem held;
        int row = 0;
    };
    void read();
    void rebuild();
    const Held *find(const QString &id) const;

    application::Problems &m_problems;
    QcNotesController &m_notes;
    Hooks m_hooks;
    std::vector<Held> m_held;
    QVariantList m_entries;
    QVariantList m_sources;
    QVariantList m_severities;
    QString m_source;
    QString m_severity;
    std::optional<application::DocumentId> m_readDocument;
    std::uint64_t m_readRevision = 0;
    bool m_read = false;
};

}
