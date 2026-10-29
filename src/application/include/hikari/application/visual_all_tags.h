#pragma once

#include "hikari/application/all_tags.h"
#include "hikari/application/visual_tools.h"
#include "hikari/application/visual_transform.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hikari::application::visual {

// What Set writes on `line` at the definition's starting values, split
// around the written tag.
struct AllTagsSetPreview {
    std::u16string before, tag, after;
};
AllTagsSetPreview allTagsSetPreview(const AllTagsSetting &tag, std::u16string_view line);

class AllTagsTool : public VisualTool {
public:
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

    // changeOf: Insert is Set, Multiply+ is Multiply, every gradient is Gradient.
    enum class Change { Set, Add, Multiply, Gradient };
    static Change changeOf(int pasteMode);
    // Per definition: 0 Common, 1 More, 2 Your tags.
    std::vector<int> sectionsOf(const VisualHost &host) const;
    std::vector<int> listOrder(const VisualHost &host) const;
    // Sliders over the video instead of the inspector strip's values.
    void setLegacySliders(bool on) { m_legacySliders = on; }

    // The change option << 20 plus the definition index.
    int toggled() const { return (m_listMode << 20) + m_listSelection; }
    // Without a host only the state is stored.
    void setToggled(int tool, VisualHost *host = nullptr);
    // Keeps the selection by name where it can; a hidden tool (!shown)
    // only updates the selection.
    void definitionsChanged(const std::vector<std::u16string> &oldNames, VisualHost &host, bool shown = true);

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
    // Moved to the tag while a one-Line gesture runs, given to the editor on release.
    std::pair<long, long> editorCaret() const { return m_find.selection(); }
    const std::u16string &editorText() const { return m_editorText; }

private:
    void changeTool(int tool, bool blockSetCurVisual, VisualHost &host);
    void setCurVisual(VisualHost &host);
    void findTagValues(const transform::Context &ctx);
    void checkTag();
    void checkRange(float val);
    void setupSlidersPosition(int position);
    std::u16string selectedTagIn(transform::FindData *result) const;
    std::u16string visualValue(const std::u16string &curValue) const;
    long changeVisualEditor(std::u16string &text);
    void changeVisualLine(std::u16string &text, std::size_t numOfSelections);
    // True when the wheel stepped the slider.
    bool sliderMouse(int index, const Pointer &event, VisualHost &host);
    void drawSlider(Overlay &out, const VisualHost &host, const Slider &slider) const;
    float diffValue(const Slider &slider) const;
    // False when nothing was staged (dummy) or committed.
    bool setVisual(bool dummy, VisualHost &host);
    bool beginEdit(VisualHost &host);
    bool finishEdit(VisualHost &host);
    bool several(const VisualHost &host) const;
    std::vector<core::LineId> targetsFor(const VisualHost &host) const;
    // The picked Lines less those the Grid hides; a redone last change's own, unfiltered.
    std::vector<core::LineId> editedTargets(const VisualHost &host) const;
    // The active Line is a comment or not shown: no pointer events, no value writes.
    bool blocked(const VisualHost &host) const;
    void legacyPointer(const Pointer &event, VisualHost &host);
    bool legacyKey(const Key &event, VisualHost &host);

    // set, start and to follow the active Line's value until typed; by and perLine start at 0.
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
    void load(const Fields &f);
    // Stages (preview) or commits as one step; true when a step was recorded.
    bool run(const Fields &f, bool commit, VisualHost &host);
    void forgetValues();
    void remember(const Fields &f, const std::array<float, 4> &base, std::pair<long, long> caret, VisualHost &host);
    void dropDrag(VisualHost &host);
    void takeView(const VideoView &view);
    const std::vector<AllTagsSetting> &tags(const VisualHost &host) const;

    int m_listSelection = 0;
    int m_listMode = PasteInsert;

    bool m_legacySliders = false;
    Fields m_fields;
    std::optional<Fields> m_sample; // a drag's or scrub's, until it ends
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
        const EditSession *session = nullptr;
    };
    std::optional<Last> m_last;
    std::optional<std::vector<core::LineId>> m_forcedTargets;
    struct Outlines {
        std::uint64_t revision = 0;
        std::int64_t time = -1;
        std::vector<core::LineId> targets;
        std::u8string draft;
        float coeffW = 0, coeffH = 0;
        PointF zoomMove, zoomScale;
        Overlay overlay;
    };
    mutable std::optional<Outlines> m_outlines;

    float m_coeffW = 1, m_coeffH = 1;
    // MUST KILL: m_videoWidth holds the video rectangle's right edge, not a width
    int m_videoWidth = 0;
    int m_start = 0, m_end = 0;
    bool m_replaceTagsInCursorPosition = false;
    std::u16string m_currentLineText;
    transform::TagFind m_find;
    std::u16string m_editorText;
    bool m_editorIsTranslation = false;
    std::optional<core::LineId> m_editing;
    std::vector<core::LineId> m_targets;
    mutable std::vector<AllTagsSetting> m_ownTags;

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

// `accel` is accelerator text such as "Ctrl-,".
bool keyMatchesAccelerator(const Key &key, std::string_view accel);

}
