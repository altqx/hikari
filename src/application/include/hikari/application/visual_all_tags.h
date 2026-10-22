#pragma once

// T6: the all-tags tool (legacy AllTags, HikariSub/VisualAllTags.cpp, and its
// sliders AllTagsSlider, VisualAllTagsControls.cpp at 20d647c4, with the
// toolbar item AllTagsItem, VideoToolbar.cpp:646-824). The chosen tag
// definition (all_tags.h) gives one slider per value over the video, its
// thumb at the active Line's value (the tag at the Line editor's caret, else
// the Style's); dragging a thumb, clicking the track or turning the wheel
// writes the tag by the toolbar's change option: Add (every tag of the kind
// changes by the slider's move), Insert (the tag put at the caret, or at the
// text's start), Multiply and Multiply+ (each selected Line by its place),
// and the four gradients (by character or by Line). Shift with the wheel
// steps through the definitions; the right button drags the sliders up and
// down. With the Line editor's "Insert difference" keys a \fad or \t takes
// the video's time. Each slider release, track click or wheel step is one
// history step ("Hydra"), and the tool keeps its state afterwards, as legacy
// (Visuals::SetVisual sent it with the visual dummy flag).
//
// HY1 (#271): the sliders leave the video for the inspector strip. The
// change option is a switch (Set, Add, Multiply, Gradient) with the rest
// beside it (Set's placing, Multiply's two ways, the four gradients), and
// the values say what they do: Set's Value, Add's By, Multiply's Start and
// Per line, a gradient's From and To. Typing one (Enter) applies the change
// as one step; a drag sideways anywhere on the video changes the marked one
// (◆) with a live preview and applies it on release; the wheel steps it.
// The legacy engine writes the tags: the values are put on its sliders
// (Set: the thumb; Add: the thumb By past the Line's value; Multiply and the
// gradients: Per line or To - From as the thumb's move, Start or From as the
// base). The picked Lines are outlined and numbered on the video, and the
// last change can be done again with other values in place of its step.
// The legacy sliders stay for the T6 probe's replay (setLegacySliders).

#include "hikari/application/all_tags.h"
#include "hikari/application/visual_tools.h"
#include "hikari/application/visual_transform.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hikari::application::visual {

class AllTagsTool : public VisualTool {
public:
    // AllTagsSlider (VisualAllTagsControls.h:21-57).
    struct Slider {
        bool holding = false;
        float thumbValue = 0.f;
        float firstThumbValue = 0.f;
        float lastThumbValue = 0.f;
        float left = 0, right = 0, bottom = 0, top = 0;
        float x = 0, y = 0;
        int thumbState = 0; // 1 hovered, 2 held
        bool onThumb = false;
        bool onSlider = false;
    };

    Family family() const override { return Family::Hydra; }
    void reset(VisualHost &host) override;
    bool keepsStateAfterCommit() const override { return true; }
    void selected(VisualHost &host) override;
    void pointer(const Pointer &event, VisualHost &host) override;
    bool key(const Key &event, VisualHost &host) override;
    Overlay overlay(const VisualHost &host) const override;
    std::vector<ToolOption> options(const VisualHost &host) const override;
    bool setOption(const std::string &name, int value, VisualHost &host) override;
    std::vector<ToolValue> values(const VisualHost &host) const override;
    bool setValue(const std::string &name, const std::u16string &text, VisualHost &host,
                  ValueEdit edit = ValueEdit::Commit) override;
    bool markValue(const std::string &name, VisualHost &host) override;
    void targetsChanged(VisualHost &host) override;
    std::optional<LastChange> lastChange(const VisualHost &host) const override;
    bool editLastChange(const std::string &name, const std::u16string &text, VisualHost &host) override;
    void dismissLastChange() override { m_last.reset(); }

