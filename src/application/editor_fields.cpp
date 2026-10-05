#include "hikari/application/editor_fields.h"

#include "hikari/core/ass_load.h"
#include "hikari/core/ass_save.h"
#include "hikari/core/checked.h"
#include "hikari/core/line_formats.h"
#include "hikari/core/srt.h"

#include <algorithm>
#include <charconv>
#include <climits>
#include <cmath>
#include <regex>
#include <string>

namespace hikari::application {

namespace {

std::int64_t msOf(core::DocumentTime t)
{
    return t.microseconds() / 1000;
}

std::u8string number(std::int64_t n)
{
    const std::string s = std::to_string(n);
    return std::u8string(s.begin(), s.end());
}

// SubsDialogue.h:20 ZEROIT: down to whole centiseconds.
std::int64_t zeroIt(std::int64_t ms)
{
    return (ms / 10) * 10;
}

// SubsTime::raw (SubsTime.cpp:85-116) for a time and its frame.
std::u8string raw(core::SubtitleFormat format, std::int64_t ms, std::int64_t frame)
{
    switch (format) {
    case core::SubtitleFormat::Srt: return core::legacy::srtTimeText(ms);
    case core::SubtitleFormat::TMPlayer: return core::legacy::tmpTimeText(ms);
    case core::SubtitleFormat::MicroDvd: return number(frame);
    case core::SubtitleFormat::Mpl2:
        return number(static_cast<std::int64_t>(std::ceil(static_cast<float>(ms) * (10.0f / 1000.0f))));
    default: return core::legacy::assTimeText(ms);
    }
}

bool exact(const LegacyTimebase *frames)
{
    return frames && frames->exact();
}

int clampInt(std::int64_t v)
{
    return static_cast<int>(std::clamp<std::int64_t>(v, INT_MIN, INT_MAX));
}

std::optional<std::int64_t> digits(std::u8string_view text)
{
    if (text.empty() || text.size() > 18)
        return std::nullopt;
    std::int64_t value = 0;
    for (const char8_t c : text) {
        if (c < u8'0' || c > u8'9')
            return std::nullopt;
        value = value * 10 + (c - u8'0');
    }
    return value;
}

// "H...:MM:SS" followed by `tail` digits after `separator` (none when 0).
std::optional<std::int64_t> clockTime(std::u8string_view text, char8_t separator, std::size_t tail, int scale)
{
    const std::string t(text.begin(), text.end());
    const std::string pattern = tail == 0 ? std::string("^([0-9]{2,}):([0-5][0-9]):([0-5][0-9])$")
                                          : "^([0-9]{2,}):([0-5][0-9]):([0-5][0-9])\\" + std::string(1, char(separator)) +
                                                "([0-9]{" + std::to_string(tail) + "})$";
    std::smatch m;
    if (!std::regex_match(t, m, std::regex(pattern)) || m[1].length() > 9)
        return std::nullopt;
    std::int64_t ms = std::stoll(m[1]) * 3600000 + std::stoll(m[2]) * 60000 + std::stoll(m[3]) * 1000;
    if (tail)
        ms += std::stoll(m[4]) * scale;
    return ms;
}

} // namespace

std::optional<core::DocumentTime> microDvdFrameTime(std::int64_t frame, const std::optional<core::FrameRate> &rate)
{
    if (!rate)
        return std::nullopt;
    const auto seconds = rate->secondsAt(core::VideoFrameIndex(frame));
    if (!seconds)
        return std::nullopt;
    const auto us = core::mulDiv(seconds->numerator(), 1'000'000, seconds->denominator(), 1,
                                 core::Rounding::NearestTiesAway);
    if (!us)
        return std::nullopt;
    return core::DocumentTime(*us);
}

std::optional<MicroDvdTime> microDvdFieldTime(std::int64_t ms, const std::optional<core::FrameRate> &rate)
{
    if (!rate)
        return std::nullopt;
    ms = std::max<std::int64_t>(0, ms);
    const auto &fps = rate->framesPerSecond();
    const auto frame = core::mulDiv(ms * 1000, fps.numerator(), fps.denominator(), 1'000'000, core::Rounding::Ceil);
    if (!frame)
        return std::nullopt;
    const auto time = microDvdFrameTime(*frame, rate);
    if (!time)
        return std::nullopt;
    return MicroDvdTime{*frame, msOf(*time)};
}

EditorTimeTexts editorTimeTexts(const core::LineRecord &line, core::SubtitleFormat format, const LegacyTimebase *frames)
{
    const std::int64_t startMs = msOf(line.start.value), endMs = msOf(line.end.value);
    EditorTimeTexts out;
    if (exact(frames)) {
        // TimeCtrl::SetTime (TimeCtrl.cpp:247-266): Start FrameAt, End FrameAt - 1;
        // SetLine's Duration is their difference (clamped at 0, SubsTime::operator-)
        // plus one (EditBox.cpp:416-419).
        const std::int64_t startFrame = frames->frameAt(clampInt(startMs));
        const std::int64_t endFrame = frames->frameAt(clampInt(endMs)) - 1;
        out.start = number(startFrame);
        out.end = number(endFrame);
        out.duration = number(std::max<std::int64_t>(0, endFrame - startFrame) + 1);
        return out;
    }
    const std::int64_t startFrame = line.startFrame.value_or(0), endFrame = line.endFrame.value_or(0);
    out.start = raw(format, startMs, startFrame);
    out.end = raw(format, endMs, endFrame);
    // line->End - line->Start: both clamped at 0 (SubsTime.cpp:214-222).
    out.duration = raw(format, std::max<std::int64_t>(0, endMs - startMs), std::max<std::int64_t>(0, endFrame - startFrame));
    return out;
}

std::u8string editedDurationText(const core::LineRecord &line, core::SubtitleFormat format, const LegacyTimebase *frames)
{
    const std::int64_t ms = std::max<std::int64_t>(0, msOf(line.end.value) - msOf(line.start.value));
    if (exact(frames))
        return number(frames->frameAt(clampInt(ms))); // SetTime(durTime, false, 1)
    const std::int64_t frame = std::max<std::int64_t>(0, line.endFrame.value_or(0) - line.startFrame.value_or(0));
    return raw(format, ms, frame);
}

std::optional<TypedTime> typedTime(std::u8string_view text, TimeFieldRole role, core::SubtitleFormat format,
                                   const std::optional<core::FrameRate> &microDvdRate, const LegacyTimebase *frames)
{
    TypedTime out;
    if (exact(frames)) {
        // TimeCtrl::GetTime (TimeCtrl.cpp:268-286): the frame's time, ZEROIT.
        const auto frame = digits(text);
        if (!frame || *frame > INT_MAX)
            return std::nullopt;
        const int f = static_cast<int>(*frame);
        const int ms = role == TimeFieldRole::Start ? frames->startTimeFor(f)
                       : role == TimeFieldRole::End ? frames->endTimeFor(f)
                                                    : frames->msAt(f);
        out.ms = zeroIt(ms);
        if (format == core::SubtitleFormat::MicroDvd)
            out.frame = *frame; // the SubsTime keeps the typed frame (ChangeFormat skips FRAME)
        return out;
    }
    switch (format) {
    case core::SubtitleFormat::Srt: {
        const auto ms = clockTime(text, u8',', 3, 1);
        if (!ms)
            return std::nullopt;
        out.ms = *ms;
        return out;
    }
    case core::SubtitleFormat::TMPlayer: {
        const auto ms = clockTime(text, 0, 0, 0);
        if (!ms)
            return std::nullopt;
        out.ms = *ms;
        return out;
    }
    case core::SubtitleFormat::MicroDvd: {
        // SubsTime::ParseMS (SubsTime.cpp:74-78): the frame; its time from
        // the Document's rate (C01-fps-isolation) or unresolved.
        const auto frame = digits(text);
        if (!frame)
            return std::nullopt;
        out.frame = *frame;
        const auto time = microDvdFrameTime(*frame, microDvdRate);
        out.timeResolved = time.has_value();
        out.ms = time ? msOf(*time) : 0;
        return out;
    }
    case core::SubtitleFormat::Mpl2: {
        const auto tenths = digits(text);
        if (!tenths)
            return std::nullopt;
        out.ms = *tenths * 100; // ParseMS: result * 100
        return out;
    }
    default:
        if (!core::legacy::isCanonicalAssTime(text))
            return std::nullopt;
        out.ms = core::legacy::assTimeMilliseconds(text);
        return out;
    }
}

std::int64_t fieldPrecisionTime(core::SubtitleFormat format, std::int64_t ms)
{
    ms = std::max<std::int64_t>(0, ms);
    switch (format) {
    case core::SubtitleFormat::Ass:
    case core::SubtitleFormat::PlainText: return (ms / 10) * 10;
    case core::SubtitleFormat::TMPlayer: return (ms / 1000) * 1000;
    case core::SubtitleFormat::Mpl2:
        return static_cast<std::int64_t>(std::ceil(static_cast<float>(ms) * (10.0f / 1000.0f))) * 100;
    default: return ms;
    }
}

std::optional<std::int64_t> layerValue(std::u8string_view text)
{
    constexpr std::int64_t kMin = -10000000, kMax = 10000000;
    std::string t(text.begin(), text.end());
    // NumCtrl::GetString (NumCtrl.cpp): "," as ".", a lone "-" keeps the old value.
    std::replace(t.begin(), t.end(), ',', '.');
    if (t.empty() || t == "-")
        return std::nullopt;
    if (t.front() == '.')
        t.insert(t.begin(), '0');
    if (t.back() == '.')
        t.push_back('0');
    double value = 0;
    const auto [end, ec] = std::from_chars(t.data(), t.data() + t.size(), value);
    if (ec == std::errc::result_out_of_range)
        value = t.front() == '-' ? kMin : kMax;
    else if (ec != std::errc() || end != t.data() + t.size())
        return std::nullopt;
    value = std::clamp<double>(value, kMin, kMax);
    return static_cast<std::int64_t>(value); // NumCtrl::GetInt truncates
}

int legacyAlignment(const core::LineRecord &line, int styleAlignment)
{
    const std::u8string &text = !line.translation.empty() ? line.translation : line.text;
    const std::string t(text.begin(), text.end());
    std::smatch m;
    // wxRegEx "\\\\an([0-9]+)" (ADVANCED): the first match in the text.
    if (std::regex_search(t, m, std::regex(R"(\\an([0-9]+))"))) {
        // wxAtoi of the digits (an int; very long numbers are out of the list anyway)
        const std::string d = m[1].str();
        return d.size() > 9 ? INT_MAX : std::stoi(d);
    }
    return styleAlignment;
}

} // namespace hikari::application
