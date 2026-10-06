#pragma once

// T4: the rectangle clip (legacy ClipRect, HikariSub/VisualClipRect.cpp) and
// the vector clip (legacy DrawingAndClip as VECTORCLIP, VisualClips.cpp) at
// 20d647c4 (docs/qt/visual-tools.md, "Clips"). The vector clip edits its
// points with the shared VectorEditor (visual_vector.h), which T5's drawing
// tool reuses.
//
// A Line's text is read and written as legacy did, through the ported
// TagFindReplace::FindTag (core::legacy::TagEditor) and Replace, from the
// start of the text: the Line editor's caret plays no part (approved
// T4-clip-read-start, docs/qt/compatibility-decisions.md).

#include "hikari/application/visual_tools.h"
#include "hikari/application/visual_vector.h"

#include <optional>
#include <string>
#include <string_view>

namespace hikari::application::visual {

namespace clip {

// TagFindReplace::Replace on a FindTag result: the tag replaces the match
// (or goes where FindTag would put it, in a new block when outside one).
// FindTag runs from position 0 (mode 1).
std::u16string replaceTag(std::u16string text, std::u16string_view pattern, std::u16string_view tag);

// ClipRect::SetCurVisual (VisualClipRect.cpp:226-252): the first clip's
// rectangle. `shown` is nullopt when a clip with three commas does not read
// as four numbers (legacy then keeps its previous state).
struct RectangleRead {
    std::optional<bool> shown;
    bool inverse = false;
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
};
RectangleRead readRectangle(std::u16string_view text, int scriptWidth, int scriptHeight);

// ClipRect::ChangeVisual (VisualClipRect.cpp:296-345): "\clip(x1,y1,x2,y2)"
// or "\iclip(...)" with the corners ordered, replacing the first clip.
std::u16string putRectangle(std::u16string text, int x1, int y1, int x2, int y2, bool inverse);

// DrawingAndClip::ChangeVectorVisual for VECTORCLIP (VisualClips.cpp:545-573):
// the body replaces the first vector clip's, keeping \clip or \iclip; a new
// one is \clip. An empty body removes the clip (and every "{}" left). The
// mask's tag (the clip's opposite) is set when `maskTag` is given.
std::u16string putVector(std::u16string text, std::u16string_view body, std::u16string *maskTag = nullptr);

// ClipRect::InvertClip / DrawingAndClip::InvertClip (VisualClipRect.cpp:381-452,
// VisualClips.cpp:1284-1355): the name every clip of the kind is given, the
// opposite of the active Line's last one ("clip" or "iclip"; empty when it
// has none). A rectangle clip's body has three commas or more, a vector
// clip's an "m".
std::u16string invertedName(std::u16string_view activeText, bool vector);
// Renames every clip of the kind in a Line's text; nullopt when none is.
std::optional<std::u16string> renameClips(std::u16string_view text, std::u16string_view name, bool vector);

// The vector clip as DrawingAndClip::SetCurVisual reads it (with
// Visuals::GetPosnScale's scale): the body's points, the drawing scale and
// the mask.
struct VectorRead {
    std::u16string body; // the drawing commands
    std::optional<int> vectorScale; // the clip's scale, when the text gives one
    int divisor = 1;                // 2^(scale-1): the points are in 1/divisor units
    std::u16string maskTag, maskBody; // CreateClipMask (empty: no mask)
};
VectorRead readVector(std::u16string_view text);

// CreateClipMask's tag: the opposite of the text's first clip ("iclip(" for
// a \clip, "clip(" for an \iclip; empty without a clip).
std::u16string maskTag(std::u16string_view text);

// DrawingAndClip::CreateClipMask's Line text (VisualClips.cpp:1260-1282):
// a translucent black rectangle over the script, clipped by the opposite of
// the clip, drawn above everything.
std::u16string maskText(std::u16string_view tag, std::u16string_view body, int scriptWidth, int scriptHeight);

} // namespace clip

class RectangleClipTool : public VisualTool {
public:
    Family family() const override { return Family::RectangleClip; }
    // Legacy sent the clips' edits as visual dummies: no SetVisual followed.
    bool keepsStateAfterCommit() const override { return true; }
    void reset(VisualHost &host) override;
    void pointer(const Pointer &event, VisualHost &host) override;
    bool key(const Key &event, VisualHost &host) override;
    Overlay overlay(const VisualHost &host) const override;
    std::vector<ToolOption> options(const VisualHost &host) const override;
    bool setOption(const std::string &name, int value, VisualHost &host) override;

    // ClipRect's state, as legacy names it.
    PointF corner(int i) const { return m_corner[i]; }
    bool shown() const { return m_showClip; }
    bool inverse() const { return m_invClip; }
    int grabbed() const { return m_grabbed; }
    int hitTest(PointF pos, bool diff, const VisualHost &host);
    void invert(VisualHost &host);

private:
    void apply(VisualHost &host, bool commit);
    std::u16string change(std::u16string text) const;

    PointF m_corner[2]{};
    bool m_invClip = false;
    bool m_showClip = false;
    int m_grabbed = -1;
    PointF m_diffs{};
    bool m_began = false;        // this press or key began the open gesture
    bool m_keyCommit = false;    // a nudge commits on its key's release
};

class VectorClipTool : public VisualTool {
public:
    Family family() const override { return Family::VectorClip; }
    // Legacy sent the clips' edits as visual dummies: no SetVisual followed.
    bool keepsStateAfterCommit() const override { return true; }
    void reset(VisualHost &host) override;
    void pointer(const Pointer &event, VisualHost &host) override;
    bool key(const Key &event, VisualHost &host) override;
    Overlay overlay(const VisualHost &host) const override;
    std::vector<ToolOption> options(const VisualHost &host) const override;
    bool setOption(const std::string &name, int value, VisualHost &host) override;
    std::vector<core::LineRecord> previewLines(const VisualHost &host) const override;

    VectorEditor &editor() { return m_editor; }
    const VectorEditor &editor() const { return m_editor; }
    VectorFrame frame(const VisualHost &host) const;
    int vectorScale() const { return m_vectorScale; }
    // DrawingAndClip::GetVisual for VECTORCLIP: "<scale>," when above 1,
    // then the points in whole units.
    std::u16string body() const;
    std::u16string mask(const VisualHost &host) const;
    void invert(VisualHost &host);

private:
    void apply(VisualHost &host, bool commit);
    VectorEditor::Callbacks callbacks(VisualHost &host);

    VectorEditor m_editor;
    int m_vectorScale = 1;
    float m_scale = 1.f; // GetPosnScale's scale (1 / divisor); coeffW /= scale
    std::u16string m_maskTag, m_maskBody;
    bool m_began = false;
    bool m_inKey = false, m_keyCommit = false;
};

} // namespace hikari::application::visual
