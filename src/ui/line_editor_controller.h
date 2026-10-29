#pragma once

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
    Q_PROPERTY(TagListController *tagList READ tagList CONSTANT)
    Q_PROPERTY(QString text READ text NOTIFY changed)
    Q_PROPERTY(bool translationMode READ translationMode NOTIFY changed)
    Q_PROPERTY(QString translationText READ translationText NOTIFY changed)
    Q_PROPERTY(QString videoText READ videoText NOTIFY changed)
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
    Q_PROPERTY(int selectionStart READ selectionStart NOTIFY selectionRequested)
    Q_PROPERTY(int selectionEnd READ selectionEnd NOTIFY selectionRequested)
    Q_PROPERTY(QStringList history READ history NOTIFY changed)
    Q_PROPERTY(int historyCursor READ historyCursor NOTIFY changed)
    Q_PROPERTY(bool canUndoToLastSave READ canUndoToLastSave NOTIFY changed)
    // MUST KILL: selectionRole is a magic int (0 Original, 1 Translated, 2 Actor, 3 Effect)
    Q_PROPERTY(int selectionRole READ selectionRole NOTIFY selectionRequested)
    Q_PROPERTY(bool assFields READ assFields NOTIFY changed)
    Q_PROPERTY(bool hasEnd READ hasEnd NOTIFY changed)
    Q_PROPERTY(bool comment READ comment NOTIFY changed)
    Q_PROPERTY(QString layerText READ layerText NOTIFY changed)
    Q_PROPERTY(QString durationText READ durationText NOTIFY changed)
    Q_PROPERTY(QString style READ style NOTIFY changed)
    Q_PROPERTY(QStringList styleNames READ styleNames NOTIFY changed)
    Q_PROPERTY(int styleIndex READ styleIndex NOTIFY changed)
    Q_PROPERTY(QStringList styleMargins READ styleMargins NOTIFY changed)
    Q_PROPERTY(QString actor READ actor NOTIFY changed)
    Q_PROPERTY(QString effect READ effect NOTIFY changed)
    Q_PROPERTY(QStringList actors READ actors NOTIFY changed)
    Q_PROPERTY(QStringList effects READ effects NOTIFY changed)
    Q_PROPERTY(int alignmentIndex READ alignmentIndex NOTIFY changed)
    Q_PROPERTY(bool alignmentMixed READ alignmentMixed NOTIFY changed)
    Q_PROPERTY(QString charsText READ charsText NOTIFY changed)
    Q_PROPERTY(bool charsWarning READ charsWarning NOTIFY changed)
    Q_PROPERTY(QString cpsText READ cpsText NOTIFY changed)
    Q_PROPERTY(bool cpsWarning READ cpsWarning NOTIFY changed)
    Q_PROPERTY(bool startWarning READ startWarning NOTIFY changed)
    Q_PROPERTY(bool endWarning READ endWarning NOTIFY changed)
    Q_PROPERTY(bool durationWarning READ durationWarning NOTIFY changed)
    Q_PROPERTY(bool framesAvailable READ framesAvailable NOTIFY changed)
    Q_PROPERTY(bool showFrames READ showFrames WRITE setShowFrames NOTIFY changed)
    // MUST KILL: timeForm is application::TimeFieldForm passed as a magic int
    Q_PROPERTY(int timeForm READ timeForm NOTIFY changed)
    // MUST KILL: timeCtrlForm is application::TimeCtrlForm passed as a magic int
    Q_PROPERTY(int timeCtrlForm READ timeCtrlForm NOTIFY changed)
    Q_PROPERTY(bool framesShown READ framesShown NOTIFY changed)
    // MUST KILL: steppedTimes is a bitmask, one bit per TimeFieldRole int
    Q_PROPERTY(int steppedTimes READ steppedTimes NOTIFY changed)
    // MUST KILL: steppedNumbers is a bitmask, one bit per numberStepped role int
    Q_PROPERTY(int steppedNumbers READ steppedNumbers NOTIFY changed)
    Q_PROPERTY(bool unconfirmed READ unconfirmed NOTIFY changed)
    Q_PROPERTY(bool moveTags READ moveTags NOTIFY changed)
    Q_PROPERTY(QVariantList tagRuns READ tagRuns NOTIFY changed)
    Q_PROPERTY(bool karaokeCarried READ karaokeCarried NOTIFY changed)
    Q_PROPERTY(int textState READ textState NOTIFY changed)

