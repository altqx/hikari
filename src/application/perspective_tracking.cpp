// Ports arch1t3cht's Perspective-Motion 0.4.1, under its licence:
//
// MIT License
//
// Copyright (c) 2022 arch1t3cht
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "hikari/application/perspective_tracking.h"

#include "hikari/application/grid_split.h"
#include "hikari/application/resample.h"
#include "hikari/application/visual_transform.h"

#include "hikari/core/extradata.h"
#include "hikari/core/style.h"
#include "hikari/core/text_projection.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <charconv>
#include <map>
#include <set>

namespace hikari::application {

namespace persp = visual::perspective;
using visual::PointF;

namespace {

std::vector<std::u8string_view> words(std::u8string_view line)
{
    std::vector<std::u8string_view> out;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == u8' ' || line[i] == u8'\t' || line[i] == u8'\r'))
            ++i;
        const std::size_t start = i;
        while (i < line.size() && line[i] != u8' ' && line[i] != u8'\t' && line[i] != u8'\r')
            ++i;
        if (i > start)
            out.push_back(line.substr(start, i - start));
    }
    return out;
}

std::optional<double> number(std::u8string_view text)
{
    const auto *first = reinterpret_cast<const char *>(text.data());
    double value = 0;
    const auto [end, error] = std::from_chars(first, first + text.size(), value);
    if (error != std::errc{} || end != first + text.size())
        return std::nullopt;
    return value;
}

std::u8string joined(const std::vector<std::u8string_view> &parts)
{
    std::u8string out;
    for (const auto part : parts)
        out += (out.empty() ? u8"" : u8" ") + std::u8string(part);
    return out;
}

std::optional<std::vector<PointF>> corner(const std::vector<std::u8string_view> &lines, std::u8string_view marker)
{
    const std::u8string header = u8"Effects CC Power Pin #1 CC Power Pin-" + std::u8string(marker);
    std::optional<std::size_t> at;
    for (std::size_t i = 0; i < lines.size(); ++i)
        if (joined(words(lines[i])) == header) {
            if (at)
                return std::nullopt;
            at = i;
        }
    if (!at)
        return std::nullopt;
    std::vector<PointF> out;
    for (std::size_t i = *at + 2; i < lines.size(); ++i) {
        const auto line = lines[i];
        if (line.empty() || (line.front() != u8' ' && line.front() != u8'\t'))
            break;
        const auto values = words(line);
        if (values.empty() || values.front().front() < u8'0' || values.front().front() > u8'9')
            break;
        const auto x = values.size() > 1 ? number(values[1]) : std::nullopt;
        const auto y = values.size() > 2 ? number(values[2]) : std::nullopt;
        if (!x || !y)
            return std::nullopt;
        out.push_back({static_cast<float>(*x), static_cast<float>(*y)});
    }
    return out;
}

std::optional<double> setting(const std::vector<std::u8string_view> &lines, std::u8string_view name)
{
    for (const auto line : lines) {
        const auto parts = words(line);
        if (parts.size() < 2)
            continue;
        const std::vector<std::u8string_view> label(parts.begin(), parts.end() - 1);
        if (joined(label) == name)
            return number(parts.back());
    }
    return std::nullopt;
}

int msOf(core::DocumentTime t)
{
    return static_cast<int>(t.microseconds() / 1000);
}

std::vector<const core::LineRecord *> shownSelected(const EditSession &s, const LineVisible &visible)
{
    std::vector<const core::LineRecord *> out;
    for (const auto *l : s.document().lines())
        if (s.selection().selected.contains(l->id) && l->visibility != core::LineVisibility::Hidden &&
            (!visible || visible(l->id)))
            out.push_back(l);
    return out;
}

int rowOf(const core::Document &d, core::LineId id)
{
    int row = 0;
    for (const auto *l : d.lines()) {
        ++row;
        if (l->id == id)
            return row;
    }
    return 0;
}

int layoutResY(const core::Document &d)
{
    int value = 0;
    const auto text = d.scriptInfo(u8"LayoutResY").value_or(u8"");
    const auto *first = reinterpret_cast<const char *>(text.data());
    std::from_chars(first, first + text.size(), value);
    return value;
}

