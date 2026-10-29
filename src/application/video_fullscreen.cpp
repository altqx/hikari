#include "hikari/application/video_fullscreen.h"

#include "hikari/core/text_projection.h"

#include <algorithm>
#include <cstdio>

namespace hikari::application {

int fullscreenMonitor(int monitor, const std::vector<MonitorRect> &monitors, const MonitorRect &program)
{
    if (monitors.empty())
        return -1;
    if (monitor == -1 || monitors.size() == 1)
        return 0;
    if (monitor == 0) {
        const int x = (program.width / 2) + program.x;
        const int y = (program.height / 2) + program.y;
        for (std::size_t i = 0; i < monitors.size(); ++i)
            if (monitors[i].contains(x, y))
                return static_cast<int>(i);
        return 0;
    }
    if (monitor > 0 && monitor < static_cast<int>(monitors.size()))
        return monitor;
    return 0;
}

bool fullscreenOwnedByMainWindow(std::string_view platformName)
{
    return !platformName.starts_with("wayland");
}

int fullscreenMonitorEntries(int monitors)
{
    return monitors > 1 ? monitors - 1 : 0;
}

FullscreenBarMode fullscreenBarMode(std::int64_t setting)
{
    switch (setting) {
    case 1: return FullscreenBarMode::Always;
    case 2: return FullscreenBarMode::Never;
    default: return FullscreenBarMode::Auto;
    }
}

FullscreenBarMode nextFullscreenBarMode(FullscreenBarMode mode)
{
    switch (mode) {
    case FullscreenBarMode::Auto: return FullscreenBarMode::Always;
    case FullscreenBarMode::Always: return FullscreenBarMode::Never;
    case FullscreenBarMode::Never: break;
    }
    return FullscreenBarMode::Auto;
}

bool fullscreenIdle(const FullscreenBarState &state)
{
    return !state.awake && state.playing && !state.menuOpen && !state.pointerOverBar;
}

bool fullscreenBarShown(FullscreenBarMode mode, const FullscreenBarState &state)
{
    switch (mode) {
    case FullscreenBarMode::Always: return true;
    case FullscreenBarMode::Never: return false;
    case FullscreenBarMode::Auto: break;
    }
    return !fullscreenIdle(state);
}

std::string fullscreenLineText(std::string_view text)
{
    const std::string_view input = fullscreenLineInput(text);
    const std::u16string raw = core::toUtf16(std::u8string_view(reinterpret_cast<const char8_t *>(input.data()), input.size()));
    std::u16string out;
    bool space = false;
    bool lineBreak = false;
    bool full = false;
    const auto put = [&](char16_t c) {
        if (out.size() >= kFullscreenLineLabelUnits && (c < 0xDC00 || c > 0xDFFF)) {
            full = true; // a low surrogate still ends its pair
            return;
        }
        if (!out.empty() && lineBreak)
            out += u" \u21B5 ";
        else if (!out.empty() && space)
            out += u' ';
        space = lineBreak = false;
        out += c;
    };
    for (const auto &span : core::project(raw).spans) {
        if (full)
            break;
        switch (span.kind) {
        case core::SpanKind::Text:
            for (std::size_t i = span.rawStart; i < span.rawEnd && !full; ++i) {
                const char16_t c = raw[i];
                if (c == u' ' || c == u'\t' || c == u'\r' || c == u'\n' || c == u'\v' || c == u'\f' ||
                    c == u'\u0085' || c == u'\u2028' || c == u'\u2029')
                    space = true;
                else
                    put(c);
            }
            break;
        case core::SpanKind::HardBreak:
        case core::SpanKind::SoftBreak: lineBreak = true; break;
        case core::SpanKind::HardSpace: space = true; break;
        case core::SpanKind::HiddenOverride:
        case core::SpanKind::DrawingPayload:
        case core::SpanKind::Malformed: break;
        }
    }
    const std::u8string utf8 = core::toUtf8(out);
    return {reinterpret_cast<const char *>(utf8.data()), utf8.size()};
}

std::string_view fullscreenLineInput(std::string_view text)
{
    std::size_t cut = std::min(text.size(), kFullscreenLineInputBytes);
    while (cut > 0 && cut < text.size() && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
        --cut; // UTF-8 continuation byte
    return text.substr(0, cut);
}

std::string fullscreenOneRow(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == '\r' || c == '\n' || c == '\v' || c == '\f') {
            out += ' ';
        } else if (c == 0xC2 && i + 1 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0x85) {
            out += ' '; // NEL
            ++i;
        } else if (c == 0xE2 && i + 2 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0x80 &&
                   (static_cast<unsigned char>(text[i + 2]) == 0xA8 || static_cast<unsigned char>(text[i + 2]) == 0xA9)) {
            out += ' '; // U+2028, U+2029
            i += 2;
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}

namespace {

std::string tmpTime(std::int64_t ms)
{
    const int mstime = static_cast<int>(ms);
    const int sec = mstime / 1000;
    const int min = mstime / 60000;
    const int hours = mstime / 3600000;
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%02i:%02i:%02i", hours, min % 60, sec % 60);
    return buffer;
}

} // namespace

std::string fullscreenProgressText(std::int64_t positionMs, std::int64_t durationMs)
{
    return tmpTime(positionMs) + " / " + tmpTime(durationMs);
}

FullscreenProgressBar fullscreenProgressBar(int clientWidth, int textWidth, int textHeight, std::int64_t timeMs,
                                            std::int64_t durationMs)
{
    const int w = clientWidth;
    const int fw = textWidth;
    const int fh = textHeight;
    const int progresbarHeight = static_cast<int>(fh * 0.6f);
    const int margin = static_cast<int>(fh * 0.25f);
    FullscreenProgressBar bar;
    bar.textTop = static_cast<int>(progresbarHeight + (margin * 1.5f));
    bar.textBottom = fh + bar.textTop;
    bar.textLeft = w - (fw + margin);
    bar.textRight = w - margin;
    bar.frameLeft = w - (fw + margin);
    bar.frameTop = margin;
    bar.frameRight = w - margin;
    bar.frameBottom = progresbarHeight + margin;
    bar.innerLeft = w - (fw - 1 + margin);
    bar.innerTop = margin + 1;
    bar.innerRight = w - (margin + 1);
    bar.innerBottom = progresbarHeight - 1 + margin;
    const int rw = w - (fw - 2 + margin);
    const int progBarLen = fw - 4;
    bar.barWidth = progresbarHeight - 2;
    bar.barLeft = rw;
    bar.barY = (margin + 1) + (bar.barWidth / 2);
    const int duration = static_cast<int>(durationMs);
    bar.barRight = duration > 0
                       ? static_cast<int>(((static_cast<float>(timeMs) / static_cast<float>(duration)) * progBarLen) + rw)
                       : progBarLen + rw;
    return bar;
}

} // namespace hikari::application
