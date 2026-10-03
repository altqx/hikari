#include "hikari/application/shift_times.h"

#include "hikari/core/text_projection.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>
#include <string>

namespace hikari::application {

namespace legacy {

std::u8string changeTagTimes(std::u8string_view text8, int start, int end)
{
    std::u16string text = core::toUtf16(text8);
    // ParseTags over {move, t, fad}: the value inside the parentheses, up to
    // the next '\' or '}' (so \t's value stops before its inner tags).
    struct Found {
        std::u16string name;
        std::size_t valueStart;
        std::u16string value;
    };
    std::vector<Found> found;
    bool block = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char16_t c = text[i];
        if (c == u'{')
            block = true;
        else if (c == u'}')
            block = false;
        else if (block && c == u'\\') {
            std::size_t end_ = i + 1;
            while (end_ < text.size() && text[end_] != u'\\' && text[end_] != u'}')
                ++end_;
            std::u16string tag = text.substr(i + 1, end_ - i - 1);
            if (!tag.empty() && tag.back() == u')')
                tag.pop_back();
            for (const std::u16string name : {std::u16string(u"move"), std::u16string(u"t"), std::u16string(u"fad")}) {
                if (tag.size() > name.size() && tag.starts_with(name) && tag[name.size()] == u'(') {
                    std::u16string value = tag.substr(name.size() + 1);
                    const auto close = value.find(u')');
                    if (close != std::u16string::npos)
                        value = value.substr(0, close);
                    found.push_back({name, i + 1 + name.size() + 1, value});
                    break;
                }
            }
            i = end_ - 1;
        }
    }
    // Dialogue::ChangeTimes: replace "t1,t2" by the shifted pair; later
    // positions move by the length difference (legacy replaceMismatch).
    long mismatch = 0;
    for (const auto &f : found) {
        struct Token {
            std::size_t pos, len;
        };
        std::vector<Token> tokens; // wxTOKEN_STRTOK: empty tokens are skipped
        for (std::size_t p = 0; p < f.value.size();) {
            const auto comma = std::min(f.value.find(u',', p), f.value.size());
            if (comma > p)
                tokens.push_back({p, comma - p});
            p = comma + 1;
        }
        const std::size_t first = f.name == u"move" ? 4 : 0;
        const std::size_t needed = f.name == u"move" ? 5 : f.name == u"t" ? 2 : 1;
        if (tokens.size() < needed || tokens.size() < first + 2)
            continue; // a lone \fad value has no second time to pair (legacy reads past it)
        auto atoi16 = [&](const Token &t) {
            std::string s;
            for (std::size_t k = 0; k < t.len; ++k)
                s += static_cast<char>(f.value[t.pos + k] < 0x80 ? f.value[t.pos + k] : '?');
            return std::atoi(s.c_str());
        };
        const Token &a = tokens[first], &b = tokens[first + 1];
        const int t1 = std::max(0, atoi16(a) + start), t2 = std::max(0, atoi16(b) + end);
        const std::u16string replacement = core::toUtf16(std::u8string(
            reinterpret_cast<const char8_t *>((std::to_string(t1) + "," + std::to_string(t2)).c_str())));
        const long at = static_cast<long>(f.valueStart + a.pos) - mismatch;
        const std::size_t total = a.len + b.len + 1;
        if (at < 0 || static_cast<std::size_t>(at) + total > text.size())
            continue;
        text.replace(static_cast<std::size_t>(at), total, replacement);
        mismatch += static_cast<long>(total) - static_cast<long>(replacement.size());
    }
    return core::toUtf8(text);
}

} // namespace legacy

namespace {

constexpr std::int64_t kUsPerMs = 1000;

int msOf(core::DocumentTime t)
{
    return static_cast<int>(t.microseconds() / kUsPerMs);
}

int zeroIt(int ms)
{
    return (ms / 10) * 10;
}

std::optional<double> microDvdFps(const core::Document &d)
{
    if (d.format() != core::SubtitleFormat::MicroDvd)
        return std::nullopt;
    if (!d.frameRate())
        return 0.0;
    const auto &fps = d.frameRate()->framesPerSecond();
    return static_cast<double>(fps.numerator()) / static_cast<double>(fps.denominator());
}

struct Shifted {
    core::LineId id;
    int start, end;
    std::u8string text;        // the field the tag times live in (translation if any)
    bool textChanged = false;
    std::size_t textLength = 0; // legacy Text.Len(): UTF-16 units of the text
};

} // namespace