public:
    struct Options {
        bool liveEditing = true;
        bool dontAdvanceOnTimes = false;
        bool allCharsForCps = false;
        bool allCharsForWraps = false;
    };

    explicit LineEditorController(application::DocumentFiles &files, QObject *parent = nullptr);
    TagListController *tagList() { return &m_tagList; }

    void setDocument(std::optional<application::DocumentId> document, bool editable);
    void setCommittedListener(std::function<void()> listener) { m_onCommitted = std::move(listener); }

    bool hasLine() const;
    bool editable() const { return m_editable && hasLine(); }
    bool showTags() const { return m_showTags; }
    void setShowTags(bool show);
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

    void setOptionsSource(std::function<Options()> source) { m_options = std::move(source); }
    void optionsChanged() { refresh(); }
    void selectionChanged() { emit changed(); }
    void setVideoTimebase(std::optional<application::LegacyTimebase> timebase);
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
    // MUST KILL: charsText and cpsText hard-code the limits 43 and 15 and a "0/86" placeholder without a Line
    QString charsText() const;
    bool charsWarning() const;
    QString cpsText() const;
    bool cpsWarning() const;
    bool startWarning() const { return m_timeWarning[0]; }
    bool endWarning() const { return m_timeWarning[1]; }
    bool durationWarning() const { return m_timeWarning[2]; }
    void timesFromAudio(std::int64_t startMs, std::int64_t endMs, bool duration);
    void audioSent();
    bool framesAvailable() const { return m_framesAvailable; }
    int textState() const { return static_cast<int>(m_textState); }
    core::syntax::TextState syntaxState() const { return m_textState; }
    // MUST KILL: colourRuns returns a flat int list of [start, end, syntax::Role] triples; role is 0 Original, 1 Translated
    Q_INVOKABLE QVariantList colourRuns(int role, const QString &text) const;
    bool showFrames() const { return m_showFrames; }
    void setShowFrames(bool on);
    const application::LegacyTimebase *frameTimebase() const;
    std::optional<core::Document> draftDocument() const;

    Q_INVOKABLE bool setComment(bool comment);
    Q_INVOKABLE bool chooseStyle(const QString &style);
    Q_INVOKABLE void setActorText(const QString &text);
    Q_INVOKABLE void setEffectText(const QString &text);
    Q_INVOKABLE bool chooseActor(const QString &text);
    Q_INVOKABLE bool chooseEffect(const QString &text);
    Q_INVOKABLE void setLayerText(const QString &text);
    Q_INVOKABLE void setDurationText(const QString &text);
    // MUST KILL: role is TimeFieldRole as a magic int (0 Duration, 1 Start, 2 End)
    Q_INVOKABLE void timeTyped(int role, const QString &text);
    // MUST KILL: role and focused are TimeFieldRole magic ints, focused -1 for none
    Q_INVOKABLE void timeStepped(int role, const QString &text, int focused);
    // MUST KILL: role is a magic int (0 Layer, 1 left, 2 right, 3 vertical margin), one off from setMarginText's which
    Q_INVOKABLE void numberStepped(int role, const QString &text);
    Q_INVOKABLE bool timeModified(int role) const;
    Q_INVOKABLE bool numberModified(int role) const;
    Q_INVOKABLE bool chooseAlignment(int index);
    Q_INVOKABLE bool commitFromField(bool timeField);

    // MUST KILL: showLine also commits the left Line's draft
    Q_INVOKABLE bool showLine(qulonglong id);
    Q_INVOKABLE void textEdited(const QString &newText, int cursor);
    Q_INVOKABLE void translationEdited(const QString &newText, int cursor);
    Q_INVOKABLE void setStartText(const QString &text);
    Q_INVOKABLE void setEndText(const QString &text);
    // MUST KILL: which is a magic int (0 left, 1 right, 2 vertical)
    Q_INVOKABLE void setMarginText(int which, const QString &text);
    Q_INVOKABLE bool commitAndAdvance();
    Q_INVOKABLE bool nextLine(int direction);
    Q_INVOKABLE bool commit();
    Q_INVOKABLE bool applyChanges();
    Q_INVOKABLE void discard();
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    Q_INVOKABLE bool goToHistory(int step);
    Q_INVOKABLE bool undoToLastSave();
    QStringList history() const;
    int historyCursor() const;
    bool canUndoToLastSave() const;
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool toggleTag(const QString &tag, int selectionStart, int selectionEnd);
    Q_INVOKABLE bool toggleTagIn(int role, const QString &tag, int selectionStart, int selectionEnd);
    Q_INVOKABLE bool splitLine(int role, int selectionStart, int selectionEnd);
    // MUST KILL: type is a magic int (0 at the caret, 1 at the text start, 2 plain text)
    Q_INVOKABLE bool applyTagButton(int role, const QString &tag, int type, int selectionStart, int selectionEnd);
    Q_INVOKABLE QVariantMap beginFont(int role, int selectionStart, int selectionEnd);
    Q_INVOKABLE bool changeFont(const QVariantMap &font);
    // MUST KILL: number is a magic int (1 primary, 2 secondary, 3 outline, 4 shadow), also in switchColour and simplePickerColour
    Q_INVOKABLE QVariantMap beginColour(int number, int role, int selectionStart, int selectionEnd);
    Q_INVOKABLE QVariantMap switchColour(int number);
    Q_INVOKABLE QVariantMap simplePickerColour(int number);
    Q_INVOKABLE bool changeColour(const QVariantMap &colour);
    Q_INVOKABLE void endDialog(bool accepted);
    Q_INVOKABLE bool pasteAllToTranslation();
    Q_INVOKABLE bool pasteSelectionToTranslation(int originalStart, int originalEnd, int translationCaret);
    Q_INVOKABLE bool commentOutOriginal();
    Q_INVOKABLE bool insertTimeDifference(bool fromEnd, int selectionStart, int selectionEnd);
    Q_INVOKABLE void reportFieldSelection(int role, int start, int end);
    std::pair<int, int> fieldSelection() const { return m_fieldSelection[translationMode() ? 1 : 0]; }
    std::pair<int, int> fieldSelectionOf(int role) const { return m_fieldSelection[role == 1 ? 1 : 0]; }
    std::pair<long, long> rawFieldSelection(int role) const;
    // MUST KILL: role -1 (any but 0 or 1) means the edited field
    void selectInField(int start, int end, int role = -1);
    void selectRaw(int role, int start, int end);
    void selectInBox(int role, int start, int end);
    std::pair<int, int> displaySpan(int role, int start, int end) const;
    void reloadFromSession();
    void setVideoTimeSource(std::function<std::optional<std::int64_t>()> source) { m_videoTime = std::move(source); }
    Q_INVOKABLE bool toggleUnconfirmedAndAdvance();
    Q_INVOKABLE bool toggleUnconfirmed();
    bool unconfirmed() const;
    bool moveTags() const { return m_moveTags; }
    void setMoveTags(bool on);
    QVariantList tagRuns() const;
    bool karaokeCarried() const
    {
        const auto tags = runTags();
        return tags && tags->karaoke;
    }
    Q_INVOKABLE bool placeRun(int number, int selectionStart, int selectionEnd);
    Q_INVOKABLE bool acceptSuggestedRuns();
    Q_INVOKABLE bool dismissSuggestedRuns();
    void setVisualToolActive(std::function<bool()> active) { m_visualToolActive = std::move(active); }
    Q_INVOKABLE bool findNextUnconfirmed();
    Q_INVOKABLE bool findNextUntranslated();
    int selectionStart() const { return m_selectionStart; }
    int selectionEnd() const { return m_selectionEnd; }
    int selectionRole() const { return m_selectionRole; }

    void writeFinished();

