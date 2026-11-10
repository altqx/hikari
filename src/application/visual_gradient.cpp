#include "hikari/application/visual_gradient.h"

#include "hikari/application/automation_services.h"
#include "hikari/application/visual_transform.h"

#include "hikari/core/style.h"
#include "hikari/core/text_projection.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace hikari::application::visual {

namespace {

using gradient::Mode;
using transform::Context;
using gradient::Point;
using gradient::Problem;

constexpr std::uint32_t kStripOutline = 0x90FFFFFF;
constexpr std::uint32_t kGuide = 0xFFBB0000;
constexpr std::uint32_t kProblemText = 0xFFFFD54F;
constexpr int kModeCount = 7;

struct ModeInfo {
    std::string_view icon;
    std::u16string_view name;
};
constexpr ModeInfo kModes[kModeCount] = {
    {"gradient-rect", u"Rectangle strips"},     {"gradient-rotated", u"Rotated strips"},
    {"gradient-skew", u"Skewed strips"},        {"gradient-clip-edge", u"Strips along the clip"},
    {"gradient-vector", u"Wedge strips"},       {"gradient-by-char", u"By character"},
    {"gradient-by-line", u"By line"},
};

bool stripMode(Mode m)
{
    return m == Mode::Rect || m == Mode::Rotated || m == Mode::Skew || m == Mode::ClipEdge;
}

bool linearMode(Mode m)
{
    return m == Mode::Rect || m == Mode::Rotated || m == Mode::Skew;
}

Point toScript(const VideoView &v, float x, float y)
{
    return {(double(x) / v.zoomScale().x + v.zoomMove().x) * v.coeffW(),
            (double(y) / v.zoomScale().y + v.zoomMove().y) * v.coeffH()};
}

PointF toView(const VideoView &v, Point p)
{
    return {static_cast<float>(((p.x / v.coeffW()) - v.zoomMove().x) * v.zoomScale().x),
            static_cast<float>(((p.y / v.coeffH()) - v.zoomMove().y) * v.zoomScale().y)};
}

double dot(Point a, Point b)
{
    return a.x * b.x + a.y * b.y;
}

std::u16string number(double value, int decimals = 2)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, value);
    std::string s = buf;
    if (s.find('.') != std::string::npos) {
        while (s.back() == '0')
            s.pop_back();
        if (s.back() == '.')
            s.pop_back();
    }
    if (s == "-0")
        s = "0";
    return std::u16string(s.begin(), s.end());
}

std::string utf8(const std::u8string &text)
{
    return {text.begin(), text.end()};
}

std::u8string u8(const std::string &text)
{
    return {text.begin(), text.end()};
}

int channelCount(lerp::Kind kind)
{
    switch (kind) {
    case lerp::Kind::Colour:
        return 3;
    case lerp::Kind::Point:
        return 2;
    case lerp::Kind::Rect:
        return 4;
    default:
        return 1;
    }
}

std::size_t channelSlot(lerp::Kind kind, int channel)
{
    return static_cast<std::size_t>(kind == lerp::Kind::Colour ? 2 - channel : channel);
}

std::u16string channelLabel(lerp::Kind kind, int channel)
{
    switch (kind) {
    case lerp::Kind::Colour:
        return std::u16string(1, u"RGB"[channel]);
    case lerp::Kind::Point:
        return std::u16string(1, u"XY"[channel]);
    default:
        return number(channel + 1);
    }
}

double channelLimit(lerp::Kind kind)
{
    return kind == lerp::Kind::Colour || kind == lerp::Kind::Alpha ? 255 : kValueLimit;
}

}

