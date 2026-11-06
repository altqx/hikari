#pragma once

#include "srv3_net.h"

#include <memory>
#include <optional>
#include <vector>

namespace hikari::core::srv3 {

enum class AnchorPoint { TopLeft, TopCenter, TopRight, MiddleLeft, Center, MiddleRight, BottomLeft, BottomCenter, BottomRight };
enum class ShadowType { Glow, Bevel, HardShadow, SoftShadow };
enum class OffsetType { Regular, Subscript, Superscript };
enum class RubyPart { None, Base, Parenthesis, TextBefore, TextAfter };
enum class HorizontalTextDirection { LeftToRight, RightToLeft };
enum class VerticalTextType { None, Positioned, Rotated };
enum class LineMergeType { MoveNew, MoveExisting };

bool isLeftAligned(AnchorPoint p);
bool isCenterAligned(AnchorPoint p);
bool isRightAligned(AnchorPoint p);
bool isTopAligned(AnchorPoint p);
bool isMiddleAligned(AnchorPoint p);
bool isBottomAligned(AnchorPoint p);

inline constexpr Ticks kTimeBase = 0;
int startTimeToFrame(Ticks time);
int endTimeToFrame(Ticks time);
Ticks frameToStartTime(int frame);
Ticks frameToEndTime(int frame);
Ticks roundTimeToFrameCenter(Ticks time);
// DateTime.AddMilliseconds(double) as of .NET 7.
Ticks addMilliseconds(Ticks time, double ms);
// (int)TimeSpan.TotalMilliseconds
int totalMilliseconds(Ticks span);

struct Line;
struct Section;

struct CharacterRange {
    char16_t start, end;
    bool contains(char16_t c) const { return c >= start && c < end; }
};

class Animation {
public:
    enum class Kind { ForeColor, BackColor, SecondaryColor, ShadowColor, Fade, Move, Scale, Shake, GlitchingChar };

    virtual ~Animation() = default;
    virtual Kind kind() const = 0;
    virtual bool affectsPast() const = 0;
    virtual bool affectsText() const { return false; }
    virtual void apply(Line &line, Section *section, float t) = 0;
    virtual std::shared_ptr<Animation> clone() const = 0;
    bool isColour() const { return kind() <= Kind::ShadowColor; }

    Ticks startTime = 0, endTime = 0;
    float acceleration = 1;

protected:
    Animation(Ticks start, Ticks end, float accel) : startTime(start), endTime(end), acceleration(accel) {}
    int interpolate(int from, int to, float t) const;
    float interpolate(float from, float to, float t) const;
    Color interpolate(Color from, Color to, float t) const;
};

class ColourAnimation : public Animation {
public:
    ColourAnimation(Kind kind, Ticks start, Color startColour, Ticks end, Color endColour, float accel,
                    ShadowType shadow = ShadowType::Glow)
        : Animation(start, end, accel), startColor(startColour), endColor(endColour), shadowType(shadow), m_kind(kind)
    {
    }
    Kind kind() const override { return m_kind; }
    bool affectsPast() const override { return false; }
    void apply(Line &line, Section *section, float t) override;
    std::shared_ptr<Animation> clone() const override { return std::make_shared<ColourAnimation>(*this); }

