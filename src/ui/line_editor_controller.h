#pragma once

// Presenter for the Line editor (V2; docs/ux/ass-editor.md). One source of
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
#include "hikari/application/editor_fields.h"
#include "hikari/application/legacy_timebase.h"
#include "hikari/core/editor_syntax.h"
#include "hikari/core/editor_font_colour.h"
#include "hikari/core/style.h"
#include "hikari/core/tag_transfer.h"
#include "hikari/core/tag_commands.h"
#include "tag_list_controller.h"

#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <map>
#include <memory>
#include <tuple>
#include <utility>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace hikari::ui {

class LineEditorController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application composition")
    Q_PROPERTY(bool hasLine READ hasLine NOTIFY changed)
    Q_PROPERTY(bool editable READ editable NOTIFY changed)
    Q_PROPERTY(bool showTags READ showTags WRITE setShowTags NOTIFY changed)
    // E6: the tag list popup of the text fields.
    Q_PROPERTY(TagListController *tagList READ tagList CONSTANT)
    Q_PROPERTY(QString text READ text NOTIFY changed)
    // Translation mode (Script Info "TLMode: Yes"): Original above Translated.
    Q_PROPERTY(bool translationMode READ translationMode NOTIFY changed)
    Q_PROPERTY(QString translationText READ translationText NOTIFY changed)
    // FS1-current-line: the raw text the video shows for the Line, the
    // draft applied: in translation mode its translation when it has one.
    Q_PROPERTY(QString videoText READ videoText NOTIFY changed)
    // Its times as the video has them ("start – end", the start alone
    // where the format has no end), from the same record as videoText.
    Q_PROPERTY(QString videoTimes READ videoTimes NOTIFY changed)
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
    // The field a requested selection belongs to (0 Original, 1 Translated,
    // 2 Actor, 3 Effect).
    Q_PROPERTY(int selectionRole READ selectionRole NOTIFY selectionRequested)
    // E4: the metadata fields (legacy EditBox's Comment, LayerEdit, DurEdit,
    // StyleChoice, ActorEdit, EffectEdit and the \an choice Ban), enabled as
    // EditBox::HideControls enables them for the Document's format.
    Q_PROPERTY(bool assFields READ assFields NOTIFY changed)
    Q_PROPERTY(bool hasEnd READ hasEnd NOTIFY changed)
    Q_PROPERTY(bool comment READ comment NOTIFY changed)
    Q_PROPERTY(QString layerText READ layerText NOTIFY changed)
    Q_PROPERTY(QString durationText READ durationText NOTIFY changed)
    Q_PROPERTY(QString style READ style NOTIFY changed)
    Q_PROPERTY(QStringList styleNames READ styleNames NOTIFY changed)
    Q_PROPERTY(int styleIndex READ styleIndex NOTIFY changed)
    // LE1: the shown Line's Style's left, right and vertical margins (what a
    // Line margin of 0 stands for); empty without that Style.
    Q_PROPERTY(QStringList styleMargins READ styleMargins NOTIFY changed)
    Q_PROPERTY(QString actor READ actor NOTIFY changed)
    Q_PROPERTY(QString effect READ effect NOTIFY changed)
    Q_PROPERTY(QStringList actors READ actors NOTIFY changed)
    Q_PROPERTY(QStringList effects READ effects NOTIFY changed)
    // 0..8 for "Bottom-left (an1)" .. "Top-right (an9)", -1 for none.
    Q_PROPERTY(int alignmentIndex READ alignmentIndex NOTIFY changed)
    // LE4: several Lines selected whose alignments differ (the picker's
    // mixed icon); the shown Line's is alignmentIndex.
    Q_PROPERTY(bool alignmentMixed READ alignmentMixed NOTIFY changed)
    // The counters (EditBox::UpdateChars) with their warnings: each wrap's
    // characters and the limit ("5/10/43"), and the characters per second
    // and the limit ("8/15"); empty for a comment. LE1: the Line editor
    // names them (Wraps, CPS) beside the numbers.
    Q_PROPERTY(QString charsText READ charsText NOTIFY changed)
    Q_PROPERTY(bool charsWarning READ charsWarning NOTIFY changed)
    Q_PROPERTY(QString cpsText READ cpsText NOTIFY changed)
    Q_PROPERTY(bool cpsWarning READ cpsWarning NOTIFY changed)
    // Start and End in the warning colour while a live edit has Start after
    // End (EditBox::OnEdit, EditBox.cpp:1526-1545), and Start, End and
    // Duration while they hold times the audio display put there and the
    // Line has not taken (TimeCtrl::SetTime(..., true) from
    // AudioDisplay::CommitChanges and UpdateTimeEditCtrls,
    // AudioDisplay.cpp:1718-1722, 2825-2826), until the Line is sent or shown
    // again (Send, EditBox.cpp:546-554; SetLine, EditBox.cpp:394-402). A Send
    // from OnCommit while a visual tool is on leaves them (EditBox.cpp:972).
    Q_PROPERTY(bool startWarning READ startWarning NOTIFY changed)
    Q_PROPERTY(bool endWarning READ endWarning NOTIFY changed)
    Q_PROPERTY(bool durationWarning READ durationWarning NOTIFY changed)
    // The Time/Frames switch (EDITBOX_TIMES_TO_FRAMES_SWITCH): enabled once a
    // video was opened (Notebook::LoadVideo), as legacy enables it.
    Q_PROPERTY(bool framesAvailable READ framesAvailable NOTIFY changed)
    Q_PROPERTY(bool showFrames READ showFrames WRITE setShowFrames NOTIFY changed)
    // The time fields' form (application::TimeFieldForm: 0 ASS, 1 SRT,
    // 2 TMPlayer, 3 a number), which TimeField.qml masks its editing to.
    Q_PROPERTY(int timeForm READ timeForm NOTIFY changed)
    // E4: what the time fields' mouse reads (TimeCtrl::OnMouseEvent):
    // TimeCtrl's own form (application::TimeCtrlForm: 0 ASS, 1 SRT,
    // 2 TMPlayer, 3 MicroDVD, 4 MPL2) and whether the fields show frames.
    Q_PROPERTY(int timeCtrlForm READ timeCtrlForm NOTIFY changed)
    Q_PROPERTY(bool framesShown READ framesShown NOTIFY changed)
    // E4-stepped-field-warning: the fields holding a mouse step the Line has
    // not taken (timeModified, numberModified), one bit a role: the time
    // fields' TimeFieldRole (1 << role), the Layer's and the margins' role.
    Q_PROPERTY(int steppedTimes READ steppedTimes NOTIFY changed)
    Q_PROPERTY(int steppedNumbers READ steppedNumbers NOTIFY changed)
    // E5: the active Line's Unconfirmed (legacy DoubtfulTL->SetValue in
    // SetLine) and the "Tags from original" toggle (TM1; legacy "Moving tags",
    // AUTO_MOVE_TAGS_FROM_ORIGINAL).
    Q_PROPERTY(bool unconfirmed READ unconfirmed NOTIFY changed)
    Q_PROPERTY(bool moveTags READ moveTags NOTIFY changed)
    // TM1: the Original text's runs while tags are carried (tagRuns()).
    Q_PROPERTY(QVariantList tagRuns READ tagRuns NOTIFY changed)
    // TM1: the active Line is karaoke (\k): only its leading tags are carried.
    Q_PROPERTY(bool karaokeCarried READ karaokeCarried NOTIFY changed)
    // E4: the edited text field's state (core::syntax::TextState: 0 dialogue,
    // 1 comment, 2 template, 3 code), as legacy TextEditor::SetState gets it
    // from EditBox::SetLine and from a Comment box toggle (OnCommit), not
    // from typing in Effect.
    Q_PROPERTY(int textState READ textState NOTIFY changed)