const std::vector<GradientTag> &gradientTags()
{
    static const std::vector<GradientTag> tags{
        {"1c", u"Primary colour", 0},  {"2c", u"Secondary colour", 0}, {"3c", u"Outline colour", 0},
        {"4c", u"Shadow colour", 0},   {"alpha", u"Alpha", 0},         {"1a", u"Primary alpha", 0},
        {"2a", u"Secondary alpha", 0}, {"3a", u"Outline alpha", 0},    {"4a", u"Shadow alpha", 0},
        {"bord", u"Border", 1},        {"xbord", u"Border X", 1},      {"ybord", u"Border Y", 1},
        {"shad", u"Shadow", 1},        {"xshad", u"Shadow X", 1},      {"yshad", u"Shadow Y", 1},
        {"blur", u"Blur", 1},          {"be", u"Blur edges", 1},       {"fs", u"Font size", 2},
        {"fsp", u"Letter spacing", 2}, {"fscx", u"Scale X", 2},        {"fscy", u"Scale Y", 2},
        {"frz", u"Rotation Z", 2},     {"frx", u"Rotation X", 2},      {"fry", u"Rotation Y", 2},
        {"fax", u"Shear X", 2},        {"fay", u"Shear Y", 2},
    };
    return tags;
}

std::u16string_view gradientProblemText(Problem problem)
{
    switch (problem) {
    case Problem::NoTags:
        return u"Choose a tag for the gradient";
    case Problem::TooFewStops:
        return u"Pick two or more lines as stops";
    case Problem::StopsDiffer:
        return u"The picked lines must have the same text and tag blocks";
    case Problem::NothingDiffers:
        return u"No tag differs between the picked lines";
    case Problem::NoLength:
        return u"Drag from where the gradient starts to where it ends";
    case Problem::NoArea:
        return u"The line has no text or clip to cover";
    case Problem::NeedsClip:
        return u"Strips along the clip need a clip on the line";
    case Problem::InverseClip:
        return u"Gradients do not work on an inverse clip";
    case Problem::NoText:
        return u"No characters on the line";
    }
    return {};
}

std::vector<GradientTool::Target> GradientTool::targets(const VisualHost &host) const
{
    std::vector<Target> out;
    const EditSession *session = host.session();
    if (!session)
        return out;
    const Context ctx = transform::context(host);
    const auto draft = session->draftRecord();
    for (const auto &id : host.batchTargets()) {
        const core::LineRecord *line = (draft && draft->id == id) ? &*draft : transform::findLine(session->document(), id);
        if (!line)
            continue;
        Target t{*line, {}};
        t.in.text = utf8(line->text);
        t.in.style = transform::lineStyle(ctx, line->style);
        if (const auto box = transform::lineBox(ctx, *line))
            t.in.box = gradient::Rect{box->first.x, box->first.y, box->second.x, box->second.y};
        out.push_back(std::move(t));
    }
    return out;
}

bool GradientTool::needsDrag() const
{
    return linearMode(m_spec.mode) || m_spec.mode == Mode::Wedge;
}

void GradientTool::refresh(const VisualHost &host)
{
    m_preview.clear();
    m_hidden.clear();
    m_replaced.clear();
    m_changed.clear();
    m_problem.reset();
    if (!m_placed || !host.session())
        return;
    const auto ts = targets(host);
    if (ts.empty())
        return;
    std::vector<std::pair<core::LineId, std::vector<std::string>>> replaced;
    std::vector<std::pair<core::LineId, std::string>> changed;
    const auto fail = [&](Problem p) { m_problem = p; };
    if (stripMode(m_spec.mode) && m_spec.pickedLines) {
        std::vector<gradient::LineIn> stops;
        for (const auto &t : ts)
            stops.push_back(t.in);
        const auto strips = gradient::stripTextsFromStops(m_spec, stops);
        if (!strips)
            return fail(strips.error());
        for (std::size_t i = 0; i + 1 < ts.size(); ++i)
            replaced.emplace_back(ts[i].record.id, std::vector<std::string>{});
        replaced.emplace_back(ts.back().record.id, *strips);
    } else {
        for (std::size_t i = 0; i < ts.size(); ++i) {
            const Target &t = ts[i];
            if (stripMode(m_spec.mode)) {
                const auto strips = gradient::stripTexts(m_spec, t.in);
                if (!strips)
                    return fail(strips.error());
                replaced.emplace_back(t.record.id, *strips);
                continue;
            }
            std::expected<std::string, Problem> text = std::unexpected(Problem::NoText);
            if (m_spec.mode == Mode::Wedge) {
                text = gradient::maskText(m_spec, t.in);
            } else if (m_spec.mode == Mode::ByLine) {
                text = gradient::byLineText(m_spec, t.in, i, ts.size());
            } else {
                std::vector<std::string> fields;
                for (const auto &f : core::legacy::styleRawFields(t.in.style))
                    fields.emplace_back(f.begin(), f.end());
                TextMeasurePort *measure = host.textMeasure();
                const gradient::Measure width = [&](const std::string &s) -> std::optional<double> {
                    if (!measure)
                        return std::nullopt;
                    const auto e = measure->measure(fields, s);
                    return e ? std::optional(e->width) : std::nullopt;
                };
                text = gradient::byCharText(m_spec, t.in, width);
            }
            if (!text)
                return fail(text.error());
            changed.emplace_back(t.record.id, *text);
        }
    }
    for (const auto &t : ts) {
        for (const auto &[id, strips] : replaced) {
            if (id != t.record.id)
                continue;
            m_hidden.push_back(id);
            std::vector<std::u8string> texts;
            for (const auto &strip : strips) {
                core::LineRecord copy = t.record;
                copy.comment = false;
                copy.text = u8(strip);
                texts.push_back(copy.text);
                m_preview.push_back(std::move(copy));
            }
            m_replaced.emplace_back(id, std::move(texts));
        }
        for (const auto &[id, text] : changed) {
            if (id != t.record.id)
                continue;
            m_hidden.push_back(id);
            core::LineRecord copy = t.record;
            copy.text = u8(text);
            m_changed.emplace_back(id, copy.text);
            m_preview.push_back(std::move(copy));
        }
    }
}

