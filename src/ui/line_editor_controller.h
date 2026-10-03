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
    // Translation mode (Script Info "TLMode: Yes"): Original above Translated.
    Q_PROPERTY(bool translationMode READ translationMode NOTIFY changed)
    Q_PROPERTY(QString translationText READ translationText NOTIFY changed)
    Q_PROPERTY(QString startText READ startText NOTIFY changed)
    Q_PROPERTY(QString endText READ endText NOTIFY changed)
    Q_PROPERTY(QString marginLeftText READ marginLeftText NOTIFY changed)
    Q_PROPERTY(QString marginRightText READ marginRightText NOTIFY changed)
    Q_PROPERTY(QString marginVerticalText READ marginVerticalText NOTIFY changed)
    Q_PROPERTY(QString problem READ problem NOTIFY changed)
    Q_PROPERTY(QString attempted READ attempted NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(QString saveStatus READ saveStatus NOTIFY changed)
    // Where the text field's selection should be after a command.
    Q_PROPERTY(int selectionStart READ selectionStart NOTIFY selectionRequested)
    Q_PROPERTY(int selectionEnd READ selectionEnd NOTIFY selectionRequested)
    // The field a requested selection belongs to (0 Original, 1 Translated).
    Q_PROPERTY(int selectionRole READ selectionRole NOTIFY selectionRequested)

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
    QString text() const { return m_shown[0]; }
    bool translationMode() const;
    QString translationText() const { return m_shown[1]; }
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
    Q_INVOKABLE void translationEdited(const QString &newText, int cursor);
    Q_INVOKABLE void setStartText(const QString &text);
    Q_INVOKABLE void setEndText(const QString &text);
    Q_INVOKABLE void setMarginText(int which, const QString &text); // 0 left, 1 right, 2 vertical
    Q_INVOKABLE bool commitAndAdvance(); // Enter, outside composition
    Q_INVOKABLE bool commit();
    Q_INVOKABLE void discard();          // Esc
    Q_INVOKABLE bool undo();             // draft history first, then the Document
    Q_INVOKABLE bool redo();
    Q_INVOKABLE bool save();
    // Legacy Bold/Italic/Underline/Strikeout ('b', 'i', 'u', 's') on the
    // text field's selection, with the Style's value deciding the direction.
    Q_INVOKABLE bool toggleTag(const QString &tag, int selectionStart, int selectionEnd);
    // The same in a given role: 0 Original, 1 Translated.
    Q_INVOKABLE bool toggleTagIn(int role, const QString &tag, int selectionStart, int selectionEnd);
    // EDITBOX_SPLIT_LINE (Shift+Enter): a hard break replaces the selection,
    // taking an adjacent space on each side.
    Q_INVOKABLE bool splitLine(int role, int selectionStart, int selectionEnd);
    // Translation mode: EDITBOX_SET_DOUBTFUL (Alt+Down) toggles Unconfirmed and
    // goes to the next Line; Ctrl+D / Ctrl+R find the next unconfirmed or
    // untranslated visible Line, wrapping once.
    Q_INVOKABLE bool toggleUnconfirmedAndAdvance();
    Q_INVOKABLE bool findNextUnconfirmed();
    Q_INVOKABLE bool findNextUntranslated();
    int selectionStart() const { return m_selectionStart; }
    int selectionEnd() const { return m_selectionEnd; }
    int selectionRole() const { return m_selectionRole; }

    // The WriteCoordinator listener calls this after DocumentFiles has the result.
    void writeFinished();

signals:
    void changed();
    void lineChanged(qulonglong id); // the active Line moved (keeps the Grid in step)
    void selectionRequested();

private:
    application::EditSession *session() const;
    std::optional<core::LineRecord> record() const; // the active Line, draft applied
    void refresh();
    void fail(const QString &problem, const QString &attempted = {});
    bool setRaw(int role, std::u8string raw);
    void edit(int role, const QString &newText, int cursor);
    // After Undo or Redo on the same Line: the caret goes to the end of what
    // changed, in the field that changed (as in a text editor's own undo).
    void placeCaretAfterChange(const QString (&before)[2]);
    const std::u8string &roleText(const core::LineRecord &line, int role) const
    {
        return role == 0 ? line.text : line.translation;
    }
    void committed();
    QString problemText() const;

    application::DocumentFiles &m_files;
    std::optional<application::DocumentId> m_document;
    bool m_editable = false;
    bool m_showTags = false; // tags hidden by default
    QString m_shown[2];      // what the Original and Translated fields show
    QString m_problem;
    QString m_attempted;
    // Exact raw snapshots of this draft, both roles.
    struct Snapshot {
        std::u8string text, translation;
    };
    std::vector<Snapshot> m_draftUndo;
    std::vector<Snapshot> m_draftRedo;
    std::function<void()> m_onCommitted;
    bool findNext(std::size_t &cursor, const std::function<bool(const core::LineRecord &)> &match,
                  const QString &none);
    std::size_t m_nextUnconfirmed = 0; // legacy CurrentDoubtful
    std::size_t m_nextUntranslated = 0; // legacy CurrentUntranslated
    int m_selectionStart = 0;
    int m_selectionEnd = 0;
    int m_selectionRole = 0;
};

} // namespace hikari::ui
