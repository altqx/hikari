// Ported from YTSubConverter 1.6.5, Copyright (c) 2018 arcusmaximus, MIT licence (see THIRDPARTY).
#include "srv3_karaoke.h"

#include <algorithm>
#include <cstdint>
#include <map>

namespace hikari::core::srv3 {

namespace {

using Kind = Animation::Kind;

const u16 kZwsp = u"​";

Section &sectionAt(Line &line, std::size_t i)
{
    if (i >= line.sections.size())
        throw ConversionError("Index was out of range. Must be non-negative and less than the size of the collection.");
    return line.sections[i];
}

class SimpleKaraoke : public KaraokeType {
public:
    bool isPlainSimple() const override { return true; }
    std::vector<LinePtr> apply(KaraokeStepContext &context) const override
    {
        sing(context);
        return {context.stepLine};
    }

protected:
    static void sing(KaraokeStepContext &context)
    {
        for (std::size_t i = context.singingBegin; i < context.singingEnd; ++i) {
            Section &s = context.stepLine->sections[i];
            if (!s.currentWordForeColor.isEmpty()) {
                s.foreColor = s.currentWordForeColor;
                s.animations.removeKind(Kind::ForeColor);
            }
            if (!s.currentWordShadowColor.isEmpty()) {
                for (ShadowType type : s.shadowColors.keys()) {
                    s.shadowColors.set(type, s.currentWordShadowColor);
                    s.animations.removeIf([type](const Animation &a) {
                        return a.kind() == Kind::ShadowColor && static_cast<const ColourAnimation &>(a).shadowType == type;
                    });
                }
            }
            if (!s.currentWordOutlineColor.isEmpty() && s.shadowColors.contains(ShadowType::Glow)) {
                s.shadowColors.set(ShadowType::Glow, s.currentWordOutlineColor);
                s.animations.removeIf([](const Animation &a) {
                    return a.kind() == Kind::ShadowColor && static_cast<const ColourAnimation &>(a).shadowType == ShadowType::Glow;
                });
            }
        }
    }
};

std::shared_ptr<ColourAnimation> colourAnimation(Kind kind, Ticks start, Color from, Ticks end, Color to, ShadowType shadow = ShadowType::Glow)
{
    return std::make_shared<ColourAnimation>(kind, start, from, end, to, 1.0f, shadow);
}

class FadeKaraoke : public KaraokeType {
public:
    std::vector<LinePtr> apply(KaraokeStepContext &context) const override
    {
        fadeIn(context);
        fadeOut(context);
        return {context.stepLine};
    }

private:
    static void fadeIn(KaraokeStepContext &context)
    {
        Line &step = *context.stepLine;
        const Ticks fadeEnd = std::min(addMilliseconds(step.start, 500), step.end);
        for (std::size_t i = context.singingBegin; i < context.singingEnd; ++i) {
            Section &s = step.sections[i];
            if (s.currentWordForeColor.isEmpty()) {
                if (s.foreColor != s.secondaryColor)
                    s.animations.add(colourAnimation(Kind::ForeColor, step.start, s.secondaryColor, fadeEnd, s.foreColor));
            } else if (s.currentWordForeColor != s.secondaryColor) {
                s.animations.add(colourAnimation(Kind::ForeColor, step.start, s.secondaryColor, fadeEnd, s.currentWordForeColor));
            }
            if (!s.currentWordShadowColor.isEmpty())
                for (const auto &[type, colour] : s.shadowColors.entries())
                    if (s.currentWordShadowColor != colour)
                        s.animations.add(colourAnimation(Kind::ShadowColor, step.start, colour, fadeEnd, s.currentWordShadowColor, type));
            if (!s.currentWordOutlineColor.isEmpty() && s.currentWordOutlineColor != s.shadowColors.getOrDefault(ShadowType::Glow))
                s.animations.add(colourAnimation(Kind::ShadowColor, step.start, s.shadowColors.at(ShadowType::Glow), fadeEnd,
                                                 s.currentWordOutlineColor, ShadowType::Glow));
        }
    }

