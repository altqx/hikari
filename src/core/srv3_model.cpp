// Ported from YTSubConverter 1.6.5, Copyright (c) 2018 arcusmaximus, MIT licence (see THIRDPARTY).
#include "srv3_model.h"

#include <cmath>

namespace hikari::core::srv3 {

bool isLeftAligned(AnchorPoint p)
{
    return p == AnchorPoint::TopLeft || p == AnchorPoint::MiddleLeft || p == AnchorPoint::BottomLeft;
}

bool isCenterAligned(AnchorPoint p)
{
    return p == AnchorPoint::TopCenter || p == AnchorPoint::Center || p == AnchorPoint::BottomCenter;
}

bool isRightAligned(AnchorPoint p)
{
    return p == AnchorPoint::TopRight || p == AnchorPoint::MiddleRight || p == AnchorPoint::BottomRight;
}

bool isTopAligned(AnchorPoint p)
{
    return p == AnchorPoint::TopLeft || p == AnchorPoint::TopCenter || p == AnchorPoint::TopRight;
}

bool isMiddleAligned(AnchorPoint p)
{
    return p == AnchorPoint::MiddleLeft || p == AnchorPoint::Center || p == AnchorPoint::MiddleRight;
}

bool isBottomAligned(AnchorPoint p)
{
    return p == AnchorPoint::BottomLeft || p == AnchorPoint::BottomCenter || p == AnchorPoint::BottomRight;
}

namespace {

constexpr double kFrameMs = 33.36666666666667;

double totalMs(Ticks span)
{
    return static_cast<double>(span) / static_cast<double>(kTicksPerMs);
}

Ticks frameToTime(int frame)
{
    if (frame == 0)
        return kTimeBase;
    const int ms = static_cast<int>(frame * kFrameMs);
    return kTimeBase + static_cast<Ticks>(ms) * kTicksPerMs;
}

}

int totalMilliseconds(Ticks span)
{
    return static_cast<int>(totalMs(span));
}

Ticks addMilliseconds(Ticks time, double ms)
{
    const double integral = std::trunc(ms);
    const double fraction = ms - integral;
    return time + static_cast<Ticks>(integral) * kTicksPerMs + static_cast<Ticks>(fraction * kTicksPerMs);
}

int endTimeToFrame(Ticks time)
{
    return static_cast<int>((totalMs(time - kTimeBase) + 1) / kFrameMs);
}

int startTimeToFrame(Ticks time)
{
    if (time <= kTimeBase)
        return 0;
    return endTimeToFrame(time) + 1;
}

Ticks frameToStartTime(int frame)
{
    if (frame <= 0)
        return kTimeBase;
    return addMilliseconds(frameToTime(frame), -16);
}

Ticks frameToEndTime(int frame)
{
    return addMilliseconds(frameToTime(frame), 16);
}

Ticks roundTimeToFrameCenter(Ticks time)
{
    if (time <= kTimeBase)
        return kTimeBase;
    return frameToStartTime(startTimeToFrame(time));
}

int Animation::interpolate(int from, int to, float t) const
{
    return from + static_cast<int>((to - from) * std::pow(static_cast<double>(t), static_cast<double>(acceleration)));
}

float Animation::interpolate(float from, float to, float t) const
{
    return from + (to - from) * static_cast<float>(std::pow(static_cast<double>(t), static_cast<double>(acceleration)));
}

Color Animation::interpolate(Color from, Color to, float t) const
{
    return Color::fromArgb(interpolate(from.a(), to.a(), t), interpolate(from.r(), to.r(), t), interpolate(from.g(), to.g(), t),
                           interpolate(from.b(), to.b(), t));
}

void ColourAnimation::apply(Line &, Section *section, float t)
{
    const Color colour = interpolate(startColor, endColor, t);
    switch (m_kind) {
    case Kind::ForeColor:
        section->foreColor = colour;
        break;
    case Kind::BackColor:
        section->backColor = colour;
        break;
    case Kind::SecondaryColor:
        section->secondaryColor = colour;
        break;
    default:
        section->shadowColors.set(shadowType, colour);
        break;
    }
}

void FadeAnimation::apply(Line &line, Section *, float t)
{
    line.alpha = interpolate(startAlpha, endAlpha, t);
}

void MoveAnimation::apply(Line &line, Section *, float t)
{
    const float x = interpolate(startPosition.x, endPosition.x, t);
    const float y = interpolate(startPosition.y, endPosition.y, t);
    line.position = PointF{x, y};
}

void ScaleAnimation::apply(Line &, Section *section, float t)
{
    section->scale = interpolate(startScale, endScale, t);
}

ShakeAnimation::ShakeAnimation(Ticks start, Ticks end, PointF radius)
    : Animation(start, end, 1), m_radius(radius), m_random(totalMilliseconds(start - kTimeBase))
{
}

void ShakeAnimation::apply(Line &line, Section *, float t)
{
    if (t <= 0 || t >= 1 || !line.position)
        return;
    const float x = line.position->x + m_radius.x * (static_cast<float>(m_random.nextDouble()) * 2 - 1.0f);
    const float y = line.position->y + m_radius.y * (static_cast<float>(m_random.nextDouble()) * 2 - 1.0f);
    line.position = PointF{x, y};
}

GlitchingCharAnimation::GlitchingCharAnimation(Ticks start, Ticks end, std::vector<CharacterRange> ranges)
    : Animation(start, end, 1), m_ranges(std::move(ranges)), m_random(totalMilliseconds(start - kTimeBase))
{
}

void GlitchingCharAnimation::apply(Line &, Section *section, float t)
{
    if (!(t > 0 && t < 1))
        return;
    const CharacterRange &range = m_ranges[static_cast<std::size_t>(m_random.next(static_cast<int>(m_ranges.size())))];
    section->text = u16(1, static_cast<char16_t>(range.start + m_random.next(range.end - range.start)));
}

Animations::Animations(const Animations &other)
{
    items.reserve(other.items.size());
    for (const auto &a : other.items)
        items.push_back(a->clone());
}

Animations &Animations::operator=(const Animations &other)
{
    if (this != &other)
        *this = Animations(other);
    return *this;
}

bool Animations::hasKind(Animation::Kind kind) const
{
    for (const auto &a : items)
        if (a->kind() == kind)
            return true;
    return false;
}

u16 Line::text() const
{
    u16 out;
    for (const Section &s : sections)
        out += s.text;
    return out;
}

void Line::normalizeAlpha()
{
    if (alpha == 255)
        return;
    const float factor = static_cast<float>(alpha) / 255.0f;
    auto multiply = [factor](Color c) {
        if (c.isEmpty())
            return c;
        return changeAlpha(c, static_cast<int>(static_cast<float>(c.a()) * factor));
    };
    for (Section &section : sections) {
        section.foreColor = multiply(section.foreColor);
        section.backColor = multiply(section.backColor);
        for (const auto &[type, colour] : section.shadowColors.entries())
            section.shadowColors.set(type, multiply(colour));
    }
    alpha = 255;
}

}