    // HY1: the switch's entries, and which one a legacy change option is
    // (Insert: Set; Multiply, Multiply+: Multiply; the gradients: Gradient).
    enum class Change { Set, Add, Multiply, Gradient };
    static Change changeOf(int pasteMode);
    // HY1: the tag list's sections (Common, More, Your tags) by definition,
    // and its order (Shift+wheel steps through it).
    std::vector<int> sectionsOf(const VisualHost &host) const;
    std::vector<int> listOrder(const VisualHost &host) const;
    // The T6 probe's replay drives legacy's sliders over the video.
    void setLegacySliders(bool on) { m_legacySliders = on; }

    // AllTagsItem::GetItemToggled: the change option << 20 plus the list's
    // selection (default: Insert, the first definition).
    int toggled() const { return (m_listMode << 20) + m_listSelection; }
    // The toolbar's state as it is (tests and the probe's replay); with a
    // host the tool takes it as a toolbar change does (ChangeTool).
    void setToggled(int tool, VisualHost *host = nullptr);
    // After the definitions changed (the "Tag editing" dialog's OK): the
    // list's names again, its selection kept by name where it can be
    // (HikariChoice::PutArray), and the tool told.
    void definitionsChanged(const std::vector<std::u16string> &oldNames, VisualHost &host);

    // Legacy state, as the probe records it.
    const AllTagsSetting &actualTag() const { return m_actualTag; }
    int currentTag() const { return m_currentTag; }
    int mode() const { return m_mode; }
    int tagKind() const { return m_tagMode; }
    float multiplyCounter() const { return m_multiplyCounter; }
    int sliderPositionY() const { return m_sliderPositionY; }
    int sliderPositionDiff() const { return m_sliderPositionDiff; }
    bool rightHolding() const { return m_rholding; }
    const std::u16string &selectedTag() const { return m_selectedTag; }
    const std::string &floatFormat() const { return m_floatFormat; }
    bool replaceTagsInCursorPosition() const { return m_replaceTagsInCursorPosition; }
    const std::array<Slider, 4> &sliders() const { return m_slider; }
    // The editor's caret as legacy's TextEdit selection: moved to the tag
    // while a one-Line gesture runs and given to the editor on release.
    std::pair<long, long> editorCaret() const { return m_find.selection(); }
    // The text the Line editor would show (the one-Line path's dummy edits).
    const std::u16string &editorText() const { return m_editorText; }

private:
    // AllTags (VisualAllTags.cpp).
    void changeTool(int tool, bool blockSetCurVisual, VisualHost &host);
    void setCurVisual(VisualHost &host);
    void findTagValues(const transform::Context &ctx);
    void checkTag();
    void checkRange(float val);
    void setupSlidersPosition(int position);
    std::u16string selectedTagIn(transform::FindData *result) const; // GetSelectedTag
    std::u16string visualValue(const std::u16string &curValue) const; // GetVisualValue
    long changeVisualEditor(std::u16string &text);                     // ChangeVisual(txt)
    void changeVisualLine(std::u16string &text, std::size_t numOfSelections); // ChangeVisual(txt, dial, n)
    // AllTagsSlider.
    // True when the wheel stepped the slider (held until pointer writes it).
    bool sliderMouse(int index, const Pointer &event, VisualHost &host);
    void drawSlider(Overlay &out, const VisualHost &host, const Slider &slider) const;
    float diffValue(const Slider &slider) const; // GetDiffValue
    // Visuals::SetVisual(dummy) through a gesture: false when nothing was
    // staged (dummy) or committed.
    bool setVisual(bool dummy, VisualHost &host);
    bool beginEdit(VisualHost &host);
    bool finishEdit(VisualHost &host);
    bool several(const VisualHost &host) const; // EditBox::IsCursorOnStart
    std::vector<core::LineId> targetsFor(const VisualHost &host) const;
    void legacyPointer(const Pointer &event, VisualHost &host);
    bool legacyKey(const Key &event, VisualHost &host);