PointF alignmentShift(int an)
{
    return {(an - 1) % 3 / 2.f, 1.f - (an - 1) / 3 / 2.f};
}

persp::Quad rectAt(const persp::Quad &plane, PointF uvPos, int an, PointF size)
{
    const PointF shift = alignmentShift(an);
    persp::Quad out{PointF{0, 0}, PointF{1, 0}, PointF{1, 1}, PointF{0, 1}};
    for (PointF &p : out)
        p = persp::fromUV(plane, {(p.x - shift.x) * size.x + uvPos.x, (p.y - shift.y) * size.y + uvPos.y});
    return out;
}

std::optional<persp::Transform> fitToPlane(const persp::LineState &line, const persp::Quad &plane,
                                           persp::OrgMode mode, double screenZ)
{
    const persp::Transform &t = line.transform;
    const persp::Quad source = persp::textToQuad(t, line.box, screenZ);
    const PointF uvPos = persp::toUV(plane, t.pos);
    const auto unit = persp::quadToTransform(rectAt(plane, uvPos, t.an, {1, 1}), line.box, mode, t, screenZ);
    if (!unit || !(unit->fscx != 0) || !(unit->fscy != 0))
        return std::nullopt;
    const persp::Quad target =
        rectAt(plane, uvPos, t.an, {static_cast<float>(t.fscx / unit->fscx), static_cast<float>(t.fscy / unit->fscy)});
    const PointF shift = alignmentShift(t.an);
    const float w = static_cast<float>(line.box.width()), h = static_cast<float>(line.box.height());
    const float left = t.pos.x - shift.x * w, top = t.pos.y - shift.y * h;
    const persp::Quad flat{PointF{left, top}, PointF{left + w, top}, PointF{left + w, top + h}, PointF{left, top + h}};
    persp::Quad onPlane;
    for (std::size_t i = 0; i < 4; ++i)
        onPlane[i] = persp::fromUV(target, persp::toUV(flat, source[i]));
    auto out = persp::quadToTransform(onPlane, line.box, mode, *unit, screenZ);
    if (out) {
        out->bord = t.bord;
        out->shad = t.shad;
    }
    return out;
}

struct Part {
    core::LineRecord line;
    int frame = 0;
    std::size_t relativePart = 0;
};

}

std::optional<PowerPin> parsePowerPin(std::u8string_view text)
{
    std::vector<std::u8string_view> lines;
    for (std::size_t start = 0; start <= text.size();) {
        const std::size_t end = std::min(text.find(u8'\n', start), text.size());
        auto line = text.substr(start, end - start);
        if (!line.empty() && line.back() == u8'\r')
            line.remove_suffix(1);
        lines.push_back(line);
        start = end + 1;
    }
    const auto tl = corner(lines, u8"0002"), tr = corner(lines, u8"0003");
    const auto br = corner(lines, u8"0005"), bl = corner(lines, u8"0004");
    if (!tl || !tr || !br || !bl || tl->empty() || tr->size() != tl->size() || br->size() != tl->size() ||
        bl->size() != tl->size())
        return std::nullopt;
    PowerPin out;
    for (std::size_t i = 0; i < tl->size(); ++i)
        out.frames.push_back({(*tl)[i], (*tr)[i], (*br)[i], (*bl)[i]});
    out.fps = setting(lines, u8"Units Per Second").value_or(0);
    out.width = static_cast<int>(setting(lines, u8"Source Width").value_or(0));
    out.height = static_cast<int>(setting(lines, u8"Source Height").value_or(0));
    return out;
}

namespace {

using Corners = std::array<std::array<std::string, 2>, 4>;

std::string shortest(float v)
{
    char buffer[32];
    const auto end = std::to_chars(buffer, buffer + sizeof buffer, v).ptr;
    return {buffer, end};
}

Corners cornersOf(const persp::Quad &q)
{
    Corners out;
    for (std::size_t i = 0; i < 4; ++i)
        out[i] = {shortest(q[i].x), shortest(q[i].y)};
    return out;
}

std::optional<Corners> planeCorners(std::u8string_view value)
{
    const std::string_view text(reinterpret_cast<const char *>(value.data()), value.size());
    Corners out;
    std::size_t at = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        const char end = i == 7 ? '\0' : i % 2 == 0 ? ';' : '|';
        const std::size_t stop = end ? text.find(end, at) : text.size();
        if (stop == std::string_view::npos || stop == at)
            return std::nullopt;
        const std::string_view part = text.substr(at, stop - at);
        if (part.find_first_not_of("0123456789-.") != std::string_view::npos)
            return std::nullopt;
        out[i / 2][i % 2] = std::string(part);
        at = stop + 1;
    }
    return out;
}