    static void fadeOut(KaraokeStepContext &context)
    {
        Line &step = *context.stepLine;
        const auto &steps = context.activeSectionsPerStep;
        std::size_t first = 0;
        for (std::size_t prev = 0; prev < context.stepIndex; ++prev) {
            const Ticks fadeStart = context.originalLine->start + steps[prev + 1].first;
            const Ticks fadeEnd = addMilliseconds(fadeStart, 1000);
            const int last = steps[prev].second - 1;
            for (int idx = static_cast<int>(first); idx <= last; ++idx) {
                Section &s = sectionAt(step, static_cast<std::size_t>(idx));
                if (!s.currentWordForeColor.isEmpty() && s.currentWordForeColor != s.foreColor)
                    s.animations.add(colourAnimation(Kind::ForeColor, fadeStart, s.currentWordForeColor, fadeEnd, s.foreColor));
                if (!s.currentWordShadowColor.isEmpty())
                    for (const auto &[type, colour] : s.shadowColors.entries())
                        if (s.currentWordShadowColor != colour)
                            s.animations.add(colourAnimation(Kind::ShadowColor, fadeStart, s.currentWordShadowColor, fadeEnd, colour, type));
                if (!s.currentWordOutlineColor.isEmpty() && s.currentWordOutlineColor != s.shadowColors.getOrDefault(ShadowType::Glow))
                    s.animations.add(colourAnimation(Kind::ShadowColor, fadeStart, s.currentWordOutlineColor, fadeEnd,
                                                     s.shadowColors.at(ShadowType::Glow), ShadowType::Glow));
            }
            first = static_cast<std::size_t>(last + 1);
        }
    }
};

class GlitchKaraoke : public SimpleKaraoke {
public:
    bool isPlainSimple() const override { return false; }
    std::vector<LinePtr> apply(KaraokeStepContext &context) const override
    {
        Line &step = *context.stepLine;
        std::optional<std::size_t> singing;
        for (std::size_t i = context.singingBegin; i < context.singingEnd; ++i)
            if (step.sections[i].rubyPart == RubyPart::None || step.sections[i].rubyPart == RubyPart::Base)
                singing = i;
        if (!singing || step.sections[*singing].text.empty())
            return {context.stepLine};
        sing(context);
        const Ticks glitchEnd = frameToEndTime(startTimeToFrame(step.start) + 1);
        Section &s = step.sections[*singing];
        s.animations.add(std::make_shared<GlitchingCharAnimation>(step.start, glitchEnd, rangesFor(s.text[0])));
        return {context.stepLine};
    }

private:
    static std::vector<CharacterRange> rangesFor(char16_t c)
    {
        static const std::vector<std::vector<CharacterRange>> kLanguages = {
            {{u'A', u'Z'}, {u'a', u'z'}},
            {{0x4E00, 0x9FCC}, {0x3400, 0x4DB6}, {0xF900, 0xFA6B}},
            {{0x3041, 0x3097}},
            {{0x30A0, 0x3100}},
            {{0xAC00, 0xD7A4}},
        };
        for (const auto &ranges : kLanguages)
            for (const CharacterRange &r : ranges)
                if (r.contains(c))
                    return ranges;
        return {{0x2300, 0x231A}, {0x231C, 0x23E1}};
    }
};

class CursorKaraoke : public SimpleKaraoke {
public:
    CursorKaraoke(std::vector<u16> cursors, Ticks interval, bool before)
        : m_cursors(std::move(cursors)), m_intervalMs(totalMilliseconds(interval)), m_beforeSinging(before)
    {
    }
    bool isPlainSimple() const override { return false; }

