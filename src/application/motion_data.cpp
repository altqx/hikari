#include "hikari/application/motion_data.h"

#include "motion_tags.h"

#include "hikari/application/resample.h"

#include "hikari/core/extradata.h"
#include "hikari/core/style.h"
#include "hikari/json/json.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <map>
#include <random>
#include <set>

namespace hikari::application {

namespace {

std::string narrow(std::u8string_view text)
{
    return {reinterpret_cast<const char *>(text.data()), text.size()};
}

std::u8string widen(std::string_view text)
{
    return {reinterpret_cast<const char8_t *>(text.data()), text.size()};
}

std::optional<double> number(std::string_view text)
{
    double value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || error != std::errc{} || end != text.data() + text.size())
        return std::nullopt;
    return value;
}

bool blank(char c)
{
    return c == ' ' || c == '\t';
}

std::vector<std::string_view> fields(std::string_view line)
{
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && blank(line[i]))
            ++i;
        const std::size_t start = i;
        while (i < line.size() && !blank(line[i]))
            ++i;
        if (i > start)
            out.push_back(line.substr(start, i - start));
    }
    return out;
}

std::optional<double> setting(const std::vector<std::string_view> &lines, std::string_view label)
{
    for (const auto line : lines) {
        const auto parts = fields(line);
        std::string joined;
        for (std::size_t i = 0; i + 1 < parts.size(); ++i)
            joined += (i ? " " : "") + std::string(parts[i]);
        std::string lower = joined, want(label);
        std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::ranges::transform(want, want.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (parts.size() >= 2 && lower == want)
            return number(parts.back());
    }
    return std::nullopt;
}

std::int64_t msOf(core::DocumentTime t)
{
    return t.microseconds() / 1000;
}

core::DocumentTime timeOf(std::int64_t ms)
{
    return core::DocumentTime(ms * 1000);
}

std::vector<const core::LineRecord *> trackedLines(const EditSession &s, const LineVisible &visible)
{
    std::vector<const core::LineRecord *> out;
    for (const auto *l : s.document().lines())
        if (s.selection().selected.contains(l->id) && !l->comment && l->visibility != core::LineVisibility::Hidden &&
            (!visible || visible(l->id)))
            out.push_back(l);
    return out;
}

std::string newUuid()
{
    static thread_local std::mt19937_64 random{std::random_device{}()};
    std::string out = "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx";
    constexpr char kHex[] = "0123456789abcdef";
    for (char &c : out)
        if (c == 'x')
            c = kHex[random() % 16];
        else if (c == 'y')
            c = kHex[8 + random() % 4];
    return out;
}

struct Signature {
    std::string originalText, uuid;
};

std::optional<Signature> signatureOf(const core::LineRecord &line)
{
    const auto value = core::extradataValue(line.extra, u8"a-mo");
    if (!value)
        return std::nullopt;
    const auto json = json::Json::parse(narrow(*value));
    if (!json || !json->isObject())
        return std::nullopt;
    const auto *text = json->find("originalText");
    const auto *uuid = json->find("uuid");
    if (!text || !uuid || !text->isString() || !uuid->isString())
        return std::nullopt;
    return Signature{text->asString(), uuid->asString()};
}

std::map<std::string, motion::Props> stylesOf(const core::Document &doc)
{
    std::map<std::string, motion::Props> out;
    for (const auto &s : core::decodeStyles(doc))
        out[narrow(s.name)] = motion::propsOf(s);
    return out;
}

std::int64_t floorTen(std::int64_t ms)
{
    return static_cast<std::int64_t>(std::floor(static_cast<double>(std::max<std::int64_t>(0, ms)) / 10)) * 10;
}

std::int64_t midpoint(std::int64_t a, std::int64_t b)
{
    return static_cast<std::int64_t>(std::floor(0.5 * static_cast<double>(a + b)));
}

std::optional<motion::Track> clipTrackOf(const MotionOptions &options, double resX, double resY)
{
    if (!options.clipTrack)
        return std::nullopt;
    return motion::resolveTrack(*options.clipTrack, options.clip, resX, resY);
}

bool followsRects(const MotionFollow &f)
{
    return f.rectClips || f.rectToVector;
}

bool followsVectors(const MotionFollow &f)
{
    return f.vectorClips || f.rectToVector;
}

}