std::u8string powerPinText(const std::vector<Corners> &frames, double fps, int width, int height)
{
    std::string out = "Adobe After Effects 6.0 Keyframe Data\n\n";
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "\tUnits Per Second\t%.6g\n", fps);
    out += buffer;
    out += "\tSource Width\t" + std::to_string(width) + "\n\tSource Height\t" + std::to_string(height) +
           "\n\tSource Pixel Aspect Ratio\t1\n\tComp Pixel Aspect Ratio\t1\n\n";
    constexpr std::array<std::size_t, 4> kCorner{0, 1, 3, 2};
    for (std::size_t c = 0; c < 4; ++c) {
        out += "Effects\tCC Power Pin #1\tCC Power Pin-000" + std::to_string(c + 2) + "\n\tFrame\tX pixels\tY pixels\n";
        for (std::size_t f = 0; f < frames.size(); ++f)
            out += "\t" + std::to_string(f) + "\t" + frames[f][kCorner[c]][0] + "\t" + frames[f][kCorner[c]][1] + "\n";
        out += "\n";
    }
    out += "End of Keyframe Data";
    return {reinterpret_cast<const char8_t *>(out.data()), out.size()};
}

}

std::u8string writePowerPin(const PowerPin &pin)
{
    std::vector<Corners> frames;
    for (const auto &q : pin.frames)
        frames.push_back(cornersOf(q));
    return powerPinText(frames, pin.fps, pin.width, pin.height);
}

std::expected<std::u8string, PowerPinFailure> derivePowerPinTrack(const EditSession &session,
                                                                  const LegacyTimebase &timebase, double fps,
                                                                  int videoHeight, const persp::TextMeasure &measure,
                                                                  const LineVisible &visible)
{
    const auto lines = shownSelected(session, visible);
    if (lines.empty())
        return std::unexpected(PowerPinFailure{PowerPinProblem::NoLines});
    const core::Document &doc = session.document();
    const Resolution res = scriptResolution(doc);
    const int layout = layoutResY(doc);
    const double z = persp::screenZ(res.height, layout > 0 ? layout : videoHeight > 0 ? videoHeight : res.height);
    persp::Reader reader;
    reader.document = &doc;
    reader.width = res.width;
    reader.height = res.height;
    reader.measure = measure;
    persp::StyleCache styles;
    std::map<int, Corners> quads;
    for (const auto *l : lines) {
        std::optional<Corners> corners;
        if (const auto plane = core::extradataValue(l->extra, persp::kPlaneKey))
            corners = planeCorners(*plane);
        if (!corners) {
            const auto state = persp::readLine(reader, *l, styles);
            if (!state)
                return std::unexpected(PowerPinFailure{PowerPinProblem::Unmeasured, rowOf(doc, l->id)});
            if (state->hasMove)
                return std::unexpected(PowerPinFailure{PowerPinProblem::Move, rowOf(doc, l->id)});
            corners = cornersOf(persp::textToQuad(state->transform, state->box, z));
        }
        const int first = timebase.frameAt(msOf(l->start.value)), end = timebase.frameAt(msOf(l->end.value));
        for (int f = first; f < end; ++f)
            if (!quads.emplace(f, *corners).second)
                return std::unexpected(PowerPinFailure{PowerPinProblem::Overlap, rowOf(doc, l->id)});
    }
    if (quads.empty())
        return std::unexpected(PowerPinFailure{PowerPinProblem::NoLines});
    std::vector<Corners> frames;
    Corners last = quads.begin()->second;
    for (int f = quads.begin()->first; f <= quads.rbegin()->first; ++f) {
        if (const auto it = quads.find(f); it != quads.end())
            last = it->second;
        frames.push_back(last);
    }
    return powerPinText(frames, fps, res.width, res.height);
}