gradient::TagStops *GradientTool::stopsOf(const std::string &key)
{
    for (auto &t : m_spec.tags)
        if (t.key == key)
            return &t;
    return nullptr;
}

void GradientTool::addTag(std::string_view key, const VisualHost &host)
{
    m_current = std::string(key);
    if (stopsOf(m_current))
        return;
    const lerp::TagSpec *spec = lerp::tagSpec(key);
    if (!spec)
        return;
    gradient::TagStops stops{m_current, {}, {}};
    const auto ts = targets(host);
    if (!ts.empty())
        if (const auto start = gradient::startValue(ts.front().in.text, key, ts.front().in.style))
            stops.from = *start;
    stops.to = stops.from;
    if (spec->kind == lerp::Kind::Colour)
        for (int i = 0; i < 3; ++i)
            stops.to.v[static_cast<std::size_t>(i)] = 255 - stops.from.v[static_cast<std::size_t>(i)];
    else if (spec->kind == lerp::Kind::Alpha)
        stops.to.v[0] = 255 - stops.from.v[0];
    m_spec.tags.push_back(std::move(stops));
}

void GradientTool::seed(VisualHost &host)
{
    const auto ts = targets(host);
    if (ts.empty())
        return;
    const auto clip = gradient::vectorClip(ts.front().in.text);
    if (!clip || clip->size() != 1)
        return;
    const auto &c = clip->front();
    if (m_spec.mode == Mode::Wedge && c.size() == 3) {
        m_spec.mask = {c[0], c[1], c[2]};
        m_maskSet = true;
    } else if (m_spec.mode == Mode::Skew && c.size() == 4) {
        const auto frame = gradient::linearFrame(c[0], c[1]);
        if (!frame)
            return;
        Point edge{c[3].x - c[0].x, c[3].y - c[0].y};
        if (dot(edge, frame->edge) < 0)
            edge = {-edge.x, -edge.y};
        const double skew = std::atan2(dot(edge, frame->dir), dot(edge, frame->edge)) * 180 / std::numbers::pi;
        if (std::abs(skew) >= 90)
            return;
        m_spec.start = c[0];
        m_spec.end = c[1];
        m_spec.skew = skew;
        m_lineSet = true;
    }
}

void GradientTool::place(VisualHost &host)
{
    if (!m_maskSet && !m_lineSet)
        seed(host);
    m_placed = !needsDrag() || (m_spec.mode == Mode::Wedge ? m_maskSet : m_lineSet);
}

void GradientTool::selected(VisualHost &host)
{
    m_grabbed = -1;
    m_lineSet = m_maskSet = false;
    if (m_spec.tags.empty())
        addTag("1c", host);
    place(host);
    refresh(host);
    host.toolChanged();
}