std::optional<MotionTrack> parseMotionData(std::u8string_view text)
{
    std::vector<std::string_view> lines;
    const std::string_view all(reinterpret_cast<const char *>(text.data()), text.size());
    for (std::size_t start = 0; start < all.size();) {
        const std::size_t end = std::min(all.find_first_of("\r\n", start), all.size());
        if (end > start)
            lines.push_back(all.substr(start, end - start));
        start = end + 1;
    }
    if (lines.empty() || lines.front().find("Adobe After Effects 6.0 Keyframe Data") == std::string_view::npos)
        return std::nullopt;
    MotionTrack out;
    const auto width = setting(lines, "Source Width"), height = setting(lines, "Source Height");
    if (!width || !height || *width <= 0 || *height <= 0)
        return std::nullopt;
    out.width = static_cast<int>(*width);
    out.height = static_cast<int>(*height);
    out.fps = setting(lines, "Units Per Second").value_or(0);
    enum class Section { None, Position, Scale, Rotation } section = Section::None;
    for (const auto line : lines) {
        if (!blank(line.front())) {
            section = line == "Position" ? Section::Position
                      : line == "Scale"  ? Section::Scale
                      : line == "Rotation" ? Section::Rotation
                                           : Section::None;
            continue;
        }
        const auto parts = fields(line);
        if (parts.size() < 2 || !number(parts[0]))
            continue;
        const auto a = number(parts[1]);
        const auto b = parts.size() > 2 ? number(parts[2]) : std::nullopt;
        if (!a)
            return std::nullopt;
        switch (section) {
        case Section::Position:
            if (!b)
                return std::nullopt;
            out.x.push_back(*a);
            out.y.push_back(*b);
            break;
        case Section::Scale:
            out.scaleX.push_back(*a);
            out.scaleY.push_back(b.value_or(*a));
            break;
        case Section::Rotation:
            out.rotation.push_back(*a);
            break;
        case Section::None:
            break;
        }
    }
    const std::size_t n = out.x.size();
    if (n == 0 || out.scaleX.size() != n || out.rotation.size() != n)
        return std::nullopt;
    return out;
}

std::u8string writeMotionData(const MotionTrack &track)
{
    std::string out = "Adobe After Effects 6.0 Keyframe Data\n\n";
    char buffer[160];
    const auto line = [&](const char *format, auto... values) {
        const int n = std::snprintf(buffer, sizeof buffer, format, values...);
        out.append(buffer, static_cast<std::size_t>(std::max(n, 0)));
    };
    line("\tUnits Per Second\t%.6g\n", track.fps);
    line("\tSource Width\t%d\n", track.width);
    line("\tSource Height\t%d\n", track.height);
    out += "\tSource Pixel Aspect Ratio\t1\n\tComp Pixel Aspect Ratio\t1\n\nPosition\n\tFrame\tX pixels\tY pixels\tZ pixels\n";
    for (std::size_t i = 0; i < track.x.size(); ++i)
        line("\t%zu\t%f\t%f\t0\n", i, track.x[i], track.y[i]);
    out += "\nScale\n\tFrame\tX percent\tY percent\tZ percent\n";
    for (std::size_t i = 0; i < track.scaleX.size(); ++i)
        line("\t%zu\t%f\t%f\t0\n", i, track.scaleX[i], track.scaleY[i]);
    out += "\nRotation\n\tFrame\tDegrees\n";
    for (std::size_t i = 0; i < track.rotation.size(); ++i)
        line("\t%zu\t%f\n", i, track.rotation[i]);
    out += "\nEnd of Keyframe Data\n";
    return widen(out);
}