std::optional<TrackedFrames> trackedFrames(const EditSession &session, const LegacyTimebase &timebase,
                                           const LineVisible &visible)
{
    const auto lines = shownSelected(session, visible);
    if (lines.empty() || !timebase.exact())
        return std::nullopt;
    int first = 0, end = 0;
    bool any = false;
    for (const auto *l : lines) {
        const int a = timebase.frameAt(msOf(l->start.value)), b = timebase.frameAt(msOf(l->end.value));
        first = any ? std::min(first, a) : a;
        end = any ? std::max(end, b) : b;
        any = true;
    }
    return TrackedFrames{first, std::max(0, end - first)};
}

TrackingCheck checkTracking(const EditSession &session, const LegacyTimebase &timebase, const PowerPin &data,
                            int relativeFrame, const LineVisible &visible)
{
    TrackingCheck out;
    const auto frames = trackedFrames(session, timebase, visible);
    if (!frames || frames->count == 0) {
        out.problem = TrackingProblem::NoLines;
        return out;
    }
    out.frames = *frames;
    if (static_cast<int>(data.frames.size()) != frames->count) {
        out.problem = TrackingProblem::Length;
        return out;
    }
    const int at = frames->first + std::clamp(relativeFrame, 1, frames->count) - 1;
    std::set<int> covered;
    bool overlap = false, anyAt = false;
    const core::LineRecord *missing = nullptr;
    for (const auto *l : shownSelected(session, visible)) {
        const int a = timebase.frameAt(msOf(l->start.value)), b = timebase.frameAt(msOf(l->end.value));
        for (int f = a; f < b; ++f)
            overlap = !covered.insert(f).second || overlap;
        if (a <= at && at < b)
            anyAt = true;
        else if (!missing)
            missing = l;
    }
    if (overlap && missing) {
        out.problem = TrackingProblem::Overlap;
        out.row = rowOf(session.document(), missing->id);
    } else if (!anyAt) {
        out.problem = TrackingProblem::NoLineAtFrame;
    }
    return out;
}