void GradientTool::reset(VisualHost &host)
{
    m_grabbed = -1;
    if (m_spec.tags.empty())
        addTag("1c", host);
    refresh(host);
    host.toolChanged();
}

void GradientTool::targetsChanged(VisualHost &host)
{
    refresh(host);
    host.toolChanged();
}

std::optional<Point> GradientTool::handle(int index) const
{
    if (m_spec.mode == Mode::Wedge) {
        const bool threePoints = m_spec.shape == gradient::MaskShape::Wedge || m_spec.shape == gradient::MaskShape::Box;
        if (index == 0)
            return m_spec.mask.p1;
        if (index == 1)
            return m_spec.mask.p2;
        if (index == 2 && threePoints)
            return m_spec.mask.p3;
        return std::nullopt;
    }
    if (!linearMode(m_spec.mode))
        return std::nullopt;
    if (index == 0)
        return m_spec.start;
    if (index == 1)
        return m_spec.end;
    if (index == 2 && m_spec.mode == Mode::Skew)
        if (const auto frame = gradient::linearFrame(m_spec.start, m_spec.end, m_spec.skew)) {
            const Point mid{(m_spec.start.x + m_spec.end.x) / 2, (m_spec.start.y + m_spec.end.y) / 2};
            const double reach = frame->length / 3;
            return Point{mid.x + frame->edge.x * reach, mid.y + frame->edge.y * reach};
        }
    return std::nullopt;
}

void GradientTool::moveHandle(int index, Point to)
{
    if (m_spec.mode == Mode::Wedge) {
        (index == 0 ? m_spec.mask.p1 : index == 1 ? m_spec.mask.p2 : m_spec.mask.p3) = to;
        return;
    }
    if (index == 0) {
        m_spec.start = to;
    } else if (index == 1) {
        m_spec.end = to;
    } else if (const auto square = gradient::linearFrame(m_spec.start, m_spec.end)) {
        const Point mid{(m_spec.start.x + m_spec.end.x) / 2, (m_spec.start.y + m_spec.end.y) / 2};
        Point arm{to.x - mid.x, to.y - mid.y};
        if (dot(arm, square->edge) < 0)
            arm = {-arm.x, -arm.y};
        const double skew = std::atan2(dot(arm, square->dir), dot(arm, square->edge)) * 180 / std::numbers::pi;
        m_spec.skew = std::clamp(std::round(skew * 10) / 10, -85.0, 85.0);
    }
}

int GradientTool::handleAt(PointF at, const VisualHost &host) const
{
    const float reach = 7 * unitOf(host);
    for (int i = 2; i >= 0; --i)
        if (const auto p = handle(i)) {
            const PointF v = toView(host.view(), *p);
            if (std::abs(v.x - at.x) <= reach && std::abs(v.y - at.y) <= reach)
                return i;
        }
    return -1;
}

void GradientTool::pointer(const Pointer &event, VisualHost &host)
{
    const VideoView &v = host.view();
    const PointF at{static_cast<float>(event.x), static_cast<float>(event.y)};
    const Point script = toScript(v, at.x, at.y);
    if (event.kind == Pointer::Kind::Press && event.button == Pointer::Button::Left) {
        m_fresh = false;
        m_grabbed = m_placed ? handleAt(at, host) : -1;
        if (m_grabbed < 0 && needsDrag()) {
            m_fresh = true;
            m_grabbed = 1;
            if (m_spec.mode == Mode::Wedge) {
                m_spec.mask = {script, script, script};
                m_maskSet = true;
            } else {
                m_spec.start = m_spec.end = script;
                m_lineSet = true;
            }
        }
        m_placed = true;
    } else if (event.kind == Pointer::Kind::Move && event.leftDown && m_grabbed >= 0) {
        moveHandle(m_grabbed, script);
        if (m_fresh && m_spec.mode == Mode::Wedge) {
            const Point base{m_spec.mask.p2.x - m_spec.mask.p1.x, m_spec.mask.p2.y - m_spec.mask.p1.y};
            m_spec.mask.p3 = {m_spec.mask.p2.x - base.y / 2, m_spec.mask.p2.y + base.x / 2};
        }
    } else if (event.kind == Pointer::Kind::Release && event.button == Pointer::Button::Left) {
        m_grabbed = -1;
        m_fresh = false;
    } else if (event.kind == Pointer::Kind::Move && !event.leftDown) {
        const PointerShape shape = m_placed && handleAt(at, host) >= 0 ? PointerShape::Sizing : PointerShape::Arrow;
        if (host.pointerShape() != shape) {
            host.setPointerShape(shape);
            host.toolChanged();
        }
        return;
    } else {
        return;
    }
    refresh(host);
    host.toolChanged();
}