    std::vector<LinePtr> apply(KaraokeStepContext &context) const override
    {
        sing(context);
        if (m_intervalMs == 0)
            throw ConversionError("Attempted to divide by zero.");
        const Line &step = *context.stepLine;
        const Ticks originalStart = context.originalLine->start;
        const int startIdx = totalMilliseconds(step.start - originalStart) / m_intervalMs;
        const int endIdx = totalMilliseconds(step.end - originalStart) / m_intervalMs;
        std::vector<LinePtr> out;
        for (int c = startIdx; c <= endIdx; ++c) {
            LinePtr line = cloneLine(step);
            if (c > startIdx)
                line->start = roundTimeToFrameCenter(addMilliseconds(originalStart, wrappingProduct(c, m_intervalMs)));
            if (c < endIdx)
                line->end = roundTimeToFrameCenter(addMilliseconds(originalStart, wrappingProduct(c + 1, m_intervalMs)));
            if (line->start == line->end)
                continue;
            const int singingCount = static_cast<int>(context.singingEnd - context.singingBegin);
            const int cursorIdx = m_beforeSinging ? context.numActiveSections - singingCount : context.numActiveSections;
            const Section initial = sectionAt(*line, static_cast<std::size_t>(std::max(cursorIdx - 1, 0)));
            std::vector<Section> cursor =
                cursorSections(*context.document, initial, m_cursors[static_cast<std::size_t>(c) % m_cursors.size()]);
            if (cursorIdx < 0 || static_cast<std::size_t>(cursorIdx) > line->sections.size())
                throw ConversionError("Index must be within the bounds of the List.");
            line->sections.insert(line->sections.begin() + cursorIdx, cursor.begin(), cursor.end());
            out.push_back(std::move(line));
        }
        return out;
    }

private:
    static int wrappingProduct(int a, int b)
    {
        return static_cast<int>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
    }