std::expected<ShiftOutcome, std::variant<ShiftProblem, CommandRefusal>>
shiftTimes(EditSession &session, const ShiftTimesSettings &s, const ShiftContext &context, const LineVisible &visible)
{
    using Fail = std::variant<ShiftProblem, CommandRefusal>;
    const auto &doc = session.document();
    const LegacyTimebase empty;
    const LegacyTimebase &timebase = context.timebase ? *context.timebase : empty;
    const bool exact = timebase.exact();
    if (s.byFrames && !exact)
        return std::unexpected(Fail{ShiftProblem::NoExactTimebase});
    const auto fps = microDvdFps(doc);
    if (fps && *fps <= 0)
        return std::unexpected(Fail{CommandRefusal::Invalid}); // C01-fps-isolation
    int time = s.byFrames ? 0 : s.timeMs;
    int frame = s.byFrames ? s.frames : 0;
    const int whichLines = std::max(0, s.whichLines);
    int whichTimes = std::max(0, s.whichTimes);
    if (s.styles.empty() && whichLines == 5)
        return std::unexpected(Fail{ShiftProblem::NoStylesChosen});
    const std::u8string styles = u8"," + s.styles + u8",";
    if (!s.forward) {
        time = -time;
        frame = -frame;
    }
    if (doc.format() == core::SubtitleFormat::TMPlayer)
        whichTimes = 1;
    const auto lines = doc.lines();
    std::optional<std::size_t> firstSelection;
    for (std::size_t i = 0; i < lines.size() && !firstSelection; ++i)
        if (session.selection().selected.contains(lines[i]->id))
            firstSelection = i;
    if (!firstSelection && whichLines != 0)
        return std::unexpected(Fail{ShiftProblem::NoLinesSelected}); // legacy reads outside the Lines for 4
    // The marked Line (the active one) for moving to video or audio time.
    const core::LineRecord *marked = nullptr;
    for (const auto *l : lines)
        if (session.selection().active == l->id)
            marked = l;
    if (!marked && !lines.empty())
        marked = lines.front();
    const int difftime = marked ? (s.fromStartTime ? msOf(marked->start.value) : msOf(marked->end.value)) : 0;
    if (s.moveToVideoTime && context.videoFrame) {
        if (s.byFrames) {
            frame += *context.videoFrame - timebase.frameAt(difftime);
        } else if (const auto at = s.fromStartTime ? context.videoFrameStartMs : context.videoFrameEndMs) {
            int added = *at - difftime;
            if (added < 0)
                added -= 10;
            time += zeroIt(added);
        }
    } else if (s.moveToAudioTime && context.audioMarkMs) {
        if (s.byFrames) {
            frame += timebase.frameAt(*context.audioMarkMs - difftime);
        } else {
            int added = *context.audioMarkMs - difftime;
            if (added < 0)
                added -= 10;
            time += zeroIt(added);
        }
    }
    const int firsttime = firstSelection ? msOf(lines[*firstSelection]->start.value) : 0;
    auto startEndDelay = [&](int start, int end) {
        if (timebase.empty())
            return std::pair{0, 0};
        return std::pair{timebase.msAt(timebase.frameAt(start)) - start, timebase.msAt(timebase.frameAt(end)) - end};
    };
    std::vector<Shifted> shifted;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto *line = lines[i];
        if (visible && !visible(line->id))
            continue;
        const bool chosen = whichLines == 0 || (whichLines == 1 && session.selection().selected.contains(line->id)) ||
                            (whichLines == 3 && firsttime <= msOf(line->start.value)) ||
                            (whichLines == 2 && firstSelection && i >= *firstSelection) ||
                            (whichLines == 4 && firsttime >= msOf(line->start.value)) ||
                            (whichLines == 5 && styles.find(u8"," + line->style + u8",") != std::u8string::npos);
        if (!chosen)
            continue;
        Shifted l{line->id, msOf(line->start.value), msOf(line->end.value),
                  line->translation.empty() ? line->text : line->translation};
        l.textLength = core::toUtf16(line->text).size();
        std::pair<int, int> before{0, 0};
        int duration = 0;
        if (s.tagTimes)
            before = startEndDelay(l.start, l.end);
        if (time != 0) {
            if (whichTimes != 2)
                l.start = std::max(0, l.start + time);
            if (whichTimes != 1)
                l.end = std::max(0, l.end + time);
        } else if (frame != 0) {
            if (whichTimes == 0)
                duration = l.end - l.start;
            if (whichTimes != 2)
                l.start = std::max(0, zeroIt(timebase.startTimeFor(timebase.frameAt(l.start) + frame)));
            if (whichTimes != 1)
                l.end = std::max(0, zeroIt(timebase.startTimeFor(timebase.frameAt(l.end) + frame)));
        }
        if (s.tagTimes) {
            auto after = startEndDelay(l.start, l.end);
            if (s.byFrames)
                after.second += (l.end - l.start) - duration;
            const auto text = legacy::changeTagTimes(l.text, after.first - before.first, after.second - before.second);
            l.textChanged = text != l.text;
            l.text = text;
        }
        shifted.push_back(std::move(l));
    }
    ShiftOutcome outcome;
    if (s.correctEndTimes > 0) {
        if (!exact) {
            outcome.endCorrectionSkipped = true; // legacy logs it and stops before correcting
        } else {
            // In start, then end order (a stable multimap in legacy).
            std::vector<Shifted *> order;
            for (auto &l : shifted)
                order.push_back(&l);
            std::stable_sort(order.begin(), order.end(), [](const Shifted *a, const Shifted *b) {
                return a->start != b->start ? a->start < b->start : a->end < b->end;
            });
            for (std::size_t k = 0; k + 1 < order.size(); ++k) {
                Shifted &cur = *order[k];
                const Shifted &next = *order[k + 1];
                const bool endGreater = cur.end > next.start || cur.end == next.end || k == 0;
                if (s.correctEndTimes > 1) {
                    if (endGreater)
                        continue;
                    int newEnd = s.timePerCharacter * static_cast<int>(cur.textLength);
                    if (newEnd < 1000)
                        newEnd = 1000;
                    cur.end = std::max(0, newEnd + cur.start);
                }
                if (cur.end > next.start)
                    cur.end = next.start;
            }
        }
    }
    std::set<core::LineId> touched;
    for (const auto &l : shifted)
        touched.insert(l.id);
    const auto ran = session.run(Command{"Shifting times", session.revision(), touched, [&](core::Document &d) {
        for (const auto &l : shifted)
            if (!d.editLine(l.id, [&](core::LineRecord &r) {
                    auto set = [&](core::TimeField &field, std::optional<std::int64_t> &frameField, int ms) {
                        field.value = core::DocumentTime(std::int64_t{ms} * kUsPerMs);
                        if (fps)
                            frameField = static_cast<std::int64_t>(
                                std::ceil(static_cast<float>(ms) * (static_cast<float>(*fps) / 1000.f)));
                    };
                    set(r.start, r.startFrame, l.start);
                    set(r.end, r.endFrame, l.end);
                    if (l.textChanged)
                        (r.translation.empty() ? r.text : r.translation) = l.text;
                }))
                return false;
        return true;
    }});
    if (!ran)
        return std::unexpected(Fail{ran.error()});
    return outcome;
}