bool GradientTool::takesKey(const Key &event) const
{
    return m_placed && !event.control && !event.alt &&
           (event.key == keys::Return || event.key == keys::Enter);
}

bool GradientTool::key(const Key &event, VisualHost &host)
{
    if (!takesKey(event))
        return false;
    if (!event.release)
        (void)apply(host);
    return true;
}

bool GradientTool::hasPending() const
{
    return m_placed;
}

bool GradientTool::cancelPending(VisualHost &host)
{
    if (!m_placed)
        return false;
    m_placed = false;
    m_grabbed = -1;
    refresh(host);
    host.toolChanged();
    return true;
}

std::vector<core::LineRecord> GradientTool::previewLines(const VisualHost &host) const
{
    (void)host;
    return m_preview;
}

std::vector<core::LineId> GradientTool::hiddenLines(const VisualHost &host) const
{
    (void)host;
    return m_hidden;
}

bool GradientTool::apply(VisualHost &host)
{
    refresh(host);
    if (!m_placed || host.gesture())
        return false;
    if (m_problem) {
        host.bell();
        host.notice(gradientProblemText(*m_problem));
        return false;
    }
    std::vector<core::LineId> ids;
    for (const auto &[id, texts] : m_replaced)
        ids.push_back(id);
    for (const auto &[id, text] : m_changed)
        ids.push_back(id);
    if (ids.empty())
        return false;
    auto begun = host.beginGesture(ids, std::string(familyInfo(Family::Gradient).history));
    if (!begun)
        return false;
    for (const auto &[id, texts] : m_replaced)
        (*begun)->stageReplacement(id, texts);
    for (const auto &[id, text] : m_changed)
        (*begun)->stage(id, text);
    const bool done = host.commitGesture().has_value();
    if (done) {
        m_placed = false;
        m_lineSet = m_maskSet = false;
    }
    refresh(host);
    host.toolChanged();
    return done;
}

Overlay GradientTool::overlay(const VisualHost &host) const
{
    Overlay out;
    out.unit = unitOf(host);
    if (!m_placed)
        return out;
    const VideoView &v = host.view();
    if (stripMode(m_spec.mode) && !m_hidden.empty()) {
        const auto ts = targets(host);
        if (!ts.empty())
            if (const auto shapes = gradient::stripShapes(m_spec, ts.front().in))
                for (const auto &shape : *shapes) {
                    OverlayPolygon outline;
                    outline.fill = 0;
                    outline.border = kStripOutline;
                    for (const auto &contour : shape) {
                        std::vector<PointF> points;
                        for (const auto &p : contour)
                            points.push_back(toView(v, p));
                        if (outline.points.empty())
                            outline.points = std::move(points);
                        else
                            outline.more.push_back(std::move(points));
                    }
                    if (!outline.points.empty())
                        out.polygons.push_back(std::move(outline));
                }
    }
    if (linearMode(m_spec.mode)) {
        out.lines.push_back({toView(v, m_spec.start), toView(v, m_spec.end), 2, kGuide});
        if (const auto skew = handle(2)) {
            const Point mid{(m_spec.start.x + m_spec.end.x) / 2, (m_spec.start.y + m_spec.end.y) / 2};
            out.lines.push_back({toView(v, mid), toView(v, *skew), 1, kGuide});
        }
    } else if (m_spec.mode == Mode::Wedge) {
        out.lines.push_back({toView(v, m_spec.mask.p1), toView(v, m_spec.mask.p2), 2, kGuide});
        if (handle(2))
            out.lines.push_back({toView(v, m_spec.mask.p2), toView(v, m_spec.mask.p3), 1, kGuide});
    }
    for (int i = 0; i < 3; ++i) {
        const auto p = handle(i);
        if (!p)
            continue;
        const PointF at = toView(v, *p);
        transform::drawRect(out, at, m_grabbed == i, 5);
        if (m_spec.mode != Mode::Wedge)
            continue;
        const std::u16string label = u"P" + number(i + 1);
        const auto [w, h] = host.measureLabel(label);
        const int x = static_cast<int>(std::lround(at.x + 8 * out.unit)), y = static_cast<int>(std::lround(at.y - h - 4 * out.unit));
        out.texts.push_back({{x, y, x + w, y + h}, label});
    }
    if (m_problem) {
        const std::u16string text(gradientProblemText(*m_problem));
        const auto [w, h] = host.measureLabel(text);
        const int x = v.videoRect().left + static_cast<int>(8 * out.unit);
        const int y = v.videoRect().top + static_cast<int>(8 * out.unit);
        out.texts.push_back({{x, y, x + w, y + h}, text, kProblemText});
    }
    return out;
}