MotionCheck checkMotion(const EditSession &session, const AegisubFrameTimes &times, int dataFrames,
                        const LineVisible &visible)
{
    MotionCheck out;
    const auto lines = trackedLines(session, visible);
    if (lines.empty()) {
        out.problem = MotionProblem::NoLines;
        return out;
    }
    std::int64_t first = 0, end = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto a = times.frameFromMs(msOf(lines[i]->start.value)), b = times.frameFromMs(msOf(lines[i]->end.value));
        first = i ? std::min(first, a) : a;
        end = i ? std::max(end, b) : b;
    }
    out.firstFrame = first;
    out.frames = static_cast<int>(end - first);
    if (out.frames != dataFrames)
        out.problem = MotionProblem::Length;
    return out;
}

namespace {

std::vector<core::LineRecord> trackedOutputs(const core::Document &doc, const std::vector<const core::LineRecord *> &lines,
                                             std::int64_t firstFrame, const AegisubFrameTimes &times,
                                             const MotionTrack &track, const MotionOptions &options)
{
    const Resolution res = scriptResolution(doc);
    const double resX = res.width, resY = res.height;

    motion::Setup setup;
    setup.options = options;
    if (!options.main.scale)
        setup.options.border = setup.options.shadow = setup.options.blur = false;
    setup.resX = resX;
    setup.resY = resY;
    setup.styles = stylesOf(doc);
    const motion::Track main = motion::resolveTrack(track, options.main, resX, resY);
    const auto clip = clipTrackOf(options, resX, resY);
    setup.main = &main;
    setup.rect = clip && followsRects(options.clip) ? &*clip : followsRects(options.main) ? &main : nullptr;
    setup.vector = clip && followsVectors(options.clip) ? &*clip : followsVectors(options.main) ? &main : nullptr;

    const auto ms = [&](std::int64_t frame) { return times.msFromFrame(frame); };
    std::vector<core::LineRecord> outputs;
    for (const auto *l : lines) {
        const std::string text = narrow(l->text);
        const std::int64_t start = msOf(l->start.value), end = msOf(l->end.value);
        const auto style = setup.styles.find(narrow(l->style));
        const motion::Props props = style != setup.styles.end() ? style->second : motion::Props{};
        const motion::PreparedLine prepared =
            motion::prepareLine(text, end - start, props, setup, static_cast<int>(l->marginLeft.value),
                                static_cast<int>(l->marginRight.value), static_cast<int>(l->marginVertical.value));
        core::LineRecord base = *l;
        if (!signatureOf(*l))
            base.extra = core::withExtradata(
                base.extra, u8"a-mo",
                widen(json::Json::object({{"originalText", text}, {"uuid", newUuid()}}).dump()));
        const std::int64_t lineFirst = times.frameFromMs(start), lineEnd = times.frameFromMs(end);
        const std::int64_t relStart = lineFirst - firstFrame + 1, relEnd = lineEnd - firstFrame;
        if (prepared.linear(setup)) {
            motion::LinearSpan span;
            span.first = static_cast<std::size_t>(std::max<std::int64_t>(relStart, 1) - 1);
            span.last = static_cast<std::size_t>(std::max<std::int64_t>(relEnd, 1) - 1);
            span.begin = midpoint(ms(lineFirst), ms(lineFirst + 1)) - start;
            span.end = midpoint(ms(lineEnd - 1), ms(lineEnd)) - start;
            core::LineRecord out = base;
            out.text = widen(motion::finishLinear(motion::linearText(prepared, setup, span)));
            outputs.push_back(std::move(out));
            continue;
        }
        const std::int64_t firstStart = floorTen(ms(firstFrame + relStart - 1));
        for (std::int64_t frame = relStart; frame <= relEnd; ++frame) {
            const std::int64_t newStart = floorTen(ms(firstFrame + frame - 1));
            const std::int64_t newEnd = floorTen(ms(firstFrame + frame));
            const std::int64_t at = times.frameFromMs(newStart);
            motion::FrameTime time;
            time.index = static_cast<std::size_t>(frame - 1);
            time.delta = newStart - firstStart;
            time.evaluate = midpoint(ms(at), ms(at + 1)) - newStart;
            const std::string body = motion::frameText(prepared, setup, time);
            core::LineRecord out = base;
            out.text = widen(motion::finishFrame(body, static_cast<double>(newStart - start) * 0.1));
            out.start.value = timeOf(newStart);
            out.end.value = timeOf(newEnd);
            outputs.push_back(std::move(out));
        }
    }

    std::vector<core::LineRecord> merged;
    for (auto &out : outputs) {
        if (!merged.empty()) {
            auto &last = merged.back();
            if (last.text == out.text && last.style == out.style &&
                (last.start.value == out.end.value || last.end.value == out.start.value)) {
                last.start.value = std::min(last.start.value, out.start.value);
                last.end.value = std::max(last.end.value, out.end.value);
                continue;
            }
        }
        merged.push_back(std::move(out));
    }
    return merged;
}

std::expected<void, CommandRefusal> replaceLines(EditSession &session, std::string name,
                                                 const std::vector<const core::LineRecord *> &lines,
                                                 const std::vector<core::LineRecord> &outputs)
{
    std::set<core::LineId> touched;
    for (const auto *l : lines)
        touched.insert(l->id);
    Selection selection;
    const core::LineId anchor = lines.front()->id;
    const auto ran = session.run(Command{std::move(name), session.revision(), touched, [&](core::Document &d) {
        selection = {};
        for (const auto &out : outputs) {
            const auto id = d.insertLineBefore(anchor, out);
            if (!id)
                return false;
            selection.selected.insert(*id);
            if (!selection.active)
                selection.active = *id;
        }
        for (const auto id : touched)
            if (!d.removeLine(id))
                return false;
        return true;
    }});
    if (!ran)
        return std::unexpected(ran.error());
    session.setSelection(selection);
    return {};
}

}

