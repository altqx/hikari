#include "hikari/application/jump_target.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string>
#include <vector>

namespace hikari::application {

namespace {

constexpr std::int64_t kMaxMs = std::int64_t(1000) * 60 * 60 * 1000;

std::string_view trimmed(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    return text;
}

std::optional<std::int64_t> digits(std::string_view text)
{
    if (text.empty() || text.size() > 12)
        return std::nullopt;
    std::int64_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9')
            return std::nullopt;
        value = value * 10 + (c - '0');
    }
    return value;
}

// "h:mm:ss.fff", "m:ss.f", "s.f" or "s": milliseconds.
std::optional<std::int64_t> time(std::string_view text)
{
    std::vector<std::string_view> parts;
    for (std::size_t from = 0;;) {
        const std::size_t colon = text.find(':', from);
        parts.push_back(text.substr(from, colon == std::string_view::npos ? std::string_view::npos : colon - from));
        if (colon == std::string_view::npos)
            break;
        from = colon + 1;
    }
    if (parts.size() > 3)
        return std::nullopt;
    std::string_view last = parts.back();
    std::int64_t fraction = 0;
    if (const std::size_t dot = last.find('.'); dot != std::string_view::npos) {
        const std::string_view decimals = last.substr(dot + 1);
        if (decimals.empty() || decimals.size() > 3 || !digits(decimals))
            return std::nullopt;
        fraction = *digits(decimals);
        for (std::size_t i = decimals.size(); i < 3; ++i)
            fraction *= 10;
        last = last.substr(0, dot);
    }
    parts.back() = last;
    std::int64_t seconds = 0;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const auto value = digits(parts[i]);
        if (!value || (i > 0 && (parts[i].size() > 2 || *value >= 60)))
            return std::nullopt;
        seconds = seconds * 60 + *value;
    }
    const std::int64_t ms = seconds * 1000 + fraction;
    return ms <= kMaxMs ? std::optional(ms) : std::nullopt;
}

}

std::optional<JumpTarget> parseJumpTarget(std::string_view text)
{
    text = trimmed(text);
    if (text.empty())
        return std::nullopt;
    const char sign = text.front();
    if (sign != '+' && sign != '-') {
        if (text.find_first_of(":.") == std::string_view::npos) {
            const auto frame = digits(text);
            return frame ? std::optional(JumpTarget{JumpTarget::Kind::Frame, *frame}) : std::nullopt;
        }
        const auto ms = time(text);
        return ms ? std::optional(JumpTarget{JumpTarget::Kind::Time, *ms}) : std::nullopt;
    }
    std::string_view step = trimmed(text.substr(1));
    const std::int64_t direction = sign == '-' ? -1 : 1;
    if (step.empty())
        return std::nullopt;
    const char unit = char(std::tolower(static_cast<unsigned char>(step.back())));
    if (unit == 'f') {
        const auto frames = digits(trimmed(step.substr(0, step.size() - 1)));
        return frames ? std::optional(JumpTarget{JumpTarget::Kind::FrameStep, direction * *frames}) : std::nullopt;
    }
    if (unit == 's')
        step = trimmed(step.substr(0, step.size() - 1));
    else if (step.find(':') == std::string_view::npos)
        return std::nullopt;
    const auto ms = time(step);
    return ms ? std::optional(JumpTarget{JumpTarget::Kind::TimeStep, direction * *ms}) : std::nullopt;
}

std::int64_t jumpFrame(const JumpTarget &target, const JumpClock &clock)
{
    const std::int64_t last = std::max<std::int64_t>(0, clock.frameCount - 1);
    std::int64_t frame = 0;
    switch (target.kind) {
    case JumpTarget::Kind::Frame:
        frame = target.value;
        break;
    case JumpTarget::Kind::FrameStep:
        frame = clock.shownFrame + target.value;
        break;
    case JumpTarget::Kind::Time:
        frame = clock.frameAt(target.value);
        break;
    case JumpTarget::Kind::TimeStep:
        frame = clock.frameAt(std::max<std::int64_t>(0, clock.timeOf(clock.shownFrame) + target.value));
        break;
    }
    return std::clamp<std::int64_t>(frame, 0, last);
}

}