    // HY1: the strip's values. Set's, Start/From and To follow the active
    // Line's value until typed (`base`); By and Per line start at 0.
    struct Fields {
        std::array<std::optional<float>, 4> set, start, to;
        std::array<float, 4> by{}, perLine{};
    };
    int valueCount() const;
    std::u16string valueText(float value) const;
    void readBase(VisualHost &host);
    float field(const Fields &f, const std::string &name, int mode) const;
    bool putField(Fields &f, const std::string &name, float value) const;
    std::string markedName() const;
    bool isMove(const std::string &name) const;
    std::vector<ToolValue> valuesOf(const Fields &f, int mode, const std::array<float, 4> &base,
                                    const std::string &marked, bool editable) const;
    // The values on legacy's sliders; then the change staged (preview) or
    // committed as one step (true when a step was recorded).
    void load(const Fields &f);
    bool run(const Fields &f, bool commit, VisualHost &host);
    void forgetValues();
    // The change just committed as the last change card's (its Lines as
    // edited, the values before it).
    void remember(const Fields &f, const std::array<float, 4> &base, std::pair<long, long> caret, VisualHost &host);
    void dropDrag(VisualHost &host);
    void takeView(const VideoView &view);
    const std::vector<AllTagsSetting> &tags(const VisualHost &host) const;

    // AllTagsItem's state.
    int m_listSelection = 0;
    int m_listMode = PasteInsert;

    // HY1.
    bool m_legacySliders = false;
    Fields m_fields;
    std::optional<Fields> m_sample; // a drag's or a scrub's, until it ends
    std::array<float, 4> m_base{};
    std::string m_marked;
    struct Drag {
        int x = 0, y = 0, atX = 0, atY = 0;
        std::string name;
        float from = 0;
        bool active = false;
    };
    std::optional<Drag> m_drag;
    struct Last {
        int tag = 0;
        int mode = 0;
        Fields fields;
        std::array<float, 4> base{};
        std::vector<core::LineId> targets;
        std::pair<long, long> caret;
        std::uint64_t revision = 0;
        std::optional<core::LineId> active;
        const EditSession *session = nullptr; // another tab's revision can be the same
    };
    std::optional<Last> m_last;
    std::optional<std::vector<core::LineId>> m_forcedTargets; // the last change's, done again
    // The outlines of the Lines shown, kept until what they depend on changes.
    struct Outlines {
        std::uint64_t revision = 0;
        std::int64_t time = -1;
        std::vector<core::LineId> targets;
        float coeffW = 0, coeffH = 0;
        PointF zoomMove, zoomScale;
        Overlay overlay;
    };
    mutable std::optional<Outlines> m_outlines;

    // Visuals members.
    float m_coeffW = 1, m_coeffH = 1;
    int m_videoWidth = 0; // VideoSize.width: the video rectangle's right edge
    int m_start = 0, m_end = 0;
    bool m_replaceTagsInCursorPosition = false;
    std::u16string m_currentLineText;
    transform::TagFind m_find;
    std::u16string m_editorText;
    bool m_editorIsTranslation = false;
    std::optional<core::LineId> m_editing;
    std::vector<core::LineId> m_targets; // the gesture's, shown in the Grid
    mutable std::vector<AllTagsSetting> m_ownTags; // without a host's definitions: legacy's defaults

    // AllTags members (Visuals.h:482-519).
    AllTagsSetting m_actualTag;
    std::string m_floatFormat = "5.3f";
    std::u16string m_selectedTag;
    std::array<Slider, 4> m_slider{};
    bool m_rholding = false;
    int m_currentTag = 0;
    int m_sliderPositionY = -1;
    int m_sliderPositionDiff = 0;
    int m_increase = 70;
    int m_mode = 0;
    float m_multiplyCounter = 0;
    int m_lastTool = -1;
    int m_tagMode = 0;
};

// The key text of an accelerator ("Ctrl-,") matched against a key as
// Hotkeys::GetHKey's flags and key code compare (VisualAllTags.cpp:90-93).
bool keyMatchesAccelerator(const Key &key, std::string_view accel);

} // namespace hikari::application::visual