std::expected<void, CommandRefusal> applyMotion(EditSession &session, const AegisubFrameTimes &times,
                                                const MotionTrack &track, const MotionOptions &options,
                                                const LineVisible &visible)
{
    const MotionCheck check = checkMotion(session, times, track.frames(), visible);
    if (check.problem || (options.clipTrack && options.clipTrack->frames() != track.frames()))
        return std::unexpected(CommandRefusal::Invalid);
    const auto lines = trackedLines(session, visible);
    const auto merged = trackedOutputs(session.document(), lines, check.firstFrame, times, track, options);
    return replaceLines(session, "Apply motion data", lines, merged);
}

std::expected<void, CommandRefusal> revertMotion(EditSession &session, const LineVisible &visible)
{
    const core::Document &doc = session.document();
    struct Group {
        core::LineRecord line;
        std::string originalText;
        std::optional<core::LineId> keep;
        std::vector<core::LineId> members;
        core::DocumentTime start, end;
    };
    std::map<std::string, Group> groups;
    std::vector<std::string> order;
    for (const auto *l : doc.lines()) {
        if (!session.selection().selected.contains(l->id) || l->visibility == core::LineVisibility::Hidden ||
            (visible && !visible(l->id)))
            continue;
        const auto signature = signatureOf(*l);
        if (!signature || groups.contains(signature->uuid))
            continue;
        groups.emplace(signature->uuid, Group{*l, signature->originalText, {}, {}, l->start.value, l->end.value});
        order.push_back(signature->uuid);
    }
    if (groups.empty())
        return std::unexpected(CommandRefusal::Invalid);
    std::set<core::LineId> touched;
    for (const auto *l : doc.lines()) {
        const auto signature = signatureOf(*l);
        if (!signature)
            continue;
        const auto found = groups.find(signature->uuid);
        if (found == groups.end())
            continue;
        Group &g = found->second;
        g.start = std::min(g.start, l->start.value);
        g.end = std::max(g.end, l->end.value);
        (g.keep ? g.members.push_back(l->id) : void(g.keep = l->id));
        touched.insert(l->id);
    }
    Selection selection;
    const auto ran = session.run(Command{"Revert motion data", session.revision(), touched, [&](core::Document &d) {
        selection = {};
        for (const auto &uuid : order) {
            const Group &g = groups.at(uuid);
            if (!d.editLine(*g.keep, [&](core::LineRecord &l) {
                    const core::LineId id = l.id;
                    l = g.line;
                    l.id = id;
                    l.text = widen(g.originalText);
                    l.start.value = g.start;
                    l.end.value = g.end;
                    l.extra = core::withoutExtradata(l.extra, u8"a-mo");
                }))
                return false;
            selection.selected.insert(*g.keep);
            if (!selection.active)
                selection.active = *g.keep;
            for (const auto id : g.members)
                if (!d.removeLine(id))
                    return false;
        }
        return true;
    }});
    if (!ran)
        return std::unexpected(ran.error());
    session.setSelection(selection);
    return {};
}