public:
    // The options the editor reads where legacy reads them (O1's registry).
    struct Options {
        bool liveEditing = true;        // !DISABLE_LIVE_VIDEO_EDITING
        bool dontAdvanceOnTimes = false; // EDITBOX_DONT_GO_TO_NEXT_LINE_ON_TIMES_EDIT
        bool allCharsForCps = false;     // CALC_SPACES_AND_PUNCTATION_FOR_CPS
        bool allCharsForWraps = false;   // CALC_SPACES_AND_PUNCTATION_FOR_WRAPS
    };

    explicit LineEditorController(application::DocumentFiles &files, QObject *parent = nullptr);
    TagListController *tagList() { return &m_tagList; }

    // The Document being edited (the workspace's editing target), or none.
    // `editable` is false for a protected reference.
    void setDocument(std::optional<application::DocumentId> document, bool editable);
    // Called after the session's committed content changes (for the Grid).
    void setCommittedListener(std::function<void()> listener) { m_onCommitted = std::move(listener); }

    bool hasLine() const;
    bool editable() const { return m_editable && hasLine(); }
    bool showTags() const { return m_showTags; }
    void setShowTags(bool show);
    // LE3-tags-shown: the stored choice (editor.hideTags inverted); the
    // editor saves each switch through `store`.
    void setShowTagsSetting(bool show, std::function<void(bool)> store);
    QString text() const { return m_shown[0]; }
    bool translationMode() const;
    QString translationText() const { return m_shown[1]; }
    QString videoText() const;
    QString videoTimes() const;
    QString startText() const;
    QString endText() const;
    QString marginLeftText() const;
    QString marginRightText() const;
    QString marginVerticalText() const;
    QString problem() const { return m_problem; }
    QString attempted() const { return m_attempted; }
    bool dirty() const;
    QString saveStatus() const;

    // E4
    void setOptionsSource(std::function<Options()> source) { m_options = std::move(source); }
    // An option the editor reads changed: the counters and Duration follow at once.
    void optionsChanged() { refresh(); }
    // LE4: the selected Lines changed (the active one may not have): what
    // depends on them, alignmentMixed among it, is read again.
    void selectionChanged() { emit changed(); }
    // The open video's timebase (legacy TimeCtrl::SetVideoBox), or none.
    void setVideoTimebase(std::optional<application::LegacyTimebase> timebase);
    // EDITBOX_TIMES_TO_FRAMES_SWITCH as stored; the editor saves the switch through `store`.
    void setShowFramesSetting(bool on, std::function<void(bool)> store);
    bool assFields() const;
    bool hasEnd() const;
    int timeForm() const;
    int timeCtrlForm() const;
    bool framesShown() const { return frameTimebase() != nullptr; }
    int steppedTimes() const;
    int steppedNumbers() const;
    bool comment() const;
    QString layerText() const;
    QString durationText() const;
    QString style() const;
    QStringList styleNames() const;
    int styleIndex() const;
    QStringList styleMargins() const;
    QString actor() const;
    QString effect() const;
    QStringList actors() const { return m_actors; }
    QStringList effects() const { return m_effects; }
    int alignmentIndex() const { return m_alignment >= 1 && m_alignment <= 9 ? m_alignment - 1 : -1; }
    bool alignmentMixed() const;
    QString charsText() const;
    bool charsWarning() const;
    QString cpsText() const;
    bool cpsWarning() const;
    bool startWarning() const { return m_timeWarning[0]; }
    bool endWarning() const { return m_timeWarning[1]; }
    bool durationWarning() const { return m_timeWarning[2]; }
    // E4: the audio display puts its selection into the time fields
    // (AudioDisplay::CommitChanges sets Start, End and Duration; with
    // `duration` false, UpdateTimeEditCtrls sets Start and End). Called
    // before the times reach the draft: each field whose time (in
    // milliseconds, SubsTime::operator==) changes takes the warning colour.
    void timesFromAudio(std::int64_t startMs, std::int64_t endMs, bool duration);
    // The audio display's EditBox::Send(AUDIO_CHANGE_TIME, ...): the time
    // fields back in the text colour.
    void audioSent();
    bool framesAvailable() const { return m_framesAvailable; }
    int textState() const { return static_cast<int>(m_textState); }
    core::syntax::TextState syntaxState() const { return m_textState; }
    // E4: the colour runs of `text` in the field `role` (0 Original, 1
    // Translated), flat [start, end, core::syntax::Role] triples (TextEditor's
    // DrawFieldD2D). The edited field (TextEdit: the Translated one in
    // translation mode) is coloured in the text state; the other as dialogue.
    Q_INVOKABLE QVariantList colourRuns(int role, const QString &text) const;
    bool showFrames() const { return m_showFrames; }
    void setShowFrames(bool on);
    // The timebase the times show frames of (switch on, exact), or nullptr.
    const application::LegacyTimebase *frameTimebase() const;
    // The Document with the pending draft applied (live video editing), or nullopt without a draft.
    std::optional<core::Document> draftDocument() const;

    // Legacy OnCommit through the Comment box and the Style choice: the field
    // changes and the Line is sent at once (EditBox.cpp:323-325, 966-985).
    Q_INVOKABLE bool setComment(bool comment);
    Q_INVOKABLE bool chooseStyle(const QString &style);
    // Typing in the Actor and Effect boxes changes the draft; picking from
    // their lists sends the Line (ID_COMBO_BOX_CTRL, EditBox.cpp:322).
    Q_INVOKABLE void setActorText(const QString &text);
    Q_INVOKABLE void setEffectText(const QString &text);
    Q_INVOKABLE bool chooseActor(const QString &text);
    Q_INVOKABLE bool chooseEffect(const QString &text);
    Q_INVOKABLE void setLayerText(const QString &text);
    // DurEdit: End becomes Start + the duration (EditBox::OnEdit's durFocus
    // branch), which runs only with live video editing on (EditBox.cpp:349-352).
    Q_INVOKABLE void setDurationText(const QString &text);
    // A time field's text changed while it is being typed (TimeCtrl's
    // NUMBER_CHANGED, which runs EditBox::OnEdit with live video editing on,
    // EditBox.cpp:343-346): `role` is TimeFieldRole's 0 Duration, 1 Start,
    // 2 End. Start or End go to the draft and Duration follows; Duration moves
    // End. Without live editing nothing happens until the field is applied.
    // Text not yet in the field's form is left for the field's apply, since
    // the legacy control only ever holds its form.
    Q_INVOKABLE void timeTyped(int role, const QString &text);
    // E4: the mouse stepped a time field without the focus (TimeCtrl's
    // wheel: SetValue(..., true) and NUMBER_CHANGED). The field is modified
    // and keeps `text`, and the Line does not take it until the next Send
    // (EditBox::Send reads every modified field, EditBox.cpp:560-605); showing
    // a Line, Esc, the Time/Frames switch or a time from the audio display
    // drops it, as SetTime replaced the text. With live video editing
    // EditBox::OnEdit still runs for the field with the focus, `focused`
    // (TimeFieldRole, or -1): with Start or End focused the draft takes both
    // fields' times, so a stepped Start or End goes to it and Duration
    // follows; with Duration focused End shows the draft's End again
    // (EditBox.cpp:1520-1556).
    Q_INVOKABLE void timeStepped(int role, const QString &text, int focused);
    // E4: the mouse stepped the Layer (0) or a margin (1 left, 2 right,
    // 3 vertical) without the focus (NumCtrl's wheel): modified until the
    // next Send takes it or a Line is shown.
    Q_INVOKABLE void numberStepped(int role, const QString &text);
    // Whether a field holds a step the Line has not taken: TimeFieldRole for
    // the time fields, the numberStepped role for the others.
    Q_INVOKABLE bool timeModified(int role) const;
    Q_INVOKABLE bool numberModified(int role) const;
    // EditBox::OnAnChoice: \an<index + 1> into the first block of the edited
    // text field (TextEdit, or TextEditOrig while the translation is empty).
    Q_INVOKABLE bool chooseAlignment(int index);
    // EDITBOX_COMMIT_GO_NEXT_LINE from a field (EditBox::OnNewline): the
    // time fields commit and stay with EDITBOX_DONT_GO_TO_NEXT_LINE_ON_TIMES_EDIT.
    Q_INVOKABLE bool commitFromField(bool timeField);

    // The Grid or navigation asked for a Line; commits on leave.
    Q_INVOKABLE bool showLine(qulonglong id);
    // The text field now holds `newText` with the caret at `cursor`.
    Q_INVOKABLE void textEdited(const QString &newText, int cursor);
    Q_INVOKABLE void translationEdited(const QString &newText, int cursor);
    Q_INVOKABLE void setStartText(const QString &text);
    Q_INVOKABLE void setEndText(const QString &text);
    Q_INVOKABLE void setMarginText(int which, const QString &text); // 0 left, 1 right, 2 vertical
    Q_INVOKABLE bool commitAndAdvance(); // Enter, outside composition
    // E6: GLOBAL_NEXT_LINE (1) / GLOBAL_PREVIOUS_LINE (-1), SubsGrid::NextLine:
    // the next or previous shown Line; past the last shown one a new Line.
    Q_INVOKABLE bool nextLine(int direction);
    // EditBox::Send(EDITBOX_LINE_EDITION, false): the draft to the Line.
    Q_INVOKABLE bool commit();
    // EDITBOX_COMMIT "Apply changes" (EditBox::OnCommit): commit() as
    // Send(..., visualdummy) with a visual tool on, which leaves the time
    // fields' warning colour (EditBox.cpp:972).
    Q_INVOKABLE bool applyChanges();
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
    // TM1: in the Translated field, with a selection and tags carried, the
    // Original's first run of that style (unplaced ones first) is placed on
    // it instead (placeRun).
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
    // T3: that selection in the raw text of `role` (the caret goes after
    // hidden boundary tags, as the tag commands take it).
    std::pair<long, long> rawFieldSelection(int role) const;
    // Selects text in the edited field (aegisub.gui.set_cursor/set_selection),
    // or in `role` (0 Original, 1 Translated) when given.
    void selectInField(int start, int end, int role = -1);
    // F1: selects [start, end) of the raw text of `role` (0 Original, 1
    // Translated) as find marks a match; offsets move past hidden tags.
    void selectRaw(int role, int start, int end);
    // F1/E4: selects [start, end) of the Actor (role 2) or Effect (3) box
    // (legacy ComboBoxCtrl::SetTextSelection, findreplace.cpp:366-371).
    void selectInBox(int role, int start, int end);
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
    // E5: the "Not confirmed" button (EditBox::OnDoubtfulTl without
    // NextLine): every selected Line's Unconfirmed flips, one step.
    Q_INVOKABLE bool toggleUnconfirmed();
    bool unconfirmed() const;
    // TM1 (#232) "Tags from original", legacy's "Moving tags" toggle
    // (core/tag_transfer.h; departure TM1-original-keeps-tags). With it on in
    // translation mode the Original text keeps its tags, and the Translated
    // field holds the translation without the tags carried from the Original:
    // its leading block (every line-wide tag) and a wrapper's closing tags.
    // Each change to the Translated text writes them back around it, and a
    // change to the Original's carries the new ones to the translation. Off,
    // or for a translation whose own leading or trailing tags differ, the
    // Translated field holds the whole translation. Showing an untranslated
    // Line with tags gives the Translated field the focus, as legacy's split did.
    bool moveTags() const { return m_moveTags; }
    void setMoveTags(bool on); // shows the Line again, as OnAutoMoveTags does
    // The active Line's runs (core::TagRun) while tags are carried, by
    // number: number, text, kind (what it changes: "Italic", "Bold, Colour"),
    // colour ("#rrggbb" for a primary colour run, else empty), placed, and
    // where it is in each field as the field shows its text: originalStart
    // and originalEnd, start and end in the Translated field (-1 unplaced),
    // and suggestedStart and suggestedEnd (-1 none): an unplaced run whose
    // text the translation has once, as a whole word (core::suggestPlacements),
    // until dismissed.
    QVariantList tagRuns() const;
    bool karaokeCarried() const
    {
        const auto tags = runTags();
        return tags && tags->karaoke;
    }
    // Places run `number` (0: the lowest-numbered one not placed) on the
    // Translated field's [selectionStart, selectionEnd), as a draft change:
    // core::placeRun moves an earlier placement there, or takes it off the
    // same selection. The selection stays on the same text. False without
    // a selection, carried tags or that run.
    Q_INVOKABLE bool placeRun(int number, int selectionStart, int selectionEnd);
    // The suggested placements: all placed in one draft change (Tab), or
    // dismissed for the active Line (Escape). False when there are none.
    Q_INVOKABLE bool acceptSuggestedRuns();
    Q_INVOKABLE bool dismissSuggestedRuns();
    void setVisualToolActive(std::function<bool()> active) { m_visualToolActive = std::move(active); }
    Q_INVOKABLE bool findNextUnconfirmed();
    Q_INVOKABLE bool findNextUntranslated();
    int selectionStart() const { return m_selectionStart; }
    int selectionEnd() const { return m_selectionEnd; }
    int selectionRole() const { return m_selectionRole; }

    // The WriteCoordinator listener calls this after DocumentFiles has the result.
    void writeFinished();