    std::vector<u16> m_cursors;
    int m_intervalMs;
    bool m_beforeSinging;
};

std::vector<std::pair<Ticks, int>> karaokeSteps(const Line &line)
{
    std::map<Ticks, int> steps;
    Ticks offset = 0;
    int active = 0;
    for (const Section &s : line.sections) {
        ++active;
        if (s.duration > 0) {
            steps[offset] = active;
            offset += s.duration;
        } else {
            const Ticks previous = steps.empty() ? 0 : steps.rbegin()->first;
            steps[previous] = active;
        }
    }
    return {steps.begin(), steps.end()};
}

std::vector<LinePtr> karaokeStepLines(AssDocument &doc, const LinePtr &original, const std::vector<std::pair<Ticks, int>> &steps, std::size_t idx)
{
    const Ticks offset = steps[idx].first;
    const int active = steps[idx].second;
    const Ticks start = roundTimeToFrameCenter(original->start + offset);
    if (start >= original->end)
        return {};
    Ticks end = original->end;
    if (idx + 1 < steps.size()) {
        end = roundTimeToFrameCenter(original->start + steps[idx + 1].first);
        if (end > original->end)
            end = original->end;
    }

    LinePtr step = cloneLine(*original);
    step->start = start;
    step->end = end;
    for (std::size_t i = 0; i < step->sections.size(); ++i) {
        Section &s = step->sections[i];
        s.duration = 0;
        s.startOffset = 0;
        if (static_cast<int>(i) < active) {
            s.animations.removeKind(Kind::SecondaryColor);
            continue;
        }
        s.foreColor = s.secondaryColor;
        s.animations.removeKind(Kind::ForeColor);
        std::vector<std::shared_ptr<Animation>> secondary;
        for (const auto &a : s.animations.items)
            if (a->kind() == Kind::SecondaryColor)
                secondary.push_back(a);
        for (const auto &a : secondary) {
            std::erase(s.animations.items, a);
            const auto &c = static_cast<const ColourAnimation &>(*a);
            s.animations.add(std::make_shared<ColourAnimation>(Kind::ForeColor, c.startTime, c.startColor, c.endTime, c.endColor, c.acceleration));
        }
        if (s.foreColor.a() == 0 && !s.animations.hasKind(Kind::ForeColor)) {
            s.foreColor = Color::fromArgb(0u);
            s.backColor = Color::fromArgb(0u);
            s.shadowColors.clear();
        }
    }

    KaraokeStepContext context;
    context.document = &doc;
    context.originalLine = original;
    context.activeSectionsPerStep = steps;
    context.stepLine = step;
    context.stepIndex = idx;
    context.numActiveSections = active;
    const std::size_t count = step->sections.size();
    const int previousActive = idx > 0 ? steps[idx - 1].second : 0;
    context.singingBegin = std::min(static_cast<std::size_t>(previousActive), count);
    context.singingEnd = std::min(context.singingBegin + static_cast<std::size_t>(std::max(active - previousActive, 0)), count);
    return step->karaokeType->apply(context);
}

struct TimeRange {
    Ticks start, end;
    bool contains(Ticks t) const { return t >= start && t < end; }
    bool overlaps(const TimeRange &o) const { return start < o.end && end > o.start; }
};

struct AnimAt {
    std::shared_ptr<Animation> animation;
    int section;
};

void applyAnimation(Line &line, const AnimAt &anim, float t)
{
    Section *section = nullptr;
    if (anim.section >= 0)
        section = &sectionAt(line, static_cast<std::size_t>(anim.section));
    anim.animation->apply(line, section, t);
}

void resetText(Line &frame, const Line &original)
{
    for (std::size_t i = 0; i < frame.sections.size(); ++i) {
        if (i >= original.sections.size())
            throw ConversionError("Index was out of range. Must be non-negative and less than the size of the collection.");
        frame.sections[i].text = original.sections[i].text;
    }
}

std::vector<AnimAt> animationsOf(const Line &line)
{
    std::vector<AnimAt> out;
    for (const auto &a : line.animations.items)
        if (a->kind() == Kind::Move)
            out.push_back({a, -1});
    for (const auto &a : line.animations.items)
        if (a->kind() != Kind::Move)
            out.push_back({a, -1});
    for (std::size_t i = 0; i < line.sections.size(); ++i)
        for (const auto &a : line.sections[i].animations.items)
            out.push_back({a, static_cast<int>(i)});
    return out;
}

std::vector<std::pair<TimeRange, std::vector<AnimAt>>> clusters(const Line &line, const std::vector<AnimAt> &anims)
{
    std::vector<TimeRange> ranges;
    for (const AnimAt &a : anims) {
        TimeRange range{a.animation->startTime, a.animation->endTime};
        for (std::size_t i = ranges.size(); i-- > 0;)
            if (ranges[i].overlaps(range)) {
                range.start = std::min(range.start, ranges[i].start);
                range.end = std::max(range.end, ranges[i].end);
                ranges.erase(ranges.begin() + static_cast<std::ptrdiff_t>(i));
            }
        ranges.push_back(range);
    }

    const TimeRange lineRange{line.start, line.end};
    std::vector<std::pair<TimeRange, std::vector<AnimAt>>> out;
    for (TimeRange range : ranges) {
        if (!range.overlaps(lineRange))
            continue;
        std::vector<AnimAt> members;
        for (const AnimAt &a : anims)
            if (range.contains(a.animation->startTime) && a.animation->endTime > a.animation->startTime)
                members.push_back(a);
        range.start = std::max(range.start, lineRange.start);
        range.end = std::min(range.end, lineRange.end);
        range.start = roundTimeToFrameCenter(range.start);
        range.end = roundTimeToFrameCenter(range.end);
        auto at = std::lower_bound(out.begin(), out.end(), range.start, [](const auto &entry, Ticks s) { return entry.first.start < s; });
        if (at == out.end() || at->first.start != range.start)
            at = out.insert(at, {range, {}});
        at->second.insert(at->second.end(), members.begin(), members.end());
    }
    return out;
}

LinePtr initialLine(const Line &original, const std::vector<AnimAt> &anims)
{
    LinePtr line = cloneLine(original);
    line->start = roundTimeToFrameCenter(line->start);
    std::vector<AnimAt> ended;
    for (const AnimAt &a : anims)
        if (a.animation->endTime < original.start)
            ended.push_back(a);
    std::stable_sort(ended.begin(), ended.end(), [](const AnimAt &x, const AnimAt &y) { return x.animation->endTime < y.animation->endTime; });
    for (const AnimAt &a : ended)
        applyAnimation(*line, a, 1);
    std::vector<AnimAt> future;
    for (const AnimAt &a : anims)
        if (a.animation->affectsPast() && a.animation->startTime >= original.start)
            future.push_back(a);
    std::stable_sort(future.begin(), future.end(), [](const AnimAt &x, const AnimAt &y) { return x.animation->startTime > y.animation->startTime; });
    for (const AnimAt &a : future)
        applyAnimation(*line, a, 0);
    return line;
}

LinePtr postClusterLine(const Line &original, const Line &last, Ticks start, Ticks end, std::vector<AnimAt> anims)
{
    LinePtr line = cloneLine(last);
    line->start = start;
    line->end = end;
    std::stable_sort(anims.begin(), anims.end(), [](const AnimAt &x, const AnimAt &y) { return x.animation->endTime < y.animation->endTime; });
    for (const AnimAt &a : anims) {
        if (a.animation->affectsText())
            resetText(*line, original);
        applyAnimation(*line, a, 1);
    }
    return line;
}

}

std::shared_ptr<const KaraokeType> simpleKaraokeType()
{
    static const auto instance = std::make_shared<const SimpleKaraoke>();
    return instance;
}

std::shared_ptr<const KaraokeType> fadeKaraokeType()
{
    static const auto instance = std::make_shared<const FadeKaraoke>();
    return instance;
}

std::shared_ptr<const KaraokeType> glitchKaraokeType()
{
    static const auto instance = std::make_shared<const GlitchKaraoke>();
    return instance;
}

std::shared_ptr<const KaraokeType> cursorKaraokeType(std::vector<u16> cursors, Ticks interval, bool beforeSinging)
{
    return std::make_shared<const CursorKaraoke>(std::move(cursors), interval, beforeSinging);
}

std::vector<LinePtr> emulatedKaraokeLines(AssDocument &doc, const LinePtr &line)
{
    const auto steps = karaokeSteps(*line);
    std::vector<LinePtr> out;
    for (std::size_t i = 0; i < steps.size(); ++i)
        for (LinePtr &step : karaokeStepLines(doc, line, steps, i)) {
            std::erase_if(step->sections, [](const Section &s) { return s.text == kZwsp; });
            out.push_back(std::move(step));
        }
    return out;
}

std::vector<LinePtr> animatorExpand(const AssDocument &doc, const LinePtr &original)
{
    const std::vector<AnimAt> anims = animationsOf(*original);
    if (anims.empty())
        return {original};

    const auto animClusters = clusters(*original, anims);
    LinePtr last = initialLine(*original, anims);
    std::vector<LinePtr> out;
    if (animClusters.empty() || animClusters.front().first.start > last->start)
        out.push_back(last);

    constexpr int kStep = 2;
    for (std::size_t i = 0; i < animClusters.size(); ++i) {
        const auto &[range, members] = animClusters[i];
        const LinePtr base = last;
        const int startFrame = startTimeToFrame(range.start);
        const int endFrame = endTimeToFrame(range.end);
        const int subStep = (endFrame + 1 - startFrame) % kStep;
        const int lastIteration = endFrame + 1 - subStep - kStep;
        const bool needTextReset = std::any_of(members.begin(), members.end(), [](const AnimAt &a) { return a.animation->affectsText(); });
        LinePtr frameLine = base;
        PointF basePosition = base->position ? *base->position : doc.defaultPosition(base->anchorPoint);
        for (int frame = startFrame; frame <= lastIteration; frame += kStep) {
            frameLine = cloneLine(*frameLine);
            frameLine->start = frameToStartTime(frame);
            frameLine->end = frame < lastIteration ? frameToEndTime(frame + kStep - 1) : range.end;
            frameLine->position = basePosition;
            if (needTextReset)
                resetText(*frameLine, *base);
            const float interpFrame = static_cast<float>(frame) + (kStep - 1) / 2.0f;
            for (const AnimAt &a : members) {
                const int animStart = startTimeToFrame(a.animation->startTime);
                const int animEnd = endTimeToFrame(a.animation->endTime);
                if (interpFrame >= static_cast<float>(animStart) && interpFrame < static_cast<float>(animEnd)) {
                    const float t = (interpFrame - static_cast<float>(animStart)) / static_cast<float>(animEnd - animStart);
                    applyAnimation(*frameLine, a, t);
                } else if (interpFrame >= static_cast<float>(animEnd) && interpFrame < static_cast<float>(animEnd + kStep)) {
                    applyAnimation(*frameLine, a, 1);
                    if (a.animation->kind() == Kind::Move)
                        basePosition = *frameLine->position;
                }
            }
            last->end = frameLine->start;
            out.push_back(frameLine);
            last = frameLine;
        }

        const Ticks interStart = range.end;
        const Ticks interEnd = i + 1 < animClusters.size() ? animClusters[i + 1].first.start : original->end;
        if (interEnd > interStart) {
            last = postClusterLine(*original, *last, interStart, interEnd, members);
            out.push_back(last);
        }
    }
    last->end = original->end;
    return out;
}

}
