#pragma once

// Clip tags are read and written from the start of the Line's text; the editor's caret plays no part.

#include "hikari/application/visual_tools.h"
#include "hikari/application/visual_vector.h"

#include <optional>
#include <string>
#include <string_view>

namespace hikari::application::visual {

namespace clip {

// The tag replaces the first match of `pattern`, or is inserted (in a new block when outside one).
std::u16string replaceTag(std::u16string text, std::u16string_view pattern, std::u16string_view tag);

// The first clip's rectangle. `shown` is nullopt when a clip with three commas does not read as four numbers.
struct RectangleRead {
    std::optional<bool> shown;
    bool inverse = false;
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
};
RectangleRead readRectangle(std::u16string_view text, int scriptWidth, int scriptHeight);

// Replaces the first clip with "\clip(x1,y1,x2,y2)" or "\iclip(...)", corners ordered.
std::u16string putRectangle(std::u16string text, int x1, int y1, int x2, int y2, bool inverse);

// The body replaces the first vector clip's, keeping \clip or \iclip; a new one is \clip. An empty
// body removes the clip and every "{}" left. Sets `maskTag` (the clip's opposite) when given.
std::u16string putVector(std::u16string text, std::u16string_view body, std::u16string *maskTag = nullptr);

// The opposite of the active Line's last clip of the kind ("clip" or "iclip"; empty when it has none).
// A rectangle clip's body has three commas or more, a vector clip's an "m".
std::u16string invertedName(std::u16string_view activeText, bool vector);
// Renames every clip of the kind in a Line's text; nullopt when none is.
std::optional<std::u16string> renameClips(std::u16string_view text, std::u16string_view name, bool vector);

struct VectorRead {
    std::u16string body;
    std::optional<int> vectorScale;
    int divisor = 1;                // 2^(scale-1): the points are in 1/divisor units
    std::u16string maskTag, maskBody; // empty: no mask
};
VectorRead readVector(std::u16string_view text);

// "iclip(" for a first \clip, "clip(" for an \iclip; empty without a clip.
std::u16string maskTag(std::u16string_view text);

// A translucent black rectangle over the script, clipped by the opposite of the clip, drawn above everything.
std::u16string maskText(std::u16string_view tag, std::u16string_view body, int scriptWidth, int scriptHeight);

} // namespace clip

class RectangleClipTool : public VisualTool {
public:
    Family family() const override { return Family::RectangleClip; }
    bool keepsStateAfterCommit() const override { return true; }
    void reset(VisualHost &host) override;
    void pointer(const Pointer &event, VisualHost &host) override;
    bool takesKey(const Key &event) const override;
    bool key(const Key &event, VisualHost &host) override;
    Overlay overlay(const VisualHost &host) const override;
    std::vector<ToolOption> options(const VisualHost &host) const override;
    bool setOption(const std::string &name, int value, VisualHost &host) override;

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
    bool keepsStateAfterCommit() const override { return true; }
    void reset(VisualHost &host) override;
    void pointer(const Pointer &event, VisualHost &host) override;
    bool takesKey(const Key &event) const override;
    bool key(const Key &event, VisualHost &host) override;
    Overlay overlay(const VisualHost &host) const override;
    std::vector<ToolOption> options(const VisualHost &host) const override;
    bool setOption(const std::string &name, int value, VisualHost &host) override;
    std::vector<core::LineRecord> previewLines(const VisualHost &host) const override;

    VectorEditor &editor() { return m_editor; }
    const VectorEditor &editor() const { return m_editor; }
    VectorFrame frame(const VisualHost &host) const;
    int vectorScale() const { return m_vectorScale; }
    // "<scale>," when above 1, then the points in whole units.
    std::u16string body() const;
    std::u16string mask(const VisualHost &host) const;
    void invert(VisualHost &host);

private:
    void apply(VisualHost &host, bool commit);
    VectorEditor::Callbacks callbacks(VisualHost &host);

    VectorEditor m_editor;
    int m_vectorScale = 1;
    float m_scale = 1.f; // 1 / divisor
    std::u16string m_maskTag, m_maskBody;
    bool m_began = false;
    bool m_inKey = false, m_keyCommit = false;
};

} // namespace hikari::application::visual
