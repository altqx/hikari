#include "hikari/application/visual_text_on_path.h"

#include "hikari/application/grid_split.h"
#include "hikari/application/visual_clip.h"
#include "hikari/application/visual_transform.h"

#include "hikari/core/text_projection.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

namespace hikari::application::visual {

namespace {

using text_on_path::Align;

constexpr std::uint32_t kNoticeText = 0xFFFFD54F;
constexpr int kEditorModes = 4;
constexpr int kEditorModeOf[kEditorModes] = {VectorEditor::Drag, VectorEditor::Line, VectorEditor::Bezier,
                                             VectorEditor::Delete};

std::string utf8(std::u16string_view text)
{
    const std::u8string out = core::toUtf8(text);
    return {out.begin(), out.end()};
}

std::u8string u8(const std::string &text)
{
    return {text.begin(), text.end()};
}

std::u16string number(double value)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.1f", value);
    std::string s = buf;
    if (s.ends_with(".0"))
        s.resize(s.size() - 2);
    return core::toUtf16(u8(s));
}

std::optional<core::LineRecord> targetLine(const VisualHost &host)
{
    const EditSession *session = host.session();
    const auto active = host.activeLine();
    if (!session || !active)
        return std::nullopt;
    if (auto draft = session->draftRecord(); draft && draft->id == *active)
        return draft;
    if (const core::LineRecord *line = transform::findLine(session->document(), *active))
        return *line;
    return std::nullopt;
}

}

std::u16string_view textOnPathProblemText(TextOnPathTool::Problem problem)
{
    switch (problem) {
    case TextOnPathTool::Problem::NoPath:
        return u"Draw a path, or give the line a vector clip";
    case TextOnPathTool::Problem::NoText:
        return u"No text to place";
    case TextOnPathTool::Problem::NoMeasure:
        return u"Text can’t be measured here";
    case TextOnPathTool::Problem::NoFrames:
        return u"Slide needs the video’s frames";
    }
    return {};
}

VectorFrame TextOnPathTool::frame(const VisualHost &host) const
{
    const VideoView &v = host.view();
    VectorFrame f;
    f.unit = unitOf(host);
    f.coeffW = static_cast<float>(v.coeffW() * m_divisor);
    f.coeffH = static_cast<float>(v.coeffH() * m_divisor);
    f.zoomMove = v.zoomMove();
    f.zoomScale = v.zoomScale();
    f.videoRect = v.videoRect();
    return f;
}

VectorEditor::Callbacks TextOnPathTool::callbacks(VisualHost &host)
{
    VectorEditor::Callbacks cb;
    cb.apply = [this, &host](bool) { refresh(host); };
    cb.bell = [&host] { host.bell(); };
    cb.notice = [&host](std::u16string_view text) { host.notice(text); };
    return cb;
}

void TextOnPathTool::reset(VisualHost &host)
{
    m_editor.endDrag();
    m_editor.points.clear();
    m_divisor = 1;
    if (const auto line = targetLine(host)) {
        const clip::VectorRead read = clip::readVector(core::toUtf16(line->text));
        m_divisor = read.divisor;
        m_editor.points = parseVectorPoints(read.body);
        m_editor.normaliseFirst();
    }
    refresh(host);
    host.toolChanged();
}

void TextOnPathTool::selected(VisualHost &host)
{
    reset(host);
}

void TextOnPathTool::targetsChanged(VisualHost &host)
{
    const auto active = host.activeLine();
    if (active != m_target)
        reset(host);
}