signals:
    void changed();
    // The Grid's Start/End columns show frames or times (SubsGrid::ChangeTimeDisplay).
    void frameDisplayChanged();
    void lineChanged(qulonglong id); // the active Line moved (keeps the Grid in step)
    // V6: showLine committed `line`'s draft on leaving it (legacy SetLine's
    // Send of the old Line, EditBox.cpp:383-384); emitted before changed().
    void leftLineCommitted(qulonglong line);
    void selectionRequested();
    void fieldFocusRequested(int role); // E5: 0 Original, 1 Translated
    // E4: discard() put the Line's values back: the fields drop what they
    // hold (a typed or stepped text, their modified flag), focused or not.
    void discarded();

private:
    application::EditSession *session() const;
    // The active Line, draft applied, its translation without the carried tags.
    std::optional<core::LineRecord> record() const;
    std::optional<core::LineRecord> sessionRecord() const; // the active Line, draft applied
    // FS1: the active Line as the video shows it: the committed Line and the
    // draft's changes over it (null without), both in place.
    std::pair<const core::LineRecord *, const application::DraftChange *> videoLine() const;
    // TM1: the tags carried from `line`'s Original text to its translation, as
    // the Translated field leaves them out; nullopt when nothing is carried.
    struct Carry {
        core::OriginalTags tags;
        std::u8string leading, trailing;
    };
    std::optional<Carry> carry(const core::LineRecord &line) const;
    // The Document's Styles for carry(), decoded once a revision; nullptr for
    // one with more than core::kMaxTagTransferStyles (nothing carried).
    const std::vector<core::StyleValues> *carryStyles() const;
    struct CarryStyles {
        using Key = std::tuple<std::uint64_t, const core::Document *, std::uint64_t>;
        Key key;
        std::vector<core::StyleValues> styles;
        bool within = true;
    };
    mutable std::optional<CarryStyles> m_carryStyles;
    // The active Line's Original tags read with its Style, for its runs;
    // nullopt when nothing is carried.
    std::optional<core::OriginalTags> runTags() const;
    // The Line's translation as a draft of `tags`, its equal runs named as
    // the translator last placed them (m_runOrders).
    core::TranslationDraft translationDraft(const core::OriginalTags &tags, const core::LineRecord &line) const;
    // The draft composed without the carried tags; notes its run order
    // (m_runOrders).
    std::u8string composedBody(const core::OriginalTags &tags, core::TranslationDraft draft, core::LineId line);
    // composedBody through setRaw(1) for the active Line.
    bool saveTranslationDraft(const core::OriginalTags &tags, core::TranslationDraft draft);
    // `before`'s translation for its Original's new carry `now`: the placed
    // runs read with the old runs and written with the new ones.
    std::u8string recarried(const core::LineRecord &before, const Carry &now);
    // core::suggestPlacements for the active Line, less the dismissed ones.
    std::vector<core::RunPlacement> suggestedRuns(const core::OriginalTags &tags,
                                                  const core::TranslationDraft &draft) const;
    // The translation a Translated field text stands for.
    static std::u8string carried(const Carry &carry, std::u8string body);
    std::size_t carriedLead() const; // UTF-16 units of the carried leading block before the field's text
    std::optional<core::LineId> m_carryEvaluated; // the Line SetLine last showed
    bool m_moveTags = false;
    std::function<bool()> m_visualToolActive;
    void evaluateCarry(); // what legacy SetTextWithTags did on SetLine
    bool m_carryFocus = false; // an untranslated Line asks for the Translated field's focus
    // The Line whose suggestions were dismissed, with the Original text and
    // Style its runs were read from: another Original or Style numbers other
    // runs, and the dismissals no longer hold.
    std::optional<std::tuple<core::LineId, std::u8string, std::u8string>> m_dismissedFor;
    std::set<int> m_dismissedRuns;
    // TM1: each Line's placed run numbers in text order (core::relabelRuns),
    // by Document and Line (each Document numbers its own Lines), with the
    // translation they were composed into and the runs they were composed
    // with, the latest first. A translation is read with the entry of its
    // text and runs; with runs that changed under it (a Style edited, an
    // Original edit undone) as it was composed, each placement given to the
    // run it became; another translation without them.
    using RunOrderKey = std::pair<std::uint64_t, std::uint64_t>;
    RunOrderKey runOrderKey(core::LineId line) const { return {m_document ? m_document->value : 0, line.value}; }
    struct RunOrder {
        std::u8string translation;
        std::vector<int> order;
        std::shared_ptr<const core::OriginalTags> tags; // shared by the entries read the same
        std::uint64_t noted = 0;                         // when, for the oldest to go first
    };
    static constexpr std::size_t kRunOrderHistory = 16; // entries kept per Line
    // The runs every entry keeps, in UTF-16 units, at most: past it the
    // oldest entries of any Line go.
    static constexpr std::size_t kRunOrderUnits = std::size_t(1) << 21;
    std::map<RunOrderKey, std::vector<RunOrder>> m_runOrders;
    std::uint64_t m_runOrderClock = 0;
    // Kept as entries come and go, so a note never walks every Line: the
    // entries' Lines by age (the oldest first), the entries sharing each
    // reading, and the units of them all, each reading counted once.
    std::map<std::uint64_t, RunOrderKey> m_runOrderAges;
    std::map<const core::OriginalTags *, std::size_t> m_runOrderReadings;
    std::size_t m_runOrderSize = 0;
    void countRunOrder(RunOrderKey key, const RunOrder &entry);
    void uncountRunOrder(const RunOrder &entry);
    std::optional<RunOrder> runOrderOf(core::LineId line) const;
    void setRunOrder(core::LineId line, std::optional<RunOrder> order);
    void noteRunOrder(core::LineId line, RunOrder order);
    void refresh();
    void fail(const QString &problem, const QString &attempted = {});
    // `orderBefore`: the Line's run order before this step, when the caller
    // composed the new one already (saveTranslationDraft).
    bool setRaw(int role, std::u8string raw, const std::optional<RunOrder> *orderBefore = nullptr);
    // editRaw's change on the draft's body when the Translated field has
    // placed runs; false to edit the raw text instead.
    bool editPlacedBody(int selectionStart, int selectionEnd,
                        const std::function<core::legacy::EditorText(core::legacy::EditorText)> &change);
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
    bool applyDialogToPlacedBody(DialogSession &d,
                                 const std::function<core::legacy::StepResult(const core::legacy::EditorText &, long)> &one);
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

    // E4
    Options options() const { return m_options ? m_options() : Options{}; }
    std::optional<core::LineRecord> committedRecord() const;
    // EditBox::Send(EDITBOX_LINE_EDITION, ...); `visual`: OnCommit's
    // visualdummy, which keeps the time fields' warning colour.
    bool sendDraft(bool leaving = false, bool visual = false);
    bool visualToolOn() const { return m_visualToolActive && m_visualToolActive(); }
    void rebuildLists(); // EditBox::RebuildActorEffectLists
    void lineShown();    // what EditBox::SetLine sets once per Line
    bool setTime(application::TimeFieldRole role, const QString &text);
    std::function<Options()> m_options;
    std::optional<application::LegacyTimebase> m_timebase;
    bool m_framesAvailable = false;
    bool m_showFrames = false;
    std::function<void(bool)> m_storeShowFrames;
    QStringList m_actors, m_effects;
    int m_alignment = 2;
    bool m_durationEdited = false; // OnEdit's Duration after a Start/End edit
    // StartEdit/EndEdit/DurEdit: the warning colour shown, and legacy's
    // changedBackGround, which is set with it and never cleared.
    bool m_timeWarning[3] = {false, false, false};
    bool m_timeWarned[3] = {false, false, false};
    void clearTimeWarnings();
    std::optional<QString> m_typedDuration; // DurEdit keeps what was typed until SetLine
    // E4: fields the mouse stepped without the focus (legacy IsModified with
    // no OnEdit): TimeFieldRole's Duration, Start, End, and the Layer and
    // margins. Their text shows in place of the Line's until a Send applies
    // them (applyStepped) or the Line is shown again.
    std::optional<QString> m_steppedTime[3];
    std::optional<QString> m_steppedNumber[4];
    bool applyStepped();
    void dropStepped();
    // OnEdit's startEndFocus branch for the focused Start (0) or End (1).
    void liveStartEnd(int focused);
    std::optional<core::LineId> m_shownLine;
    core::syntax::TextState m_textState = core::syntax::TextState::Dialogue;
    void updateTextState(); // TextEditor::SetState from the Line's Comment and Effect
    // LE4: alignmentMixed for a revision and selection, kept until either changes.
    struct MixedAlignment {
        const application::EditSession *session = nullptr;
        std::uint64_t revision = 0;
        int shownAlignment = 0;
        std::set<core::LineId> selected;
        bool mixed = false;
    };
    mutable std::optional<MixedAlignment> m_mixedAlignment;

    application::DocumentFiles &m_files;
    std::optional<application::DocumentId> m_document;
    bool m_editable = false;
    bool m_showTags = true; // LE3-tags-shown: tags shown by default
    std::function<void(bool)> m_storeShowTags;
    TagListController m_tagList;
    QString m_shown[2];      // what the Original and Translated fields show
    QString m_problem;
    QString m_attempted;
    // Exact raw snapshots of this draft, both roles.
    struct Snapshot {
        std::u8string text, translation;
        bool translationTyped = false;  // the draft's translation cell was typed then
        std::optional<RunOrder> order; // the Line's run order then
    };
    // The draft holds a typed translation cell, not only a carried one
    // (application::DraftChange::translationCarried).
    bool translationTyped();
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