std::expected<void, CommandRefusal> applyTracking(EditSession &session, const LegacyTimebase &timebase,
                                                  const PowerPin &data, const TrackingOptions &options,
                                                  const LineVisible &visible)
{
    const TrackingCheck check = checkTracking(session, timebase, data, options.relativeFrame, visible);
    if (check.problem)
        return std::unexpected(CommandRefusal::Invalid);
    const core::Document &doc = session.document();
    const Resolution res = scriptResolution(doc);
    const float sx = static_cast<float>(res.width) /
                     (data.width > 0 ? data.width : options.videoWidth > 0 ? options.videoWidth : res.width);
    const float sy = static_cast<float>(res.height) /
                     (data.height > 0 ? data.height : options.videoHeight > 0 ? options.videoHeight : res.height);
    std::vector<persp::Quad> quads = data.frames;
    for (auto &q : quads)
        for (PointF &p : q)
            p = {p.x * sx, p.y * sy};
    const int layout = layoutResY(doc);
    const double z = persp::screenZ(res.height, layout > 0 ? layout : options.videoHeight > 0 ? options.videoHeight : res.height);
    const int relFrame = std::clamp(options.relativeFrame, 1, check.frames.count);
    const int at = check.frames.first + relFrame - 1;
    const persp::Quad &plane = quads[static_cast<std::size_t>(relFrame - 1)];

    persp::Reader reader;
    reader.document = &doc;
    reader.width = res.width;
    reader.height = res.height;
    reader.measure = options.measure;
    persp::StyleCache styles;
    const auto styleValues = core::decodeStyles(doc);

    struct Plan {
        core::LineId id;
        std::vector<Part> parts;
    };
    std::vector<Plan> plans;
    std::vector<Part *> rels;
    const auto lines = shownSelected(session, visible);
    for (const auto *l : lines) {
        Plan plan{l->id, {}};
        const int first = timebase.frameAt(msOf(l->start.value));
        int frame = first;
        for (auto &part : lineFrames(*l, timebase, styleValues, res))
            plan.parts.push_back({std::move(part), frame++, 0});
        plans.push_back(std::move(plan));
    }
    bool allAtRel = true;
    for (const auto *l : lines) {
        const int a = timebase.frameAt(msOf(l->start.value)), b = timebase.frameAt(msOf(l->end.value));
        allAtRel = allAtRel && a <= at && at < b;
    }
    for (auto &plan : plans) {
        const auto own = std::ranges::find(plan.parts, at, &Part::frame);
        if (own != plan.parts.end() && (allAtRel || rels.empty()))
            rels.push_back(&*own);
        for (auto &part : plan.parts)
            part.relativePart = allAtRel ? rels.size() - 1 : 0;
    }

    std::vector<persp::Quad> relQuads;
    for (Part *rel : rels) {
        auto state = persp::readLine(reader, rel->line, styles);
        if (!state)
            return std::unexpected(CommandRefusal::Invalid);
        if (options.fitFlat) {
            const auto fit = fitToPlane(*state, plane, options.orgMode, z);
            if (!fit)
                return std::unexpected(CommandRefusal::Invalid);
            const auto text = core::toUtf8(persp::writeTransform(reader, styles, rel->line, *fit));
            (rel->line.translation.empty() ? rel->line.text : rel->line.translation) = text;
            state = persp::readLine(reader, rel->line, styles);
            if (!state)
                return std::unexpected(CommandRefusal::Invalid);
        }
        relQuads.push_back(persp::textToQuad(state->transform, state->box, z));
    }

    for (auto &plan : plans)
        for (auto &part : plan.parts) {
            const persp::Quad &frameQuad = quads[static_cast<std::size_t>(part.frame - check.frames.first)];
            const auto state = persp::readLine(reader, part.line, styles);
            if (!state)
                return std::unexpected(CommandRefusal::Invalid);
            persp::Quad target;
            for (std::size_t i = 0; i < 4; ++i)
                target[i] = persp::fromUV(frameQuad, persp::toUV(plane, relQuads[part.relativePart][i]));
            auto t = persp::quadToTransform(target, state->box, options.orgMode, state->transform, z);
            if (!t)
                return std::unexpected(CommandRefusal::Invalid);
            if (!options.scaleBorders) {
                t->bord = state->transform.bord;
                t->shad = state->transform.shad;
            }
            std::u16string text = persp::writeTransform(reader, styles, part.line, *t);
            if (options.moveClips) {
                visual::transform::TagFind find;
                visual::transform::mapClip(find, text, [&](PointF p) { return persp::fromUV(frameQuad, persp::toUV(plane, p)); });
            }
            (part.line.translation.empty() ? part.line.text : part.line.translation) = core::toUtf8(text);
            part.line.extra = core::withExtradata(part.line.extra, persp::kPlaneKey, persp::planeValue(frameQuad));
        }

    std::set<core::LineId> touched;
    for (const auto &plan : plans)
        touched.insert(plan.id);
    Selection selection;
    const auto ran = session.run(Command{"Perspective tracking", session.revision(), touched, [&](core::Document &d) {
        selection = {};
        for (const auto &plan : plans) {
            if (plan.parts.empty())
                continue;
            const auto &first = plan.parts.front().line;
            if (!d.editLine(plan.id, [&](core::LineRecord &l) {
                    l.text = first.text;
                    l.translation = first.translation;
                    l.start.value = first.start.value;
                    l.end.value = first.end.value;
                    l.extra = first.extra;
                }))
                return false;
            selection.selected.insert(plan.id);
            core::LineId previous = plan.id;
            for (std::size_t k = 1; k < plan.parts.size(); ++k) {
                const auto id = d.insertLineAfter(previous, plan.parts[k].line);
                if (!id)
                    return false;
                selection.selected.insert(*id);
                previous = *id;
            }
        }
        return true;
    }});
    if (!ran)
        return std::unexpected(ran.error());
    if (!selection.selected.empty()) {
        selection.active = *selection.selected.begin();
        for (const auto &plan : plans)
            if (session.selection().active == plan.id)
                selection.active = plan.id;
        session.setSelection(selection);
    }
    return {};
}

}