void TextOnPathTool::refresh(VisualHost &host)
{
    m_preview.clear();
    m_copies.clear();
    m_overflow = 0;
    m_problem.reset();
    const auto line = targetLine(host);
    m_target = line ? std::optional(line->id) : std::nullopt;
    if (!line)
        return;
    const auto path = text_on_path::Path::fromDrawing(utf8(serializeVectorPoints(m_editor.points, kClipPointFormat)),
                                                      m_divisor);
    if (path.empty()) {
        m_problem = Problem::NoPath;
        return;
    }
    TextMeasurePort *measure = host.textMeasure();
    if (!measure) {
        m_problem = Problem::NoMeasure;
        return;
    }
    const transform::Context ctx = transform::context(host);
    const core::StyleValues style = transform::lineStyle(ctx, line->style);
    const auto units = text_on_path::units(
        std::string(line->text.begin(), line->text.end()),
        [&](std::string_view raw) { return taggedTextWidth(*measure, style, core::toUtf16(u8(std::string(raw)))); },
        m_options.rotate);
    if (std::ranges::none_of(units, [](const auto &u) { return !u.space; })) {
        m_problem = Problem::NoText;
        return;
    }
    const auto add = [&](const text_on_path::Layout &layout, std::optional<core::DocumentTime> start,
                         std::optional<core::DocumentTime> end) {
        m_overflow = std::max(m_overflow, layout.overflow);
        for (const auto &glyph : layout.glyphs)
            m_copies.push_back({u8(text_on_path::glyphText(units[glyph.unit], glyph, m_options.rotate)), start, end});
    };
    if (!m_slide) {
        add(text_on_path::place(path, units, m_options), std::nullopt, std::nullopt);
    } else {
        const LegacyTimebase timebase = host.timebase();
        constexpr std::int64_t kUsPerMs = 1000;
        const int startMs = int(line->start.value.microseconds() / kUsPerMs);
        const int endMs = int(line->end.value.microseconds() / kUsPerMs);
        const int first = timebase.empty() ? 0 : timebase.frameAt(startMs);
        const int last = timebase.empty() ? 0 : timebase.frameAt(endMs);
        const int steps = text_on_path::slideSteps(last - first, m_every);
        if (steps == 0) {
            m_problem = Problem::NoFrames;
            return;
        }
        for (int j = 0; j < steps; ++j) {
            const double u = steps > 1 ? double(j) / (steps - 1) : 0;
            const auto at = [&](int frame) { return core::DocumentTime(std::int64_t{timebase.startTimeFor(frame)} * kUsPerMs); };
            add(text_on_path::place(path, units, m_options, u), j == 0 ? line->start.value : at(first + j * m_every),
                j + 1 == steps ? line->end.value : at(first + (j + 1) * m_every));
        }
    }
    for (const Replacement &copy : m_copies) {
        core::LineRecord shown = *line;
        shown.comment = false;
        shown.text = copy.text;
        shown.start.value = copy.start.value_or(line->start.value);
        shown.end.value = copy.end.value_or(line->end.value);
        m_preview.push_back(std::move(shown));
    }
}

void TextOnPathTool::pointer(const Pointer &event, VisualHost &host)
{
    if (event.kind == Pointer::Kind::Wheel) {
        if (event.control || event.wheelSteps == 0)
            return;
        m_editor.wheel(event.wheelSteps);
        host.toolChanged();
        return;
    }
    m_editor.pointer(event, frame(host), callbacks(host));
    m_editor.normaliseFirst();
    refresh(host);
    host.toolChanged();
}

bool TextOnPathTool::takesKey(const Key &event) const
{
    if (!event.control && !event.alt && (event.key == keys::Return || event.key == keys::Enter))
        return !m_copies.empty();
    return nudgeDirection(event).has_value();
}

bool TextOnPathTool::key(const Key &event, VisualHost &host)
{
    if (event.key == keys::Return || event.key == keys::Enter) {
        if (!takesKey(event))
            return false;
        if (!event.release)
            (void)apply(host);
        return true;
    }
    if (event.release)
        return isArrowKey(event.key);
    const bool used = m_editor.key(event, frame(host), callbacks(host));
    m_editor.normaliseFirst();
    if (used) {
        refresh(host);
        host.toolChanged();
    }
    return used;
}

Overlay TextOnPathTool::overlay(const VisualHost &host) const
{
    Overlay out;
    out.unit = unitOf(host);
    m_editor.draw(out, frame(host));
    const IntRect video = host.view().videoRect();
    std::u16string notice;
    if (m_problem && *m_problem != Problem::NoPath)
        notice = textOnPathProblemText(*m_problem);
    else if (m_overflow >= 0.5)
        notice = u"The text is longer than the path. The last graphemes continue along its end.";
    if (!notice.empty()) {
        const auto [w, h] = host.measureLabel(notice);
        const int margin = static_cast<int>(12 * out.unit);
        out.texts.push_back({{video.left + margin, video.bottom - margin - h, video.left + margin + w, video.bottom - margin}, notice, kNoticeText});
    }
    return out;
}

int TextOnPathTool::lineCount() const
{
    return static_cast<int>(m_copies.size());
}

std::vector<ToolValue> TextOnPathTool::values(const VisualHost &host) const
{
    (void)host;
    std::vector<ToolValue> out;
    ToolValue offset;
    offset.name = "offset";
    offset.label = u"Offset";
    offset.text = number(m_options.offset);
    offset.editable = true;
    offset.unit = u"px";
    out.push_back(std::move(offset));
    if (m_slide) {
        ToolValue every;
        every.name = "every";
        every.label = u"Frames a step";
        every.text = number(m_every);
        every.editable = true;
        out.push_back(std::move(every));
    }
    if (m_overflow >= 0.5) {
        ToolValue past;
        past.name = "overflow";
        past.label = u"Past the end";
        past.text = number(m_overflow);
        past.unit = u"px";
        out.push_back(std::move(past));
    }
    ToolValue lines;
    lines.name = "lines";
    lines.label = u"Lines";
    lines.text = core::toUtf16(u8(std::to_string(lineCount())));
    out.push_back(std::move(lines));
    return out;
}