signals:
    void changed();
    void frameDisplayChanged();
    void lineChanged(qulonglong id);
    // MUST KILL: leftLineCommitted is emitted before changed(), an ordering dependency
    void leftLineCommitted(qulonglong line);
    void selectionRequested();
    void fieldFocusRequested(int role);
    void discarded();

private:
    application::EditSession *session() const;
    // MUST KILL: record() strips the carried tags from the translation, sessionRecord() keeps them
    std::optional<core::LineRecord> record() const;
    std::optional<core::LineRecord> sessionRecord() const;
    std::pair<const core::LineRecord *, const application::DraftChange *> videoLine() const;
    struct Carry {
        core::OriginalTags tags;
        std::u8string leading, trailing;
    };
    std::optional<Carry> carry(const core::LineRecord &line) const;
    const std::vector<core::StyleValues> *carryStyles() const;
    struct CarryStyles {
        using Key = std::tuple<std::uint64_t, const core::Document *, std::uint64_t>;
        Key key;
        std::vector<core::StyleValues> styles;
        bool within = true;
    };
    mutable std::optional<CarryStyles> m_carryStyles;
    std::optional<core::OriginalTags> runTags() const;
    core::TranslationDraft translationDraft(const core::OriginalTags &tags, const core::LineRecord &line) const;
    std::u8string composedBody(const core::OriginalTags &tags, core::TranslationDraft draft, core::LineId line);
    bool saveTranslationDraft(const core::OriginalTags &tags, core::TranslationDraft draft);
    std::u8string recarried(const core::LineRecord &before, const Carry &now);
    std::vector<core::RunPlacement> suggestedRuns(const core::OriginalTags &tags,
                                                  const core::TranslationDraft &draft) const;
    static std::u8string carried(const Carry &carry, std::u8string body);
    std::size_t carriedLead() const;
    std::optional<core::LineId> m_carryEvaluated;
    bool m_moveTags = false;
    std::function<bool()> m_visualToolActive;
    void evaluateCarry();
    bool m_carryFocus = false;
    std::optional<std::tuple<core::LineId, std::u8string, std::u8string>> m_dismissedFor;
    std::set<int> m_dismissedRuns;
    using RunOrderKey = std::pair<std::uint64_t, std::uint64_t>;
    RunOrderKey runOrderKey(core::LineId line) const { return {m_document ? m_document->value : 0, line.value}; }
    struct RunOrder {
        std::u8string translation;
        std::vector<int> order;
        std::shared_ptr<const core::OriginalTags> tags;
        std::uint64_t noted = 0;
    };
    static constexpr std::size_t kRunOrderHistory = 16;
    static constexpr std::size_t kRunOrderUnits = std::size_t(1) << 21;
    std::map<RunOrderKey, std::vector<RunOrder>> m_runOrders;
    std::uint64_t m_runOrderClock = 0;
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
    bool setRaw(int role, std::u8string raw, const std::optional<RunOrder> *orderBefore = nullptr);
    bool editPlacedBody(int selectionStart, int selectionEnd,
                        const std::function<core::legacy::EditorText(core::legacy::EditorText)> &change);
    bool editRaw(int role, int selectionStart, int selectionEnd,
                 const std::function<core::legacy::EditorText(core::legacy::EditorText)> &change);
    std::optional<std::u16string> styleTagValue(std::u16string_view tag) const;
    struct DialogSession {
        int role = 0;
        bool several = false;
        bool ass = true;
        core::legacy::NonAssFormat format = core::legacy::NonAssFormat::Srt;
        core::legacy::EditorText state;
        long position = 0;
        std::u8string original;
        std::size_t draftUndo = 0;
        bool hadDraft = false;
        core::legacy::EditorText opened;
        std::size_t historyCursor = 0;
        core::legacy::FontValues actualFont, editedFont;
        int number = 1;
        core::legacy::TagColour actualColour;
        core::legacy::TagColour editedColour;
    };
    bool beginDialog(int role, int selectionStart, int selectionEnd);
    bool applyDialogToPlacedBody(DialogSession &d,
                                 const std::function<core::legacy::StepResult(const core::legacy::EditorText &, long)> &one);
    bool applyDialogChange(const std::function<core::legacy::StepResult(const core::legacy::EditorText &, long)> &one,
                           const std::function<void(std::u16string &, std::u16string &)> &several);
    void publishRawSelection(const core::legacy::EditorText &state);
    std::optional<core::StyleValues> lineStyle() const;
    std::optional<DialogSession> m_dialog;
    core::legacy::TagColour m_lastColour;
    void edit(int role, const QString &newText, int cursor);
    void placeCaretAfterChange(const QString (&before)[2]);
    const std::u8string &roleText(const core::LineRecord &line, int role) const
    {
        return role == 0 ? line.text : line.translation;
    }
    void committed();
    QString problemText() const;

    Options options() const { return m_options ? m_options() : Options{}; }
    std::optional<core::LineRecord> committedRecord() const;
    // MUST KILL: visual keeps the time fields' warning colour
    bool sendDraft(bool leaving = false, bool visual = false);
    bool visualToolOn() const { return m_visualToolActive && m_visualToolActive(); }
    void rebuildLists();
    void lineShown();
    bool setTime(application::TimeFieldRole role, const QString &text);
    std::function<Options()> m_options;
    std::optional<application::LegacyTimebase> m_timebase;
    bool m_framesAvailable = false;
    bool m_showFrames = false;
    std::function<void(bool)> m_storeShowFrames;
    QStringList m_actors, m_effects;
    int m_alignment = 2;
    bool m_durationEdited = false;
    bool m_timeWarning[3] = {false, false, false};
    // MUST KILL: m_timeWarned is set with m_timeWarning but never cleared
    bool m_timeWarned[3] = {false, false, false};
    void clearTimeWarnings();
    std::optional<QString> m_typedDuration;
    std::optional<QString> m_steppedTime[3];
    std::optional<QString> m_steppedNumber[4];
    bool applyStepped();
    void dropStepped();
    void liveStartEnd(int focused);
    std::optional<core::LineId> m_shownLine;
    core::syntax::TextState m_textState = core::syntax::TextState::Dialogue;
    void updateTextState();
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
    bool m_showTags = true;
    std::function<void(bool)> m_storeShowTags;
    TagListController m_tagList;
    QString m_shown[2];
    QString m_problem;
    QString m_attempted;
    struct Snapshot {
        std::u8string text, translation;
        bool translationTyped = false;
        std::optional<RunOrder> order;
    };
    bool translationTyped();
    std::vector<Snapshot> m_draftUndo;
    std::vector<Snapshot> m_draftRedo;
    std::function<void()> m_onCommitted;
    bool findNext(std::size_t &cursor, const std::function<bool(const core::LineRecord &)> &match,
                  const QString &none);
    std::size_t m_nextUnconfirmed = 0;
    std::size_t m_nextUntranslated = 0;
    int m_selectionStart = 0;
    int m_selectionEnd = 0;
    int m_selectionRole = 0;
    std::function<std::optional<std::int64_t>()> m_videoTime;
    std::pair<int, int> m_fieldSelection[2] = {{0, 0}, {0, 0}};
};

}
