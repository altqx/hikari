#pragma once

// Presenter for the Line editor (V2; docs/qt/ux/ass-editor.md). One source of
// truth: the editing target's EditSession holds the draft and history; this
// class only projects the active Line into editor text and fields and turns
// user edits into draft changes.
//
// With tags hidden, the text field shows the hidden-tag projection. Each user
// change is located from the old and new text plus the caret, mapped back
// onto the raw source (after-tag insertion, kept intervening tags), and
// stored as the draft's raw text. A refused mapping leaves the raw text
// unchanged and reports the attempted text, so nothing typed is lost.

#include "hikari/application/document_files.h"

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <vector>

namespace hikari::ui {

class LineEditorController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application composition")
    Q_PROPERTY(bool hasLine READ hasLine NOTIFY changed)
    Q_PROPERTY(bool editable READ editable NOTIFY changed)
    Q_PROPERTY(bool showTags READ showTags WRITE setShowTags NOTIFY changed)
    Q_PROPERTY(QString text READ text NOTIFY changed)
    Q_PROPERTY(QString startText READ startText NOTIFY changed)
    Q_PROPERTY(QString endText READ endText NOTIFY changed)
    Q_PROPERTY(QString marginLeftText READ marginLeftText NOTIFY changed)
    Q_PROPERTY(QString marginRightText READ marginRightText NOTIFY changed)
    Q_PROPERTY(QString marginVerticalText READ marginVerticalText NOTIFY changed)
    Q_PROPERTY(QString problem READ problem NOTIFY changed)
    Q_PROPERTY(QString attempted READ attempted NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(QString saveStatus READ saveStatus NOTIFY changed)

public:
    explicit LineEditorController(application::DocumentFiles &files, QObject *parent = nullptr);

    // The Document being edited (the workspace's editing target), or none.
    // `editable` is false for a protected reference.
    void setDocument(std::optional<application::DocumentId> document, bool editable);
    // Called after the session's committed content changes (for the Grid).
    void setCommittedListener(std::function<void()> listener) { m_onCommitted = std::move(listener); }

    bool hasLine() const;
    bool editable() const { return m_editable && hasLine(); }
    bool showTags() const { return m_showTags; }
    void setShowTags(bool show);
    QString text() const { return m_text; }
    QString startText() const;
    QString endText() const;
    QString marginLeftText() const;
    QString marginRightText() const;
    QString marginVerticalText() const;
    QString problem() const { return m_problem; }
    QString attempted() const { return m_attempted; }
    bool dirty() const;
    QString saveStatus() const;

    // The Grid or navigation asked for a Line; commits on leave.
    Q_INVOKABLE bool showLine(qulonglong id);
    // The text field now holds `newText` with the caret at `cursor`.
    Q_INVOKABLE void textEdited(const QString &newText, int cursor);
    Q_INVOKABLE void setStartText(const QString &text);
    Q_INVOKABLE void setEndText(const QString &text);
    Q_INVOKABLE void setMarginText(int which, const QString &text); // 0 left, 1 right, 2 vertical
    Q_INVOKABLE bool commitAndAdvance(); // Enter, outside composition
    Q_INVOKABLE bool commit();
    Q_INVOKABLE void discard();          // Esc
    Q_INVOKABLE bool undo();             // draft history first, then the Document
    Q_INVOKABLE bool redo();
    Q_INVOKABLE bool save();

    // The WriteCoordinator listener calls this after DocumentFiles has the result.
    void writeFinished();

signals:
    void changed();
    void lineChanged(qulonglong id); // the active Line moved (keeps the Grid in step)

private:
    application::EditSession *session() const;
    std::optional<core::LineRecord> record() const; // the active Line, draft applied
    void refresh();
    void fail(const QString &problem, const QString &attempted = {});
    bool setRaw(std::u8string raw);
    void committed();
    QString problemText() const;

    application::DocumentFiles &m_files;
    std::optional<application::DocumentId> m_document;
    bool m_editable = false;
    bool m_showTags = false; // tags hidden by default
    QString m_text;          // what the text field shows
    QString m_problem;
    QString m_attempted;
    std::vector<std::u8string> m_draftUndo; // exact raw snapshots of this draft
    std::vector<std::u8string> m_draftRedo;
    std::function<void()> m_onCommitted;
};

} // namespace hikari::ui