bool TextOnPathTool::setValue(const std::string &name, const std::u16string &text, VisualHost &host, ValueEdit edit)
{
    (void)edit;
    double value = 0;
    if (!parseValue(text, value))
        return false;
    if (name == "offset")
        m_options.offset = value;
    else if (name == "every" && value >= 1)
        m_every = static_cast<int>(std::lround(value));
    else
        return false;
    refresh(host);
    host.toolChanged();
    return true;
}

std::vector<ToolOption> TextOnPathTool::options(const VisualHost &host) const
{
    (void)host;
    std::vector<ToolOption> out;
    static constexpr struct {
        const char *icon;
        const char16_t *label;
    } kPoints[kEditorModes] = {{"vector-drag", u"Move points"},
                               {"vector-line", u"Add line"},
                               {"vector-bezier", u"Add Bézier curve"},
                               {"vector-delete", u"Delete point"}};
    for (int i = 0; i < kEditorModes; ++i) {
        ToolOption mode;
        mode.name = "mode" + std::to_string(i);
        mode.iconRole = kPoints[i].icon;
        mode.tooltip = kPoints[i].label;
        mode.checked = m_editor.mode == kEditorModeOf[i];
        mode.group = u"Points";
        out.push_back(std::move(mode));
    }
    static constexpr struct {
        Align align;
        const char *icon;
        const char16_t *label;
    } kAligns[] = {{Align::Left, "align-left", u"Left"},
                   {Align::Centre, "align-center", u"Centre"},
                   {Align::Right, "align-right", u"Right"},
                   {Align::Spread, "distribute-horizontal", u"Spread"}};
    for (int i = 0; i < 4; ++i) {
        ToolOption align;
        align.name = "align" + std::to_string(i);
        align.iconRole = kAligns[i].icon;
        align.tooltip = kAligns[i].label;
        align.checked = m_options.align == kAligns[i].align;
        align.enabled = !m_slide;
        align.group = u"Layout";
        out.push_back(std::move(align));
    }
    const auto toggle = [&](std::string name, const char *icon, std::u16string label, bool on) {
        ToolOption t;
        t.name = std::move(name);
        t.iconRole = icon;
        t.tooltip = std::move(label);
        t.checked = on;
        t.group = u"Layout";
        out.push_back(std::move(t));
    };
    toggle("rotate", "tool-rotate-z", u"Rotate", m_options.rotate);
    toggle("reverse", "swap-lines", u"Reverse", m_options.reverse);
    toggle("slide", "media-play", u"Slide", m_slide);
    ToolOption commit;
    commit.name = "apply";
    commit.kind = ToolOption::Kind::Action;
    commit.iconRole = "commit";
    commit.tooltip = u"Apply";
    commit.enabled = !m_copies.empty();
    commit.afterValues = true;
    out.push_back(std::move(commit));
    return out;
}

bool TextOnPathTool::setOption(const std::string &name, int value, VisualHost &host)
{
    if (name == "apply")
        return apply(host);
    if (name.size() == 5 && name.starts_with("mode") && name[4] >= '0' && name[4] < '0' + kEditorModes)
        m_editor.mode = kEditorModeOf[name[4] - '0'];
    else if (name.size() == 6 && name.starts_with("align") && name[5] >= '0' && name[5] <= '3')
        m_options.align = static_cast<Align>(name[5] - '0');
    else if (name == "rotate")
        m_options.rotate = value != 0;
    else if (name == "reverse")
        m_options.reverse = value != 0;
    else if (name == "slide")
        m_slide = value != 0;
    else
        return false;
    refresh(host);
    host.toolChanged();
    return true;
}

bool TextOnPathTool::hasPending() const
{
    return !m_copies.empty();
}

bool TextOnPathTool::cancelPending(VisualHost &host)
{
    if (m_copies.empty())
        return false;
    m_editor.points.clear();
    refresh(host);
    host.toolChanged();
    return true;
}

std::vector<core::LineRecord> TextOnPathTool::previewLines(const VisualHost &host) const
{
    (void)host;
    return m_preview;
}

std::vector<core::LineId> TextOnPathTool::hiddenLines(const VisualHost &host) const
{
    (void)host;
    if (m_copies.empty() || !m_target)
        return {};
    return {*m_target};
}

bool TextOnPathTool::apply(VisualHost &host)
{
    refresh(host);
    if (m_problem || m_copies.empty() || !m_target) {
        host.bell();
        if (m_problem)
            host.notice(textOnPathProblemText(*m_problem));
        return false;
    }
    auto begun = host.beginGesture({*m_target}, std::string(familyInfo(Family::TextOnPath).history));
    if (!begun) {
        host.bell();
        return false;
    }
    (*begun)->stageReplacement(*m_target, m_copies);
    const bool done = host.commitGesture().has_value();
    if (done) {
        m_editor.points.clear();
        refresh(host);
    }
    host.toolChanged();
    return done;
}

}