namespace {

int rowOf(const core::Document &doc, const core::LineRecord *line)
{
    int row = 0;
    for (const auto *l : doc.lines()) {
        ++row;
        if (l == line)
            return row;
    }
    return 0;
}

std::string strippedText(std::string_view text)
{
    std::string out;
    for (const auto &piece : motion::parseText(text))
        out += piece.block ? "{}" : piece.text;
    return out;
}

struct FirstBlock {
    std::optional<std::array<double, 2>> pos;
    std::optional<motion::Move> move;
    std::optional<double> scaleX, scaleY, angle;
};

FirstBlock firstBlockOf(std::string_view text, std::int64_t duration)
{
    FirstBlock out;
    const auto pieces = motion::parseText(text);
    if (pieces.empty() || !pieces.front().block)
        return out;
    const auto value = [](std::string_view tag, std::string_view name) -> std::optional<double> {
        if (!tag.starts_with(name))
            return std::nullopt;
        tag.remove_prefix(name.size());
        std::size_t n = 0;
        while (n < tag.size() && ((tag[n] >= '0' && tag[n] <= '9') || tag[n] == '.' || tag[n] == '-'))
            ++n;
        return number(tag.substr(0, n));
    };
    const auto args = [](std::string_view tag, std::string_view name) -> std::optional<std::vector<double>> {
        if (!tag.starts_with(name) || !tag.ends_with(")"))
            return std::nullopt;
        tag.remove_prefix(name.size());
        tag.remove_suffix(1);
        std::vector<double> out;
        std::size_t start = 0;
        for (std::size_t i = 0; i <= tag.size(); ++i)
            if (i == tag.size() || tag[i] == ',') {
                const auto v = number(tag.substr(start, i - start));
                if (!v)
                    return std::nullopt;
                out.push_back(*v);
                start = i + 1;
            }
        return out;
    };
    for (const auto &tag : pieces.front().tags) {
        if (const auto p = args(tag, "\\pos("); p && p->size() == 2 && !out.pos && !out.move)
            out.pos = std::array<double, 2>{(*p)[0], (*p)[1]};
        if (const auto m = args(tag, "\\move("); m && (m->size() == 4 || m->size() == 6) && !out.pos && !out.move)
            out.move = motion::Move{(*m)[0], (*m)[1], (*m)[2], (*m)[3], m->size() == 6 ? (*m)[4] : 0,
                                    m->size() == 6 ? (*m)[5] : static_cast<double>(duration)};
        if (const auto v = value(tag, "\\fscx"))
            out.scaleX = v;
        if (const auto v = value(tag, "\\fscy"))
            out.scaleY = v;
        if (const auto v = value(tag, "\\frz"))
            out.angle = v;
        else if (!tag.starts_with("\\frx") && !tag.starts_with("\\fry"))
            if (const auto w = value(tag, "\\fr"))
                out.angle = w;
    }
    return out;
}

}

