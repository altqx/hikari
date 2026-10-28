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

#include "hikari/application/all_tags.h"
#include "hikari/application/visual_tools.h"
#include "hikari/application/visual_transform.h"

#include <array>
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
    // Visuals::SetVisual(dummy) through a gesture.
    void setVisual(bool dummy, VisualHost &host);
    bool beginEdit(VisualHost &host);
    bool finishEdit(VisualHost &host);
    bool several(VisualHost &host) const; // EditBox::IsCursorOnStart
    void takeView(const VideoView &view);
    const std::vector<AllTagsSetting> &tags(const VisualHost &host) const;

    // AllTagsItem's state.
    int m_listSelection = 0;
    int m_listMode = PasteInsert;

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