std::string shiftProfileText(const std::string &name, const ShiftTimesSettings &s)
{
    auto flag = [](bool b) { return b ? std::string("1") : std::string("0"); };
    // Legacy GetProfileString: the audio flag is written under the video label too.
    return name + ": Time: " + std::to_string(s.timeMs) + " Forward: " + flag(s.forward) + " Frames: " + flag(s.byFrames) +
           " MoveTagTimes: " + flag(s.tagTimes) + " MoveToStartTimes: " + flag(s.fromStartTime) +
           " MoveToVideoTime: " + flag(s.moveToVideoTime) + " MoveToVideoTime: " + flag(s.moveToAudioTime) +
           " WhichLines: " + std::to_string(s.whichLines) + " StylesText: " + std::string(s.styles.begin(), s.styles.end()) +
           " WhichTimes: " + std::to_string(s.whichTimes) + " EndTimeCorrection: " + std::to_string(s.correctEndTimes);
}

std::string shiftProfileName(const std::string &text)
{
    return text.substr(0, text.find(':'));
}

ShiftTimesSettings applyShiftProfile(const std::string &text, ShiftTimesSettings s)
{
    // AfterFirst(':') with the first character removed, split on single spaces
    // (empty tokens kept); every other token is a label.
    const auto colon = text.find(':');
    std::string rest = colon == std::string::npos ? std::string() : text.substr(colon + 1);
    if (!rest.empty())
        rest.erase(0, 1);
    std::vector<std::string> tokens;
    for (std::size_t p = 0;;) {
        const auto space = rest.find(' ', p);
        tokens.push_back(rest.substr(p, space == std::string::npos ? std::string::npos : space - p));
        if (space == std::string::npos)
            break;
        p = space + 1;
    }
    std::size_t k = 1; // the first label
    auto next = [&]() -> std::optional<std::string> {
        if (k >= tokens.size())
            return std::nullopt;
        std::string v = tokens[k];
        k += 2; // the value and the following label
        return v;
    };
    if (auto v = next())
        s.timeMs = std::atoi(v->c_str());
    if (auto v = next())
        s.forward = *v == "1";
    if (auto v = next())
        s.byFrames = *v == "1";
    if (auto v = next())
        s.tagTimes = *v == "1";
    if (auto v = next())
        s.fromStartTime = *v == "1";
    if (auto v = next())
        s.moveToVideoTime = *v == "1";
    if (auto v = next())
        s.moveToAudioTime = *v == "1";
    if (auto v = next())
        s.whichLines = std::atoi(v->c_str());
    if (auto v = next())
        s.styles = std::u8string(v->begin(), v->end());
    if (auto v = next())
        s.whichTimes = std::atoi(v->c_str());
    if (auto v = next())
        s.correctEndTimes = std::atoi(v->c_str());
    return s;
}

} // namespace hikari::application