std::expected<MotionTrack, DeriveFailure> deriveMotion(const EditSession &session, const AegisubFrameTimes &times,
                                                      const LineVisible &visible)
{
    std::vector<const core::LineRecord *> lines;
    for (const auto *l : session.document().lines())
        if (session.selection().selected.contains(l->id) && l->visibility != core::LineVisibility::Hidden &&
            (!visible || visible(l->id)))
            lines.push_back(l);
    if (lines.size() < 2)
        return std::unexpected(DeriveFailure{DeriveProblem::TooFewLines});
    for (std::size_t i = 1; i < lines.size(); ++i) {
        const auto *a = lines[i - 1], *b = lines[i];
        if (strippedText(narrow(a->text)) != strippedText(narrow(b->text)) || a->end.value != b->start.value ||
            a->layer.value != b->layer.value || a->comment != b->comment)
            return std::unexpected(DeriveFailure{DeriveProblem::NotFrameByFrame, rowOf(session.document(), b)});
    }
    const core::Document &doc = session.document();
    const Resolution res = scriptResolution(doc);
    const auto styles = stylesOf(doc);
    MotionTrack out;
    out.fps = times.fps();
    out.width = res.width;
    out.height = res.height;
    for (const auto *l : lines) {
        const std::int64_t start = msOf(l->start.value), end = msOf(l->end.value);
        const FirstBlock block = firstBlockOf(narrow(l->text), end - start);
        if (!block.pos && !block.move)
            return std::unexpected(DeriveFailure{DeriveProblem::NoPosition, rowOf(session.document(), l)});
        const auto style = styles.find(narrow(l->style));
        const motion::Props props = style != styles.end() ? style->second : motion::Props{};
        const std::int64_t first = times.frameFromMs(start), last = times.frameFromMs(end);
        for (std::int64_t f = first; f < last; ++f) {
            const auto pos = block.pos ? *block.pos
                                       : block.move->at(static_cast<double>(std::max<std::int64_t>(0, times.msFromFrame(f) - start)));
            out.x.push_back(pos[0]);
            out.y.push_back(pos[1]);
            out.scaleX.push_back(block.scaleX.value_or(props.scaleX));
            out.scaleY.push_back(block.scaleY.value_or(props.scaleY));
            out.rotation.push_back(-block.angle.value_or(props.angle));
        }
    }
    return out;
}

namespace {

constexpr std::array kSeries{&MotionTrack::x, &MotionTrack::y, &MotionTrack::scaleX, &MotionTrack::scaleY,
                             &MotionTrack::rotation};

SeriesFits extendSeries(MotionTrack &out, const MotionTrack &given, int frames, ExtendAt at, bool keepGiven)
{
    SeriesFits fits;
    for (std::size_t i = 0; i < kSeries.size(); ++i) {
        const std::vector<double> &values = given.*kSeries[i];
        Extension e = extendValues(values, frames, at);
        fits[i] = e.fit;
        std::vector<double> &target = out.*kSeries[i];
        if (!keepGiven) {
            target = std::move(e.values);
        } else if (at == ExtendAt::Start) {
            e.values.pop_back();
            target.insert(target.begin(), e.values.begin(), e.values.end());
        } else {
            target.insert(target.end(), e.values.begin() + 1, e.values.end());
        }
    }
    return fits;
}

}

FitConfidence worstConfidence(const SeriesFits &fits)
{
    FitConfidence out = FitConfidence::Good;
    for (const auto &fit : fits)
        out = std::max(out, fit.confidence());
    return out;
}

FitConfidence ExtendedTrack::confidence() const
{
    return std::max(before ? worstConfidence(*before) : FitConfidence::Good,
                    after ? worstConfidence(*after) : FitConfidence::Good);
}