std::vector<ToolValue> GradientTool::values(const VisualHost &host) const
{
    (void)host;
    std::vector<ToolValue> out;
    const auto add = [&](std::string name, std::u16string label, double value, double step, std::u16string unit = {},
                         std::u16string group = {}) {
        ToolValue v;
        v.name = std::move(name);
        v.label = std::move(label);
        v.text = number(value);
        v.editable = true;
        v.step = step;
        v.unit = std::move(unit);
        v.group = std::move(group);
        out.push_back(std::move(v));
        return &out.back();
    };
    const bool valueStops = m_spec.mode != Mode::Wedge && !(stripMode(m_spec.mode) && m_spec.pickedLines);
    const lerp::TagSpec *spec = m_current.empty() ? nullptr : lerp::tagSpec(m_current);
    const gradient::TagStops *stops = nullptr;
    for (const auto &t : m_spec.tags)
        if (t.key == m_current)
            stops = &t;
    if (valueStops && spec && stops) {
        const int n = channelCount(spec->kind);
        for (const auto &[role, value] : {std::pair{std::string("from"), stops->from}, {std::string("to"), stops->to}}) {
            const std::u16string roleName = role == "from" ? u"From" : u"To";
            for (int c = 0; c < n; ++c) {
                ToolValue *v = add(role + std::to_string(c), n == 1 ? roleName : channelLabel(spec->kind, c),
                                   value.v[channelSlot(spec->kind, c)], 1, {}, n == 1 ? std::u16string() : roleName);
                if (spec->kind == lerp::Kind::Colour && c == 0) {
                    const auto byte = [&](std::size_t i) {
                        return static_cast<std::uint32_t>(std::clamp(std::lround(value.v[i]), 0L, 255L));
                    };
                    v->swatch = 0xFF000000u | (byte(2) << 16) | (byte(1) << 8) | byte(0);
                }
            }
        }
    }
    switch (m_spec.mode) {
    case Mode::Wedge:
        switch (m_spec.shape) {
        case gradient::MaskShape::Wedge:
            add("spacing", u"Spacing", m_spec.spacing, 1, u"px");
            add("thickness", u"Thickness", m_spec.thickness, 0.01);
            break;
        case gradient::MaskShape::Box:
        case gradient::MaskShape::Ring:
            add("spacing", u"Spacing", m_spec.spacing, 1, u"px");
            break;
        case gradient::MaskShape::Star:
            add("spikes", u"Spikes", m_spec.spikes, 1);
            add("centre", u"Centre", m_spec.centre, 0.01);
            break;
        case gradient::MaskShape::Vignette:
            add("spacing", u"Spacing", m_spec.spacing, 1, u"px");
            add("hole", u"Hole", m_spec.hole, 0.01);
            break;
        }
        break;
    case Mode::ByChar:
    case Mode::ByLine:
        add("accel", u"Accel", m_spec.accel, 0.05);
        break;
    default:
        add("strip", u"Strip", m_spec.strip, 0.5, u"px");
        if (linearMode(m_spec.mode))
            add("overlap", u"Overlap", m_spec.overlap, 0.5, u"px");
        add("accel", u"Accel", m_spec.accel, 0.05);
        if (m_spec.mode == Mode::Skew)
            add("skew", u"Skew", m_spec.skew, 1, u"°");
        break;
    }
    return out;
}