    Color startColor, endColor;
    ShadowType shadowType;

private:
    Kind m_kind;
};

class FadeAnimation : public Animation {
public:
    FadeAnimation(Ticks start, int startAlpha, Ticks end, int endAlpha) : Animation(start, end, 1), startAlpha(startAlpha), endAlpha(endAlpha) {}
    Kind kind() const override { return Kind::Fade; }
    bool affectsPast() const override { return true; }
    void apply(Line &line, Section *section, float t) override;
    std::shared_ptr<Animation> clone() const override { return std::make_shared<FadeAnimation>(*this); }
    int startAlpha, endAlpha;
};

class MoveAnimation : public Animation {
public:
    MoveAnimation(Ticks start, PointF from, Ticks end, PointF to) : Animation(start, end, 1), startPosition(from), endPosition(to) {}
    Kind kind() const override { return Kind::Move; }
    bool affectsPast() const override { return true; }
    void apply(Line &line, Section *section, float t) override;
    std::shared_ptr<Animation> clone() const override { return std::make_shared<MoveAnimation>(*this); }
    PointF startPosition, endPosition;
};

class ScaleAnimation : public Animation {
public:
    ScaleAnimation(Ticks start, float from, Ticks end, float to, float accel) : Animation(start, end, accel), startScale(from), endScale(to) {}
    Kind kind() const override { return Kind::Scale; }
    bool affectsPast() const override { return false; }
    void apply(Line &line, Section *section, float t) override;
    std::shared_ptr<Animation> clone() const override { return std::make_shared<ScaleAnimation>(*this); }
    float startScale, endScale;
};

class ShakeAnimation : public Animation {
public:
    ShakeAnimation(Ticks start, Ticks end, PointF radius);
    Kind kind() const override { return Kind::Shake; }
    bool affectsPast() const override { return false; }
    void apply(Line &line, Section *section, float t) override;
    std::shared_ptr<Animation> clone() const override { return std::make_shared<ShakeAnimation>(startTime, endTime, m_radius); }

private:
    PointF m_radius;
    NetRandom m_random;
};

class GlitchingCharAnimation : public Animation {
public:
    GlitchingCharAnimation(Ticks start, Ticks end, std::vector<CharacterRange> ranges);
    Kind kind() const override { return Kind::GlitchingChar; }
    bool affectsPast() const override { return false; }
    bool affectsText() const override { return true; }
    void apply(Line &line, Section *section, float t) override;
    std::shared_ptr<Animation> clone() const override { return std::make_shared<GlitchingCharAnimation>(startTime, endTime, m_ranges); }

private:
    std::vector<CharacterRange> m_ranges;
    NetRandom m_random;
};

// A list whose copies clone every animation, as AssSection and AssLine do.
class Animations {
public:
    Animations() = default;
    Animations(const Animations &other);
    Animations &operator=(const Animations &other);
    Animations(Animations &&) noexcept = default;
    Animations &operator=(Animations &&) noexcept = default;

    std::vector<std::shared_ptr<Animation>> items;
    std::size_t size() const { return items.size(); }
    bool empty() const { return items.empty(); }
    void add(std::shared_ptr<Animation> a) { items.push_back(std::move(a)); }
    void clear() { items.clear(); }
    template<typename Pred>
    void removeIf(Pred pred)
    {
        std::erase_if(items, [&](const std::shared_ptr<Animation> &a) { return pred(*a); });
    }
    void removeKind(Animation::Kind kind)
    {
        removeIf([kind](const Animation &a) { return a.kind() == kind; });
    }
    bool hasKind(Animation::Kind kind) const;
};

struct Section {
    u16 text;
    std::optional<u16> font;
    float scale = 1;
    OffsetType offset = OffsetType::Regular;
    bool bold = false, italic = false, underline = false;
    Color foreColor, backColor;
    NetDict<ShadowType, Color> shadowColors;
    RubyPart rubyPart = RubyPart::None;
    bool packed = false;
    Ticks startOffset = 0;

    Color secondaryColor;
    float blur = 0;
    Color currentWordForeColor, currentWordOutlineColor, currentWordShadowColor;
    Ticks duration = 0;
    Animations animations;

    Section() = default;
    explicit Section(u16 t) : text(std::move(t)) {}
};

class KaraokeType;

struct Line {
    Ticks start = 0, end = 0;
    std::vector<Section> sections;
    AnchorPoint anchorPoint = AnchorPoint::BottomCenter;
    std::optional<PointF> position;
    HorizontalTextDirection horizontalTextDirection = HorizontalTextDirection::LeftToRight;
    VerticalTextType verticalTextType = VerticalTextType::None;
    bool androidDarkTextHackAllowed = true;

    int layer = 0;
    int alpha = 255;
    Animations animations;
    std::shared_ptr<const KaraokeType> karaokeType;

    Line(Ticks s, Ticks e) : start(s), end(e) {}
    u16 text() const;
    void normalizeAlpha();
};

using LinePtr = std::shared_ptr<Line>;

inline LinePtr cloneLine(const Line &line)
{
    return std::make_shared<Line>(line);
}

struct AssStyle {
    u16 name, font;
    float lineHeight = 0;
    bool bold = false, italic = false, underline = false;
    Color primaryColor, secondaryColor, outlineColor;
    float outlineThickness = 0;
    bool outlineIsBox = false;
    Color shadowColor;
    float shadowDistance = 0;
    AnchorPoint anchorPoint = AnchorPoint::BottomCenter;

    bool hasOutline() const { return outlineThickness > 0; }
    bool hasOutlineBox() const { return hasOutline() && outlineIsBox; }
    bool hasShadow() const { return shadowDistance > 0; }
};

struct AssStyleOptions {
    u16 name;
    std::vector<ShadowType> shadowTypes;
    bool isKaraoke = false;
    Color currentWordTextColor, currentWordOutlineColor, currentWordShadowColor;
};

// Equality of formatting, as SectionFormatComparer; `normalized` compares fonts and scales as YouTube would show them.
bool sameFormat(const Section &x, const Section &y, bool normalized);

}