ExtendedTrack extendTrack(const MotionTrack &track, int before, int after)
{
    ExtendedTrack out;
    out.track = track;
    if (track.frames() == 0)
        return out;
    if (before > 0)
        out.before = extendSeries(out.track, track, before, ExtendAt::Start, true);
    if (after > 0)
        out.after = extendSeries(out.track, track, after, ExtendAt::End, true);
    return out;
}

namespace {

bool animated(std::string_view text)
{
    for (const auto &piece : motion::parseText(text))
        for (const auto &tag : piece.tags)
            for (const std::string_view name : {"\\t(", "\\move(", "\\fad", "\\k", "\\K"})
                if (std::string_view(tag).starts_with(name))
                    return true;
    return false;
}

struct ExtendPlan {
    const core::LineRecord *target = nullptr;
    core::LineRecord grown;
    MotionTrack track;
    SeriesFits fits;
};

std::expected<ExtendPlan, ExtendFailure> planExtend(const EditSession &session, const AegisubFrameTimes &times,
                                                    int frames, ExtendAt at, const LineVisible &visible)
{
    const auto lines = trackedLines(session, visible);
    const core::Document &doc = session.document();
    if (lines.size() < 2 || frames < 1)
        return std::unexpected(ExtendFailure{ExtendProblem::TooFewLines});
    const auto styles = stylesOf(doc);
    MotionTrack given;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto *l = lines[i];
        const std::string text = narrow(l->text);
        const bool oneFrame = times.frameFromMs(msOf(l->end.value)) - times.frameFromMs(msOf(l->start.value)) == 1;
        if (!oneFrame || (i > 0 && (l->start.value != lines[i - 1]->end.value ||
                                    l->layer.value != lines[i - 1]->layer.value ||
                                    strippedText(text) != strippedText(narrow(lines[i - 1]->text)))))
            return std::unexpected(ExtendFailure{ExtendProblem::NotFrameByFrame, rowOf(doc, l)});
        if (animated(text))
            return std::unexpected(ExtendFailure{ExtendProblem::Animated, rowOf(doc, l)});
        const FirstBlock block = firstBlockOf(text, msOf(l->end.value) - msOf(l->start.value));
        if (!block.pos)
            return std::unexpected(ExtendFailure{ExtendProblem::NoPosition, rowOf(doc, l)});
        const auto style = styles.find(narrow(l->style));
        const motion::Props props = style != styles.end() ? style->second : motion::Props{};
        given.x.push_back((*block.pos)[0]);
        given.y.push_back((*block.pos)[1]);
        given.scaleX.push_back(block.scaleX.value_or(props.scaleX));
        given.scaleY.push_back(block.scaleY.value_or(props.scaleY));
        given.rotation.push_back(-block.angle.value_or(props.angle));
    }
    ExtendPlan plan;
    const Resolution res = scriptResolution(doc);
    plan.track.width = res.width;
    plan.track.height = res.height;
    plan.track.fps = times.fps();
    plan.fits = extendSeries(plan.track, given, frames, at, false);
    plan.target = at == ExtendAt::Start ? lines.front() : lines.back();
    plan.grown = *plan.target;
    if (at == ExtendAt::Start) {
        const std::int64_t frame = times.frameFromMs(msOf(plan.target->start.value)) - frames;
        if (frame <= 0)
            return std::unexpected(ExtendFailure{ExtendProblem::BeforeVideo, rowOf(doc, plan.target)});
        plan.grown.start.value = timeOf(times.msFromFrame(frame));
    } else {
        plan.grown.end.value = timeOf(times.msFromFrame(times.frameFromMs(msOf(plan.target->end.value)) + frames));
    }
    return plan;
}

}

std::expected<SeriesFits, ExtendFailure> checkExtend(const EditSession &session, const AegisubFrameTimes &times,
                                                     int frames, ExtendAt at, const LineVisible &visible)
{
    const auto plan = planExtend(session, times, frames, at, visible);
    if (!plan)
        return std::unexpected(plan.error());
    return plan->fits;
}

