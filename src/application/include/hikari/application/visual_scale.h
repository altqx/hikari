#pragma once

// T3: the Scale family (legacy Scale, VisualScale.cpp at 20d647c4, and its
// toolbar item ScaleItem, VideoToolbar.cpp:900-992). Dragging the arrows
// writes \fscx and \fscy (left: width, right: height, middle or Shift+left
// or "Preserve proportions": both, linked); the rectangle mode scales the
// text to a drawn rectangle on one or both axes, against the text's measured
// size or a second rectangle drawn with the right button ("Set a custom
// rectangle for the current scale"); "Change all scale tags" multiplies
// every \fscx/\fscy of the Line; "Preserve proportions" also moves the
// several targets' positions and clips about the active Line's position.
// W/A/S/D nudge the arrows (Shift: a tenth), each press its own step.

#include "hikari/application/visual_tools.h"
#include "hikari/application/visual_transform.h"

#include <array>
#include <optional>

namespace hikari::application::visual {

class ScaleTool : public VisualTool {
public:
    Family family() const override { return Family::Scale; }
    void reset(VisualHost &host) override;
    bool keepsStateAfterCommit() const override { return true; }
    void selected(VisualHost &host) override;
    void pointer(const Pointer &event, VisualHost &host) override;
    bool key(const Key &event, VisualHost &host) override;
    Overlay overlay(const VisualHost &host) const override;
    std::vector<ToolValue> values(const VisualHost &host) const override;
    std::vector<ToolOption> options(const VisualHost &host) const override;
    bool setOption(const std::string &name, int value, VisualHost &host) override;

    // ScaleItem::GetItemToggled: bit 1 rectangle, 2 width, 4 aspect ratio,
    // 8 height, 16 custom rectangle, 32 change all tags, 64 preserve
    // proportions (default 2 | 4).
    int toggled() const;
    // The toolbar's toggles as they are (tests and the probe's replay); with
    // a host the tool takes them as a toolbar click does (ChangeTool).
    void setToggled(int bits, VisualHost *host = nullptr);

    // Legacy state, as the probe records it.
    PointF from() const { return m_from; }
    PointF to() const { return m_to; }
    PointF scale() const { return m_scale; }
    PointF arrowLengths() const { return m_arrowLengths; }
    const std::array<PointF, 4> &sizingRectangle() const { return m_sizingRectangle; }
    PointF originalSize() const { return m_originalSize; }
    PointF border() const { return m_border; }
    bool rectangleVisible() const { return m_rectangleVisible; }
    bool originalRectangleVisible() const { return m_originalRectangleVisible; }
    // Legacy's editor caret (TextEdit's selection): FindTag's, moved to the
    // tag while a one-Line gesture runs and given to the editor on release.
    std::pair<long, long> editorCaret() const { return m_find.selection(); }

private:
    // Scale::ChangeTool / SetCurVisual / SetVisual (Visuals::SetVisual).
    void changeTool(int tool, bool blockSetCurVisual, VisualHost &host);
    void setCurVisual(VisualHost &host);
    void setVisual(bool dummy, VisualHost &host);
    void changeVisualLine(std::u16string &text, const core::LineRecord &line, const transform::Context &context);
    std::pair<long, long> changeVisualEditor(std::u16string &text);
    int hitTest(PointF pos, bool originalRect, bool diff);
    void sortPoints();
    void setScale();
    PointF scaleToVideo(PointF point, const VideoView &view) const;
    void setSecondRectScale(const VideoView &view);
    bool beginEdit(VisualHost &host);
    bool finishEdit(VisualHost &host);

    std::array<bool, 7> m_toggle{false, true, true, false, false, false, false};

    // Visuals members: the coefficients and zoom of the last SizeChanged and
    // SetZoom (the view's, refreshed on every event).
    void takeView(const VideoView &view);
    float m_coeffW = 1, m_coeffH = 1;
    PointF m_viewZoomMove{0.f, 0.f}, m_viewZoomScale{1.f, 1.f};
    mutable PointF m_from, m_to;
    mutable double m_moveValues[7] = {0, 0, 0, 0, 0, 0, 0};
    mutable int m_oldtime = 0;
    int m_start = 0, m_end = 0;
    bool m_replaceTagsInCursorPosition = true;
    std::u16string m_currentLineText;
    transform::TagFind m_find;
    // The editor's text and caret while a one-Line gesture runs (legacy's
    // dummy edits go into the editor).
    std::u16string m_editorText;
    bool m_editorIsTranslation = false;
    std::optional<core::LineId> m_editing;

    // Scale members (Visuals.h:362-403).
    int m_type = 0;
    int m_grabbed = -1;
    int m_an = 2;
    PointF m_scale{1.f, 1.f};
    PointF m_lastScale{1.f, 1.f};
    PointF m_originalScale{1.f, 1.f};
    PointF m_diffs{0.f, 0.f};
    bool m_wasUsedShift = false;
    PointF m_arrowLengths{100.f, 100.f};
    std::array<PointF, 4> m_sizingRectangle{};
    PointF m_originalSize{0.f, 0.f};
    PointF m_border{0.f, 0.f};
    bool m_hasScaleToRectangle = false;
    bool m_hasOriginalRectangle = false;
    bool m_hasScaleX = false;
    bool m_hasScaleY = false;
    bool m_preserveAspectRatio = false;
    bool m_rectangleVisible = false;
    bool m_originalRectangleVisible = false;
    bool m_rightHolding = false;
    bool m_preserveProportions = false;
    bool m_changeAllTags = false;
};

} // namespace hikari::application::visual