bool GradientTool::setValue(const std::string &name, const std::u16string &text, VisualHost &host, ValueEdit edit)
{
    (void)edit;
    double value = 0;
    if (!parseValue(text, value))
        return false;
    const auto role = name.rfind("from", 0) == 0 ? std::string("from") : name.rfind("to", 0) == 0 ? std::string("to") : "";
    if (!role.empty()) {
        gradient::TagStops *stops = stopsOf(m_current);
        const lerp::TagSpec *spec = lerp::tagSpec(m_current);
        if (!stops || !spec)
            return false;
        const int channel = std::atoi(name.c_str() + role.size());
        if (channel < 0 || channel >= channelCount(spec->kind))
            return false;
        const double limit = channelLimit(spec->kind);
        const double lower = limit == 255 ? 0 : -limit;
        (role == "from" ? stops->from : stops->to).v[channelSlot(spec->kind, channel)] = std::clamp(value, lower, limit);
    } else if (name == "strip") {
        m_spec.strip = std::clamp(value, 0.5, 4096.0);
    } else if (name == "overlap") {
        m_spec.overlap = std::clamp(value, 0.0, 4096.0);
    } else if (name == "accel") {
        m_spec.accel = std::clamp(value, 0.01, 100.0);
    } else if (name == "skew") {
        m_spec.skew = std::clamp(value, -85.0, 85.0);
    } else if (name == "spacing") {
        m_spec.spacing = std::clamp(value, 1.0, 4096.0);
    } else if (name == "thickness") {
        m_spec.thickness = std::clamp(value, 0.0, 1.0);
    } else if (name == "spikes") {
        m_spec.spikes = static_cast<int>(std::clamp(std::round(value), 3.0, 360.0));
    } else if (name == "centre") {
        m_spec.centre = std::clamp(value, 0.0, 1.0);
    } else if (name == "hole") {
        m_spec.hole = std::clamp(value, 0.0, 1.0);
    } else {
        return false;
    }
    refresh(host);
    host.toolChanged();
    return true;
}

