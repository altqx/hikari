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
#include <cstdlib>
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

// MUST KILL: zeroIt truncates to whole centiseconds
std::int64_t zeroIt(std::int64_t ms)
{
    return (ms / 10) * 10;
}

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

}

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
    out.duration = raw(format, std::max<std::int64_t>(0, endMs - startMs), std::max<std::int64_t>(0, endFrame - startFrame));
    return out;
}

std::u8string editedDurationText(const core::LineRecord &line, core::SubtitleFormat format, const LegacyTimebase *frames)
{
    const std::int64_t ms = std::max<std::int64_t>(0, msOf(line.end.value) - msOf(line.start.value));
    if (exact(frames))
        return number(frames->frameAt(clampInt(ms)));
    const std::int64_t frame = std::max<std::int64_t>(0, line.endFrame.value_or(0) - line.startFrame.value_or(0));
    return raw(format, ms, frame);
}

std::optional<TypedTime> typedTime(std::u8string_view text, TimeFieldRole role, core::SubtitleFormat format,
                                   const std::optional<core::FrameRate> &microDvdRate, const LegacyTimebase *frames)
{
    TypedTime out;
    if (text.empty() && timeFieldForm(format, frames) == TimeFieldForm::Number)
        text = u8"0";
    if (exact(frames)) {
        const auto frame = digits(text);
        if (!frame || *frame > INT_MAX)
            return std::nullopt;
        const int f = static_cast<int>(*frame);
        const int ms = role == TimeFieldRole::Start ? frames->startTimeFor(f)
                       : role == TimeFieldRole::End ? frames->endTimeFor(f)
                                                    : frames->msAt(f);
        out.ms = zeroIt(ms);
        if (format == core::SubtitleFormat::MicroDvd)
            out.frame = *frame;
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
        out.ms = *tenths * 100;
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

TimeFieldForm timeFieldForm(core::SubtitleFormat format, const LegacyTimebase *frames)
{
    if (exact(frames))
        return TimeFieldForm::Number;
    switch (format) {
    case core::SubtitleFormat::Srt: return TimeFieldForm::Srt;
    case core::SubtitleFormat::TMPlayer: return TimeFieldForm::TMPlayer;
    case core::SubtitleFormat::MicroDvd:
    case core::SubtitleFormat::Mpl2: return TimeFieldForm::Number;
    default: return TimeFieldForm::Ass;
    }
}

namespace {

bool isDigit(char16_t c)
{
    return c >= u'0' && c <= u'9';
}

bool isSeparator(char16_t c)
{
    return c == u':' || c == u'.' || c == u',';
}

TimeFieldEdit refused(const TimeFieldState &field)
{
    return TimeFieldEdit{field, false, true};
}

TimeFieldEdit edited(std::u16string text, std::size_t start, std::size_t end)
{
    return TimeFieldEdit{TimeFieldState{std::move(text), start, end}, true, false};
}

std::pair<std::size_t, std::size_t> span(const TimeFieldState &field)
{
    const std::size_t n = field.text.size();
    const std::size_t a = std::min(field.selectionStart, n), b = std::min(field.selectionEnd, n);
    return {std::min(a, b), std::max(a, b)};
}

TimeFieldEdit replaceInNumber(const TimeFieldState &field, std::u16string_view digits, bool cut)
{
    const auto [from, to] = span(field);
    const std::size_t kept = field.text.size() - (to - from);
    if (kept >= kTimeFieldDigits || (!cut && kept + digits.size() > kTimeFieldDigits))
        return refused(field);
    digits = digits.substr(0, kTimeFieldDigits - kept);
    std::u16string text = field.text.substr(0, from);
    text += digits;
    text += field.text.substr(to);
    return edited(std::move(text), from + digits.size(), from + digits.size());
}

}

TimeFieldEdit typeIntoTimeField(TimeFieldForm form, const TimeFieldState &field, char16_t c)
{
    if (!isDigit(c))
        return refused(field);
    if (form == TimeFieldForm::Number)
        return replaceInNumber(field, std::u16string_view(&c, 1), false);
    auto [from, to] = span(field);
    std::u16string txt = field.text;
    if (from != to)
        for (std::size_t i = from; i < to; ++i)
            if (isDigit(txt[i]))
                txt[i] = u'0';
    if (from >= txt.size())
        return refused(field);
    std::size_t caret = from + 1;
    if (isSeparator(txt[from])) {
        txt[from + 1] = c;
        ++caret;
    } else {
        txt[from] = c;
    }
    if (caret > 1 && txt[caret - 2] == u':' && txt[caret - 1] > u'5')
        txt[caret - 1] = u'5';
    return edited(std::move(txt), caret, caret);
}

TimeFieldEdit backspaceTimeField(const TimeFieldState &field)
{
    auto [from, to] = span(field);
    if (from == to) {
        if (from == 0)
            return TimeFieldEdit{field, false, false};
        --from;
    }
    std::u16string txt = field.text;
    for (std::size_t i = from; i < to; ++i)
        if (!isSeparator(txt[i]))
            txt[i] = u'0';
    if (from > 0 && isSeparator(txt[from - 1]))
        --from;
    const bool changed = txt != field.text;
    return TimeFieldEdit{TimeFieldState{std::move(txt), from, from}, changed, false};
}

TimeFieldEdit pasteIntoTimeField(TimeFieldForm form, const TimeFieldState &field, std::u16string_view pasted)
{
    if (form == TimeFieldForm::Number) {
        if (pasted.empty() || !std::all_of(pasted.begin(), pasted.end(), isDigit))
            return refused(field);
        return replaceInNumber(field, pasted, true);
    }
    // MUST KILL: mask chars: '0' any digit, '5' a digit up to 5, else a literal separator
    const std::u16string_view mask = form == TimeFieldForm::Ass   ? u"0:50:50.00"
                                     : form == TimeFieldForm::Srt ? u"00:50:50,000"
                                                                  : u"00:50:50";
    if (pasted.size() != mask.size())
        return refused(field);
    for (std::size_t i = 0; i < mask.size(); ++i) {
        const bool digit = mask[i] == u'0' || mask[i] == u'5';
        if (digit ? !isDigit(pasted[i]) || (mask[i] == u'5' && pasted[i] > u'5') : pasted[i] != mask[i])
            return refused(field);
    }
    return edited(std::u16string(pasted), 0, pasted.size());
}

namespace {

// Hours before the first ':', then two digits each of minutes and seconds,
// then ASS centiseconds or SRT milliseconds.
std::int64_t clockMs(TimeFieldForm form, std::u16string_view text)
{
    const std::size_t colon = text.find(u':');
    const auto at = [&](std::size_t from, std::size_t count) -> std::int64_t {
        std::int64_t v = 0;
        for (std::size_t i = from; i < from + count && i < text.size(); ++i) {
            if (text[i] < u'0' || text[i] > u'9')
                break;
            v = v * 10 + (text[i] - u'0');
        }
        return v;
    };
    if (colon == std::u16string_view::npos)
        return 0;
    std::int64_t ms = at(0, colon) * 3600000 + at(colon + 1, 2) * 60000 + at(colon + 4, 2) * 1000;
    if (form == TimeFieldForm::Ass)
        ms += at(colon + 7, 2) * 10;
    else if (form == TimeFieldForm::Srt)
        ms += at(colon + 7, 3);
    return ms;
}

std::u16string clockText(TimeFieldForm form, std::int64_t ms)
{
    const std::u8string t = form == TimeFieldForm::Srt        ? core::legacy::srtTimeText(ms)
                            : form == TimeFieldForm::TMPlayer ? core::legacy::tmpTimeText(ms)
                                                              : core::legacy::assTimeText(ms);
    return std::u16string(t.begin(), t.end());
}

}

std::int64_t timeFieldStep(TimeFieldForm form, std::size_t column)
{
    if (form == TimeFieldForm::Number)
        return 1;
    const std::size_t start = form == TimeFieldForm::Ass ? 2 : 3;
    if (column < start)
        return 3600000;
    if (column < start + 3)
        return 60000;
    if (column < start + 6)
        return 1000;
    return 10;
}

TimeCtrlForm timeCtrlForm(core::SubtitleFormat format)
{
    switch (format) {
    case core::SubtitleFormat::Srt: return TimeCtrlForm::Srt;
    case core::SubtitleFormat::TMPlayer: return TimeCtrlForm::TMPlayer;
    case core::SubtitleFormat::MicroDvd: return TimeCtrlForm::MicroDvd;
    case core::SubtitleFormat::Mpl2: return TimeCtrlForm::Mpl2;
    default: return TimeCtrlForm::Ass;
    }
}

namespace {

TimeFieldForm clockForm(TimeCtrlForm form)
{
    return form == TimeCtrlForm::Srt        ? TimeFieldForm::Srt
           : form == TimeCtrlForm::TMPlayer ? TimeFieldForm::TMPlayer
                                            : TimeFieldForm::Ass;
}

bool shownAsNumber(TimeFieldMouse mouse)
{
    return mouse.frames || mouse.form == TimeCtrlForm::MicroDvd || mouse.form == TimeCtrlForm::Mpl2;
}

bool blank(std::u16string_view text)
{
    return std::ranges::all_of(text, [](char16_t c) { return c == u' ' || c == u'\t'; });
}

std::int64_t fieldNumber(std::u16string_view text)
{
    std::int64_t n = 0;
    for (const char16_t c : text)
        if (c >= u'0' && c <= u'9')
            n = n * 10 + (c - u'0');
    return n;
}

std::u16string numberText(std::int64_t n)
{
    const std::string t = std::to_string(n);
    return std::u16string(t.begin(), t.end());
}

// MUST KILL: kMaxFieldNumber hard-codes kTimeFieldDigits nines
constexpr std::int64_t kMaxFieldNumber = 999999999;

std::int64_t mouseMax(TimeFieldMouse mouse)
{
    if (!shownAsNumber(mouse))
        return kTimeFieldMaxMs;
    return !mouse.frames && mouse.form == TimeCtrlForm::Mpl2 ? kTimeFieldMaxMs / 100 : kMaxFieldNumber;
}

std::int64_t mouseValue(TimeFieldMouse mouse, std::u16string_view text)
{
    if (shownAsNumber(mouse))
        return fieldNumber(text);
    return blank(text) ? 0 : clockMs(clockForm(mouse.form), text);
}

std::u16string mouseText(TimeFieldMouse mouse, std::int64_t value)
{
    return shownAsNumber(mouse) ? numberText(value) : clockText(clockForm(mouse.form), value);
}

}

std::int64_t timeFieldStep(TimeFieldMouse mouse, std::size_t column)
{
    if (shownAsNumber(mouse))
        return 1;
    return timeFieldStep(clockForm(mouse.form), column);
}

std::optional<std::u16string> wheelTimeField(TimeFieldMouse mouse, std::u16string_view text, std::size_t column,
                                             int notches)
{
    if (notches == 0)
        return std::nullopt;
    const std::int64_t value =
        mouseValue(mouse, text) + static_cast<std::int64_t>(notches) * timeFieldStep(mouse, column);
    if (value < 0 || value > mouseMax(mouse))
        return std::nullopt;
    return mouseText(mouse, value);
}

TimeFieldDrag beginTimeFieldDrag(TimeFieldMouse mouse, std::u16string_view text, std::size_t column, int x, int y)
{
    return TimeFieldDrag{mouseValue(mouse, text), timeFieldStep(mouse, column), x, y};
}

std::optional<std::u16string> dragTimeField(TimeFieldMouse mouse, TimeFieldDrag &drag, int x, int y)
{
    if (drag.step == 0)
        return std::nullopt;
    const int absY = std::abs(y - drag.y), absX = std::abs(x - drag.x);
    const std::int64_t from = drag.value;
    bool changed = false;
    if (absY >= absX) {
        if (absY >= 8) {
            drag.value = drag.y < y ? drag.value - drag.step : drag.value + drag.step;
            drag.y = y;
            drag.x = x;
            changed = true;
        }
    } else if (absX >= 10) {
        const std::int64_t step = drag.step * 10;
        drag.value = drag.x > x ? drag.value - step : drag.value + step;
        drag.x = x;
        drag.y = y;
        changed = true;
    }
    drag.value = std::clamp<std::int64_t>(drag.value, 0, mouseMax(mouse));
    if (!changed || (shownAsNumber(mouse) && drag.value == from))
        return std::nullopt;
    return mouseText(mouse, drag.value);
}


std::optional<std::int64_t> numberFieldValue(std::u16string_view text)
{
    std::string t;
    for (const char16_t c : text)
        t.push_back(c == u',' ? '.' : c < 0x80 ? static_cast<char>(c) : '?');
    if (t.empty() || t == "-")
        return std::nullopt;
    double value = 0;
    const auto [end, ec] = std::from_chars(t.data(), t.data() + t.size(), value);
    if (ec != std::errc() || end != t.data() + t.size() || !std::isfinite(value))
        return std::nullopt;
    if (value >= 9.2e18 || value <= -9.2e18)
        return std::nullopt;
    return static_cast<std::int64_t>(value);
}

namespace {

std::optional<std::u16string> stepNumber(std::int64_t &value, std::int64_t delta, NumberFieldRange range)
{
    const std::int64_t next = std::clamp(value + delta, range.from, range.to);
    if (next == value)
        return std::nullopt;
    value = next;
    return numberText(next);
}

}

std::optional<std::u16string> wheelNumberField(std::int64_t &value, int notches, NumberFieldRange range)
{
    if (notches == 0)
        return std::nullopt;
    return stepNumber(value, notches, range);
}

std::optional<std::u16string> dragNumberField(NumberFieldDrag &drag, int x, int y, NumberFieldRange range)
{
    const int absY = std::abs(y - drag.y), absX = std::abs(x - drag.x);
    std::int64_t delta = 0;
    if (absY >= absX) {
        if (absY < 8)
            return std::nullopt;
        delta = drag.y < y ? -1 : 1;
    } else {
        if (absX < 10)
            return std::nullopt;
        delta = drag.x > x ? -10 : 10;
    }
    auto text = stepNumber(drag.value, delta, range);
    if (text) {
        drag.x = x;
        drag.y = y;
    }
    return text;
}

std::optional<std::int64_t> layerValue(std::u8string_view text)
{
    constexpr std::int64_t kMin = -10000000, kMax = 10000000;
    std::string t(text.begin(), text.end());
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
    return static_cast<std::int64_t>(value);
}

int legacyAlignment(const core::LineRecord &line, int styleAlignment)
{
    const std::u8string &text = !line.translation.empty() ? line.translation : line.text;
    const std::string t(text.begin(), text.end());
    std::smatch m;
    if (std::regex_search(t, m, std::regex(R"(\\an([0-9]+))"))) {
        const std::string d = m[1].str();
        return d.size() > 9 ? INT_MAX : std::stoi(d);
    }
    return styleAlignment;
}

}