std::expected<SeriesFits, ExtendFailure> extendTrackedLines(EditSession &session, const AegisubFrameTimes &times,
                                                            int frames, ExtendAt at, bool origin,
                                                            const LineVisible &visible)
{
    const auto plan = planExtend(session, times, frames, at, visible);
    if (!plan)
        return std::unexpected(plan.error());
    const core::Document &doc = session.document();
    const std::int64_t first = times.frameFromMs(msOf(plan->grown.start.value));
    if (times.frameFromMs(msOf(plan->grown.end.value)) - first != plan->track.frames())
        return std::unexpected(ExtendFailure{ExtendProblem::Refused, rowOf(doc, plan->target)});
    MotionOptions options;
    options.main.relativeFrame = at == ExtendAt::Start ? frames + 1 : 1;
    options.main.rotation = true;
    options.origin = origin;
    const auto outputs = trackedOutputs(doc, {&plan->grown}, first, times, plan->track, options);
    if (!replaceLines(session, "Extend tracked lines", {plan->target}, outputs))
        return std::unexpected(ExtendFailure{ExtendProblem::Refused, rowOf(doc, plan->target)});
    return plan->fits;
}

std::optional<std::u8string> extrapolateMove(std::u8string_view text, std::int64_t durationMs)
{
    const std::string s = narrow(text);
    for (std::size_t at = s.find("\\move("); at != std::string::npos; at = s.find("\\move(", at + 1)) {
        const std::size_t close = s.find(')', at);
        if (close == std::string::npos)
            return std::nullopt;
        std::vector<double> v;
        std::size_t start = at + 6;
        bool ok = true;
        for (std::size_t i = start; i <= close; ++i)
            if (i == close || s[i] == ',') {
                const auto n = number(std::string_view(s).substr(start, i - start));
                ok = ok && n.has_value();
                v.push_back(n.value_or(0));
                start = i + 1;
            }
        if (!ok || v.size() != 6)
            continue;
        const double length = v[5] - v[4];
        if (length == 0)
            return std::nullopt;
        const double dx = (v[2] - v[0]) / length, dy = (v[3] - v[1]) / length;
        const double t1 = v[4], t2 = v[5], duration = static_cast<double>(durationMs);
        char buffer[160];
        const int n = std::snprintf(buffer, sizeof buffer, "\\move(%.2f,%.2f,%.2f,%.2f)", v[0] - dx * t1,
                                    v[1] - dy * t1, v[2] + dx * (duration - t2), v[3] + dy * (duration - t2));
        return widen(s.substr(0, at) + std::string(buffer, static_cast<std::size_t>(std::max(n, 0))) + s.substr(close + 1));
    }
    return std::nullopt;
}

std::expected<int, CommandRefusal> extrapolateMoves(EditSession &session, const LineVisible &visible)
{
    std::map<core::LineId, std::u8string> changes;
    for (const auto *l : session.document().lines()) {
        if (!session.selection().selected.contains(l->id) || l->visibility == core::LineVisibility::Hidden ||
            (visible && !visible(l->id)))
            continue;
        if (auto text = extrapolateMove(l->text, msOf(l->end.value) - msOf(l->start.value)))
            changes.emplace(l->id, std::move(*text));
    }
    if (changes.empty())
        return std::unexpected(CommandRefusal::Invalid);
    std::set<core::LineId> touched;
    for (const auto &[id, _] : changes)
        touched.insert(id);
    const auto ran = session.run(Command{"Extrapolate \\move", session.revision(), touched, [&](core::Document &d) {
        for (const auto &[id, text] : changes)
            if (!d.editLine(id, [&](core::LineRecord &l) { l.text = text; }))
                return false;
        return true;
    }});
    if (!ran)
        return std::unexpected(ran.error());
    return static_cast<int>(changes.size());
}

}