std::vector<ToolOption> GradientTool::options(const VisualHost &host) const
{
    std::vector<ToolOption> out;
    for (int i = 0; i < kModeCount; ++i) {
        ToolOption mode;
        mode.name = "mode" + std::to_string(i);
        mode.kind = ToolOption::Kind::Toggle;
        mode.iconRole = std::string(kModes[i].icon);
        mode.tooltip = std::u16string(kModes[i].name);
        mode.checked = static_cast<int>(m_spec.mode) == i;
        out.push_back(std::move(mode));
    }
    if (m_spec.mode == Mode::Wedge) {
        ToolOption shape;
        shape.name = "shape";
        shape.kind = ToolOption::Kind::Switch;
        shape.tooltip = u"Mask shape";
        shape.choices = {u"Wedge", u"Box", u"Ring", u"Star", u"Vignette"};
        shape.index = static_cast<int>(m_spec.shape);
        shape.group = u"Shape";
        out.push_back(std::move(shape));
    } else {
        if (stripMode(m_spec.mode)) {
            ToolOption stops;
            stops.name = "stops";
            stops.kind = ToolOption::Kind::Switch;
            stops.tooltip = u"Where the gradient takes its values from";
            stops.choices = {u"Values", u"Picked lines"};
            stops.index = m_spec.pickedLines ? 1 : 0;
            stops.group = u"Stops";
            out.push_back(std::move(stops));
        }
        if (!(stripMode(m_spec.mode) && m_spec.pickedLines)) {
            ToolOption tag;
            tag.name = "tag";
            tag.kind = ToolOption::Kind::Choice;
            tag.iconRole = "tag-list";
            tag.tooltip = u"Tags the gradient changes";
            tag.searchable = true;
            tag.group = u"Tag";
            tag.sectionNames = {u"In the gradient", u"Colour and alpha", u"Border, shadow and blur", u"Text"};
            tag.index = -1;
            const auto &tags = gradientTags();
            for (std::size_t i = 0; i < tags.size(); ++i) {
                bool in = false;
                for (const auto &t : m_spec.tags)
                    in = in || t.key == tags[i].key;
                tag.choices.emplace_back(tags[i].name);
                tag.notes.push_back(u"\\" + std::u16string(tags[i].key.begin(), tags[i].key.end()));
                tag.sections.push_back(in ? 0 : tags[i].section + 1);
                if (tags[i].key == m_current)
                    tag.index = static_cast<int>(i);
            }
            out.push_back(std::move(tag));
            ToolOption remove;
            remove.name = "removeTag";
            remove.kind = ToolOption::Kind::Action;
            remove.iconRole = "remove";
            remove.tooltip = u"Remove the tag from the gradient";
            remove.enabled = !m_current.empty();
            remove.group = u"Tag";
            out.push_back(std::move(remove));
        }
    }
    if (m_spec.mode != Mode::Wedge) {
        ToolOption blend;
        blend.name = "blend";
        blend.kind = ToolOption::Kind::Switch;
        blend.tooltip = u"How colours mix";
        blend.choices = {u"RGB", u"HSL"};
        blend.index = m_spec.blend == lerp::Blend::Hsl ? 1 : 0;
        blend.group = u"Blend";
        blend.afterValues = true;
        out.push_back(std::move(blend));
    }
    if (m_spec.mode == Mode::ByChar) {
        ToolOption steps;
        steps.name = "steps";
        steps.kind = ToolOption::Kind::Switch;
        steps.tooltip = u"How far along the text each character is";
        steps.choices = {u"By width", u"By count"};
        steps.index = m_spec.measured ? 0 : 1;
        steps.group = u"Steps";
        steps.afterValues = true;
        out.push_back(std::move(steps));
    }
    if (stripMode(m_spec.mode)) {
        ToolOption merge;
        merge.name = "merge";
        merge.kind = ToolOption::Kind::Toggle;
        merge.iconRole = "gradient-merge";
        merge.tooltip = u"Merge equal strips";
        merge.checked = m_spec.merge;
        merge.afterValues = true;
        out.push_back(std::move(merge));
    }
    ToolOption apply;
    apply.name = "apply";
    apply.kind = ToolOption::Kind::Action;
    apply.iconRole = "commit";
    apply.tooltip = u"Apply gradient (Enter)";
    apply.enabled = m_placed && !m_problem && !m_hidden.empty() && host.session();
    apply.afterValues = true;
    out.push_back(std::move(apply));
    return out;
}

bool GradientTool::setOption(const std::string &name, int value, VisualHost &host)
{
    if (name.rfind("mode", 0) == 0 && name.size() == 5) {
        const int mode = name[4] - '0';
        if (mode < 0 || mode >= kModeCount)
            return false;
        m_spec.mode = static_cast<Mode>(mode);
        m_grabbed = -1;
        place(host);
    } else if (name == "shape") {
        if (value < 0 || value > static_cast<int>(gradient::MaskShape::Vignette))
            return false;
        m_spec.shape = static_cast<gradient::MaskShape>(value);
    } else if (name == "stops") {
        m_spec.pickedLines = value == 1;
    } else if (name == "tag") {
        const auto &tags = gradientTags();
        if (value < 0 || value >= static_cast<int>(tags.size()))
            return false;
        addTag(tags[static_cast<std::size_t>(value)].key, host);
    } else if (name == "removeTag") {
        std::erase_if(m_spec.tags, [&](const gradient::TagStops &t) { return t.key == m_current; });
        m_current = m_spec.tags.empty() ? std::string() : m_spec.tags.back().key;
    } else if (name == "blend") {
        m_spec.blend = value == 1 ? lerp::Blend::Hsl : lerp::Blend::Rgb;
    } else if (name == "steps") {
        m_spec.measured = value == 0;
    } else if (name == "merge") {
        m_spec.merge = value != 0;
    } else if (name == "apply") {
        return apply(host);
    } else {
        return false;
    }
    refresh(host);
    host.toolChanged();
    return true;
}

}
