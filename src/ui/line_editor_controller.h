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
#include "hikari/core/editor_font_colour.h"
#include "hikari/core/style.h"
#include "hikari/core/tag_commands.h"

#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <optional>
#include <string>
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
    // History (G10): one entry per step, "<name>, active line N" as legacy lists them.
    Q_PROPERTY(QStringList history READ history NOTIFY changed)
    Q_PROPERTY(int historyCursor READ historyCursor NOTIFY changed)
    Q_PROPERTY(bool canUndoToLastSave READ canUndoToLastSave NOTIFY changed)
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
    // Jumps to a History step (draft committed first); redo steps stay.
    Q_INVOKABLE bool goToHistory(int step);
    // Legacy GLOBAL_UNDO_TO_LAST_SAVE: back (or forward) to the saved step.
    Q_INVOKABLE bool undoToLastSave();
    QStringList history() const;
    int historyCursor() const;
    bool canUndoToLastSave() const;
    Q_INVOKABLE bool save();
    // Legacy Bold/Italic/Underline/Strikeout ('b', 'i', 'u', 's') on the
    // text field's selection, with the Style's value deciding the direction.
    Q_INVOKABLE bool toggleTag(const QString &tag, int selectionStart, int selectionEnd);
    // The same in a given role: 0 Original, 1 Translated.
    Q_INVOKABLE bool toggleTagIn(int role, const QString &tag, int selectionStart, int selectionEnd);
    // EDITBOX_SPLIT_LINE (Shift+Enter): a hard break replaces the selection,
    // taking an adjacent space on each side.
    Q_INVOKABLE bool splitLine(int role, int selectionStart, int selectionEnd);
    // E2: a custom tag button (EditBox::OnButtonTag). Types 0 (at the caret)
    // and 1 (at the text start) put the override tag into the field `role`
    // with its legacy reset; type 2 inserts plain text there, or with several
    // Lines selected into every selected Line at the caret as one step.
    Q_INVOKABLE bool applyTagButton(int role, const QString &tag, int type, int selectionStart, int selectionEnd);
    // E1: the font dialog and the colour picker (EDITBOX_CHANGE_FONT,
    // EDITBOX_CHANGE_COLOR_*). begin* reads the font or colour in effect at
    // the field's selection (legacy OnFontClick/GetColor) and opens a dialog
    // session; each change* applies the dialog's new value at once, as the
    // legacy dialogs do; endDialog(true) puts the caret after the block,
    // endDialog(false) takes every change back. With several Lines selected
    // each change is an "Editing multiple lines" step on all of them.
    // Fonts are {name, size, bold, italic, underline, strikeOut}; colours
    // {r, g, b, a} with a the ASS alpha; `number` 1 primary, 2 secondary,
    // 3 outline, 4 shadow.
    Q_INVOKABLE QVariantMap beginFont(int role, int selectionStart, int selectionEnd);
    Q_INVOKABLE bool changeFont(const QVariantMap &font);
    Q_INVOKABLE QVariantMap beginColour(int number, int role, int selectionStart, int selectionEnd);
    // The picker switched colours (legacy COLOR_TYPE_CHANGED): that colour in effect.
    Q_INVOKABLE QVariantMap switchColour(int number);
    // Y7: the simple "Color picker" switched colours (EditBox.cpp:908-912).
    // Y7-simple-picker-type: ASS formats get that colour in effect, which
    // later changes take as their reset (as switchColour); the line formats
    // keep the colour the picker shows (legacy turned it black and kept the
    // reset of the colour the picker opened with).
    Q_INVOKABLE QVariantMap simplePickerColour(int number);
    Q_INVOKABLE bool changeColour(const QVariantMap &colour);
    Q_INVOKABLE void endDialog(bool accepted);
    // Translation mode (legacy EditBox OnCopyAll, OnCopySelection, OnHideOriginal):
    // the Original's raw text replaces the Translated text; the Original's
    // selection is inserted at the Translated caret; the Original is wrapped
    // in braces (E63-comment-original: "Comment out original").
    Q_INVOKABLE bool pasteAllToTranslation();
    Q_INVOKABLE bool pasteSelectionToTranslation(int originalStart, int originalEnd, int translationCaret);
    Q_INVOKABLE bool commentOutOriginal();
    // Legacy OnPasteDifferents (Ctrl+, and Ctrl+.): the video time minus the
    // Line's Start, or its distance from the End, in milliseconds (both
    // truncated to 10 ms), replaces the selection of the edited field. Refused
    // without video or when the video time is outside the Line.
    Q_INVOKABLE bool insertTimeDifference(bool fromEnd, int selectionStart, int selectionEnd);
    // The text field reports its caret and selection (for automation's
    // aegisub.gui); `role` 0 Original, 1 Translated.
    Q_INVOKABLE void reportFieldSelection(int role, int start, int end);
    std::pair<int, int> fieldSelection() const { return m_fieldSelection[translationMode() ? 1 : 0]; }
    std::pair<int, int> fieldSelectionOf(int role) const { return m_fieldSelection[role == 1 ? 1 : 0]; }
    // Selects text in the edited field (aegisub.gui.set_cursor/set_selection),
    // or in `role` (0 Original, 1 Translated) when given.
    void selectInField(int start, int end, int role = -1);
    // F1: selects [start, end) of the raw text of `role` (0 Original, 1
    // Translated) as find marks a match; offsets move past hidden tags.
    void selectRaw(int role, int start, int end);
    // F1: [start, end) of the raw text of `role` where the field shows it
    // (past hidden tags), as selectRaw would select it.
    std::pair<int, int> displaySpan(int role, int start, int end) const;
    // The Document changed outside the editor (a macro): show it again.
    void reloadFromSession();
    // The video time shown (ms), or nullopt without video.
    void setVideoTimeSource(std::function<std::optional<std::int64_t>()> source) { m_videoTime = std::move(source); }
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
    // A legacy text command on the raw text of `role`, mapped through the
    // hidden-tag projection; the result's selection is requested in QML.
    bool editRaw(int role, int selectionStart, int selectionEnd,
                 const std::function<core::legacy::EditorText(core::legacy::EditorText)> &change);
    std::optional<std::u16string> styleTagValue(std::u16string_view tag) const;
    // E1 dialog sessions.
    struct DialogSession {
        int role = 0;
        bool several = false; // several Lines selected
        bool ass = true;
        core::legacy::NonAssFormat format = core::legacy::NonAssFormat::Srt;
        core::legacy::EditorText state; // raw text and selection of the field
        long position = 0;              // legacy GetPositionInText
        std::u8string original;
        std::size_t draftUndo = 0;
        bool hadDraft = false;
        core::legacy::EditorText opened; // the raw state the dialog opened on
        std::size_t historyCursor = 0;
        core::legacy::FontValues actualFont, editedFont;
        int number = 1;
        core::legacy::TagColour actualColour;
        core::legacy::TagColour editedColour; // the colour last chosen in the dialog
    };
    bool beginDialog(int role, int selectionStart, int selectionEnd);
    bool applyDialogChange(const std::function<core::legacy::StepResult(const core::legacy::EditorText &, long)> &one,
                           const std::function<void(std::u16string &, std::u16string &)> &several);
    void publishRawSelection(const core::legacy::EditorText &state);
    std::optional<core::StyleValues> lineStyle() const;
    std::optional<DialogSession> m_dialog;
    core::legacy::TagColour m_lastColour; // legacy EditBox::actualColor (kept between pickers)
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
    std::function<std::optional<std::int64_t>()> m_videoTime;
    std::pair<int, int> m_fieldSelection[2] = {{0, 0}, {0, 0}};
};

} // namespace hikari::ui
