// Ported from YTSubConverter 1.6.5, Copyright (c) 2018 arcusmaximus, MIT licence (see THIRDPARTY).
#include "hikari/core/srv3.h"
#include "hikari/core/text_projection.h"

#include "srv3_ass.h"

#include <algorithm>
#include <climits>
#include <cmath>

namespace hikari::core::srv3 {

namespace {

const u16 kZwsp = u"​";
const u16 kPaddingSpace = u"​ ​";

int fontStyleId(const std::optional<u16> &font)
{
    if (!font)
        return 0;
    const u16 f = toLowerInvariant(*font);
    for (u16v name : {u"courier new", u"courier", u"nimbus mono l", u"cutive mono"})
        if (f == name)
            return 1;
    for (u16v name : {u"times new roman", u"times", u"georgia", u"cambria", u"pt serif caption"})
        if (f == name)
            return 2;
    for (u16v name : {u"deja vu sans mono", u"dejavu sans mono", u"lucida console", u"monaco", u"consolas", u"pt mono"})
        if (f == name)
            return 3;
    for (u16v name : {u"comic sans ms", u"impact", u"handlee"})
        if (f == name)
            return 5;
    for (u16v name : {u"monotype corsiva", u"urw chancery l", u"apple chancery", u"dancing script"})
        if (f == name)
            return 6;
    if (f == u"carrois gothic sc")
        return 7;
    return 0;
}

u16 fontName(int id)
{
    switch (id) {
    case 1:
        return u"Courier New";
    case 2:
        return u"Times New Roman";
    case 3:
        return u"Lucida Console";
    case 5:
        return u"Comic Sans Ms";
    case 6:
        return u"Monotype Corsiva";
    case 7:
        return u"Carrois Gothic Sc";
    default:
        return u"Roboto";
    }
}

float realFontScale(int yttScale)
{
    return 1 + ((static_cast<float>(yttScale) / 100.0f) - 1) / 4;
}

int youTubeFontScale(float realScale)
{
    const float yttScale = std::max(1 + (realScale - 1) * 4, 0.0f);
    return static_cast<int>(std::nearbyint(static_cast<double>(yttScale * 100)));
}

int youTubeCoord(float pixel, float maxValue)
{
    float percentage = pixel / maxValue * 100;
    percentage = (percentage - 2) / 0.96f;
    percentage = std::isnan(percentage) ? percentage : std::max(percentage, 0.0f);
    percentage = std::isnan(percentage) ? percentage : std::min(percentage, 100.0f);
    const double rounded = std::nearbyint(static_cast<double>(percentage));
    return std::isnan(rounded) ? INT_MIN : static_cast<int>(rounded);
}

int anchorPointId(AnchorPoint p)
{
    return static_cast<int>(p);
}

int justificationId(AnchorPoint p)
{
    if (isLeftAligned(p))
        return 0;
    if (isCenterAligned(p))
        return 2;
    return 1;
}

std::pair<int, int> printAndScrollDirectionIds(HorizontalTextDirection hor, VerticalTextType ver)
{
    switch (ver) {
    case VerticalTextType::Positioned:
        return {2, hor == HorizontalTextDirection::RightToLeft ? 0 : 1};
    case VerticalTextType::Rotated:
        return {3, hor == HorizontalTextDirection::LeftToRight ? 0 : 1};
    default:
        return {hor == HorizontalTextDirection::LeftToRight ? 0 : 1, 0};
    }
}

int edgeTypeId(ShadowType type)
{
    switch (type) {
    case ShadowType::HardShadow:
        return 1;
    case ShadowType::Bevel:
        return 2;
    case ShadowType::Glow:
        return 3;
    case ShadowType::SoftShadow:
        return 4;
    }
    return 0;
}

int offsetTypeId(OffsetType type)
{
    switch (type) {
    case OffsetType::Subscript:
        return 0;
    case OffsetType::Regular:
        return 1;
    case OffsetType::Superscript:
        return 2;
    }
    return 1;
}

int rubyPartId(RubyPart part)
{
    switch (part) {
    case RubyPart::None:
        return 0;
    case RubyPart::Base:
        return 1;
    case RubyPart::Parenthesis:
        return 2;
    case RubyPart::TextBefore:
        return 4;
    case RubyPart::TextAfter:
        return 5;
    }
    return 0;
}

bool isRightToLeft(char16_t c)
{
    return (c >= 0x600 && c < 0x6FF) || (c >= 0x590 && c < 0x5FF);
}

class XmlOut {
public:
    XmlOut() { m_out = u"﻿<?xml version=\"1.0\" encoding=\"utf-8\"?>"; }

    void start(u16v name)
    {
        closeStart();
        m_out += u'<';
        m_out += name;
        m_open = true;
        m_elements.push_back({u16(name)});
    }
    void attribute(u16v name, u16v value)
    {
        m_out += u' ';
        m_out += name;
        m_out += u"=\"";
        for (char16_t c : value) {
            check(c);
            switch (c) {
            case u'&':
                m_out += u"&amp;";
                break;
            case u'<':
                m_out += u"&lt;";
                break;
            case u'>':
                m_out += u"&gt;";
                break;
            case u'"':
                m_out += u"&quot;";
                break;
            case u'\t':
                m_out += u"&#x9;";
                break;
            case u'\n':
                m_out += u"&#xA;";
                break;
            case u'\r':
                m_out += u"&#xD;";
                break;
            default:
                m_out += c;
            }
        }
        m_out += u'"';
    }
    void attribute(u16v name, std::int64_t value) { attribute(name, decimalString(value)); }
    void text(u16v value)
    {
        closeStart();
        for (std::size_t i = 0; i < value.size(); ++i) {
            const char16_t c = value[i];
            check(c, value, i);
            switch (c) {
            case u'&':
                m_out += u"&amp;";
                break;
            case u'<':
                m_out += u"&lt;";
                break;
            case u'>':
                m_out += u"&gt;";
                break;
            case u'\r':
                if (i + 1 < value.size() && value[i + 1] == u'\n')
                    ++i;
                m_out += u'\n';
                break;
            default:
                m_out += c;
            }
        }
    }
    void end()
    {
        if (m_open && !m_elements.back().hasContent) {
            m_out += u" />";
            m_open = false;
        } else {
            closeStart();
            m_out += u"</";
            m_out += m_elements.back().name;
            m_out += u'>';
        }
        m_elements.pop_back();
    }
    const u16 &result() const { return m_out; }

private:
    void closeStart()
    {
        if (m_open) {
            m_out += u'>';
            m_open = false;
        }
        if (!m_elements.empty())
            m_elements.back().hasContent = true;
    }
    static void check(char16_t c, u16v whole = {}, std::size_t i = 0)
    {
        const bool high = c >= 0xD800 && c <= 0xDBFF, low = c >= 0xDC00 && c <= 0xDFFF;
        if (high && i + 1 < whole.size() && whole[i + 1] >= 0xDC00 && whole[i + 1] <= 0xDFFF)
            return;
        if (low && i > 0 && whole[i - 1] >= 0xD800 && whole[i - 1] <= 0xDBFF)
            return;
        if ((c < 0x20 && c != u'\t' && c != u'\n' && c != u'\r') || high || low || c == 0xFFFE || c == 0xFFFF)
            throw ConversionError("'" + std::to_string(static_cast<int>(c)) + "', hexadecimal value, is an invalid character.");
    }

    u16 m_out;
    bool m_open = false;
    struct Open {
        u16 name;
        bool hasContent = false;
    };
    std::vector<Open> m_elements;
};

class YttDocument {
public:
    explicit YttDocument(AssDocument &ass) : m_video(ass.video), m_mergeType(ass.lineMergeType)
    {
        for (const LinePtr &line : ass.lines)
            if (std::any_of(line->sections.begin(), line->sections.end(), [](const Section &s) { return !s.text.empty(); }))
                m_lines.push_back(line);
    }

    u16 save()
    {
        closeGaps();
        mergeSimultaneousLines();
        mergeIdenticallyFormattedSections();
        applyEnhancements();
        mergeIdenticallyFormattedSections();
        extractAttributes();

        XmlOut xml;
        xml.start(u"timedtext");
        xml.attribute(u"format", u"3");
        writeHead(xml);
        writeBody(xml);
        xml.end();
        return xml.result();
    }

private:
    PointF defaultPosition(AnchorPoint anchor) const { return srv3::defaultPosition(m_video, anchor); }

    std::pair<int, int> youTubePosition(const Line &line) const
    {
        const PointF pixel = line.position ? *line.position : defaultPosition(line.anchorPoint);
        return {youTubeCoord(pixel.x, static_cast<float>(m_video.width)), youTubeCoord(pixel.y, static_cast<float>(m_video.height))};
    }

    void closeGaps()
    {
        std::vector<Ticks> starts;
        for (const LinePtr &line : m_lines)
            starts.push_back(line->start);
        std::sort(starts.begin(), starts.end());
        starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
        for (const LinePtr &line : m_lines) {
            std::size_t idx = static_cast<std::size_t>(std::lower_bound(starts.begin(), starts.end(), line->end) - starts.begin());
            const int before = idx > 0 ? totalMilliseconds(line->end - starts[idx - 1]) : INT_MAX;
            const int after = idx < starts.size() ? totalMilliseconds(starts[idx] - line->end) : INT_MAX;
            if (before < 50 && before < after)
                --idx;
            else if (!(after < 50 && after <= before))
                continue;
            line->end = starts[idx];
        }
    }

    static void insertConcatenated(std::vector<LinePtr> &target, std::size_t base, Ticks start, Ticks end, bool afterEqualStart,
                                   std::initializer_list<const Line *> sources)
    {
        auto it = sources.begin();
        LinePtr line = cloneLine(**it);
        for (++it; it != sources.end(); ++it) {
            if ((*it)->sections.empty())
                continue;
            const std::size_t first = line->sections.size();
            line->sections.insert(line->sections.end(), (*it)->sections.begin(), (*it)->sections.end());
            line->sections[first].text = u"\r\n" + line->sections[first].text;
        }
        line->start = start;
        line->end = end;
        std::size_t index = base;
        while (index < target.size() && (afterEqualStart ? target[index]->start <= start : target[index]->start < start))
            ++index;
        target.insert(target.begin() + static_cast<std::ptrdiff_t>(index), std::move(line));
    }

    void mergeSimultaneousLines()
    {
        std::vector<LinePtr> lines;
        for (const LinePtr &l : m_lines)
            if (l->start < l->end)
                lines.push_back(l);
        std::stable_sort(lines.begin(), lines.end(), [](const LinePtr &a, const LinePtr &b) { return a->start < b->start; });

        std::size_t i = 0;
        while (i < lines.size()) {
            if (lines[i]->position) {
                ++i;
                continue;
            }
            const LinePtr first = lines[i];
            LinePtr second;
            std::size_t j = i + 1;
            while (j < lines.size() && lines[j]->start < lines[i]->end) {
                if (!lines[j]->position && lines[j]->anchorPoint == first->anchorPoint) {
                    second = lines[j];
                    break;
                }
                ++j;
            }
            if (!second) {
                ++i;
                continue;
            }
            lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(j));
            lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(i));
            if (first->start < second->start)
                insertConcatenated(lines, i, first->start, second->start, false, {first.get()});
            const Ticks overlapEnd = std::min(first->end, second->end);
            if (isBottomAligned(first->anchorPoint) ^ (m_mergeType == LineMergeType::MoveExisting))
                insertConcatenated(lines, i, second->start, overlapEnd, false, {second.get(), first.get()});
            else
                insertConcatenated(lines, i, second->start, overlapEnd, false, {first.get(), second.get()});
            if (first->end < second->end)
                insertConcatenated(lines, i, first->end, second->end, true, {second.get()});
            else if (second->end < first->end)
                insertConcatenated(lines, i, second->end, first->end, false, {first.get()});
        }
        m_lines = std::move(lines);
    }

    void mergeIdenticallyFormattedSections()
    {
        for (const LinePtr &line : m_lines)
            mergeSections(*line);
    }

    static void mergeSections(Line &line)
    {
        std::size_t i = 0;
        while (i + 1 < line.sections.size()) {
            Section &a = line.sections[i];
            const Section &b = line.sections[i + 1];
            if (sameFormat(a, b, false) && a.startOffset == b.startOffset) {
                a.text += b.text;
                line.sections.erase(line.sections.begin() + static_cast<std::ptrdiff_t>(i + 1));
            } else {
                ++i;
            }
        }
    }

    void applyEnhancements()
    {
        addItalicPrefetch();
        for (std::size_t i = 0; i < m_lines.size(); ++i) {
            makeInvisibleTextBlack(*m_lines[i]);
            preventShadowClipping(*m_lines[i]);
            hardenSpaces(*m_lines[i]);
            limitColors(*m_lines[i]);
            i += expandLineForMultiShadows(i) - 1;
            i += expandLineForDarkText(i) - 1;
        }
        for (const LinePtr &line : m_lines) {
            applyManualLinePadding(*line);
            avoidZeroDurationKaraoke(*line);
        }
    }

    void addItalicPrefetch()
    {
        bool italic = false;
        for (const LinePtr &line : m_lines)
            for (const Section &s : line->sections)
                italic = italic || s.italic;
        if (!italic)
            return;
        auto line = std::make_shared<Line>(roundTimeToFrameCenter(addMilliseconds(kTimeBase, 5000)), roundTimeToFrameCenter(addMilliseconds(kTimeBase, 5100)));
        line->position = PointF{0, 0};
        line->anchorPoint = AnchorPoint::BottomRight;
        Section section(kZwsp);
        section.foreColor = Color::fromArgb(1, 255, 255, 255);
        section.backColor = Color();
        section.italic = true;
        line->sections.push_back(std::move(section));
        m_lines.push_back(std::move(line));
    }

    static void makeInvisibleTextBlack(Line &line)
    {
        for (Section &s : line.sections)
            if (s.foreColor.a() == 0)
                s.foreColor = Color::fromArgb(0, 0, 0, 0);
    }

    static void preventShadowClipping(Line &line)
    {
        for (std::size_t i = 0; i < line.sections.size(); ++i) {
            Section &current = line.sections[i];
            if (current.rubyPart == RubyPart::Base) {
                i += 3;
                continue;
            }
            if (current.shadowColors.empty() || current.underline)
                continue;
            if (std::any_of(current.text.begin(), current.text.end(), isRightToLeft)) {
                if (startsWithLinguistic(current.text, u" ") || i == 0)
                    continue;
                Section &previous = line.sections[i - 1];
                if (previous.rubyPart == RubyPart::Parenthesis || !endsWithLinguistic(previous.text, u" "))
                    continue;
                previous.text.pop_back();
                current.text = u" " + current.text;
            } else {
                if (endsWithLinguistic(current.text, u" ") || i == line.sections.size() - 1)
                    continue;
                Section &next = line.sections[i + 1];
                if (next.rubyPart == RubyPart::Base || !startsWithLinguistic(next.text, u" "))
                    continue;
                current.text += u" ";
                next.text.erase(0, 1);
            }
        }
    }

    static void hardenSpaces(Line &line)
    {
        static const NetRegex kSpaces(u"  +");
        for (Section &s : line.sections)
            s.text = kSpaces.replace(s.text, [](const NetRegex::Match &m) { return u16(m.length, u' '); });
    }

    static void limitColors(Line &line)
    {
        for (Section &s : line.sections) {
            if ((s.foreColor.toArgb() & 0xFFFFFF) == 0xFFFFFF)
                s.foreColor = Color::fromArgb(static_cast<std::uint32_t>(s.foreColor.a()) << 24 | 0xFEFEFE);
            if (s.foreColor.a() == 255)
                s.foreColor = changeAlpha(s.foreColor, 254);
            if (s.backColor.a() == 255)
                s.backColor = changeAlpha(s.backColor, 254);
            s.shadowColors.removeIf([](ShadowType, Color c) { return c.a() == 0; });
            std::vector<ShadowType> change;
            for (const auto &[type, colour] : s.shadowColors.entries())
                if (colour.a() != 254 && ((colour.toArgb() & 0xFFFFFF) != 0x222222 || colour.a() != s.foreColor.a()))
                    change.push_back(type);
            for (ShadowType type : change)
                s.shadowColors.set(type, changeAlpha(s.shadowColors.at(type), 254));
        }
    }

    std::size_t expandLineForMultiShadows(std::size_t index)
    {
        const LinePtr line = m_lines[index];
        if (line->sections.empty())
            throw ConversionError("Sequence contains no elements");
        std::size_t maxShadows = 0;
        for (const Section &s : line->sections)
            maxShadows = std::max(maxShadows, s.shadowColors.size());
        if (maxShadows <= 1)
            return 1;
        static constexpr ShadowType kOrder[] = {ShadowType::SoftShadow, ShadowType::HardShadow, ShadowType::Bevel, ShadowType::Glow};
        std::vector<std::vector<ShadowType>> layers;
        for (const Section &s : line->sections) {
            std::vector<ShadowType> types;
            for (ShadowType t : kOrder)
                if (s.shadowColors.contains(t))
                    types.push_back(t);
            layers.push_back(std::move(types));
        }
        m_lines.erase(m_lines.begin() + static_cast<std::ptrdiff_t>(index));
        for (std::size_t layer = 0; layer < maxShadows; ++layer) {
            LinePtr shadowLine = cloneLine(*line);
            for (std::size_t si = 0; si < shadowLine->sections.size(); ++si) {
                Section &s = shadowLine->sections[si];
                if (layer > 0)
                    s.backColor = Color();
                if (layer < layers[si].size()) {
                    const ShadowType keep = layers[si][layer];
                    s.shadowColors.removeIf([keep](ShadowType t, Color) { return t != keep; });
                } else {
                    s.shadowColors.clear();
                }
            }
            m_lines.insert(m_lines.begin() + static_cast<std::ptrdiff_t>(index + layer), std::move(shadowLine));
        }
        return maxShadows;
    }

    std::size_t expandLineForDarkText(std::size_t index)
    {
        const Line &line = *m_lines[index];
        const bool dark = std::any_of(line.sections.begin(), line.sections.end(), [](const Section &s) { return s.foreColor.a() > 0 && isDark(s.foreColor); });
        if (!line.androidDarkTextHackAllowed || !dark)
            return 1;
        LinePtr bright = cloneLine(line);
        for (Section &s : bright->sections) {
            if (s.foreColor.a() > 0 && isDark(s.foreColor))
                s.foreColor = brighten(s.foreColor);
            s.foreColor = changeAlpha(s.foreColor, 0);
            s.backColor = changeAlpha(s.backColor, 0);
            s.shadowColors.clear();
        }
        m_lines.insert(m_lines.begin() + static_cast<std::ptrdiff_t>(index + 1), std::move(bright));
        return 2;
    }

    static void moveLineBreaksToSeparateSections(Line &line)
    {
        for (std::size_t idx = line.sections.size(); idx-- > 0;) {
            const Section section = line.sections[idx];
            std::vector<u16> runs;
            const u16v text = section.text;
            for (std::size_t i = 0; i < text.size();) {
                const bool breaks = text[i] == u'\r' || text[i] == u'\n';
                std::size_t j = i;
                while (j < text.size() && ((text[j] == u'\r' || text[j] == u'\n') == breaks))
                    ++j;
                runs.emplace_back(text.substr(i, j - i));
                i = j;
            }
            if (runs.size() <= 1)
                continue;
            line.sections.erase(line.sections.begin() + static_cast<std::ptrdiff_t>(idx));
            for (std::size_t i = 0; i < runs.size(); ++i) {
                Section sub = section;
                sub.text = runs[i];
                line.sections.insert(line.sections.begin() + static_cast<std::ptrdiff_t>(idx + i), std::move(sub));
            }
        }
    }

    static Section paddingSection(const Section &templ, u16v text, Ticks startOffset)
    {
        Section s = templ;
        s.text = u16(text);
        s.rubyPart = RubyPart::None;
        s.underline = false;
        s.startOffset = startOffset;
        return s;
    }

    static void applyManualLinePadding(Line &line)
    {
        if (line.verticalTextType == VerticalTextType::Positioned)
            return;
        moveLineBreaksToSeparateSections(line);
        Section hidden;
        hidden.backColor = Color::fromArgb(0, 130, 140, 150);
        hidden.foreColor = Color::fromArgb(0, 160, 170, 180);
        auto &sections = line.sections;
        auto at = [&](std::size_t i) -> Section & {
            if (i >= sections.size())
                throw ConversionError("Index was out of range. Must be non-negative and less than the size of the collection.");
            return sections[i];
        };
        std::size_t i = 0;
        while (i < sections.size()) {
            const Section section = sections[i];
            std::size_t next = i + (section.rubyPart == RubyPart::Base ? 4 : 1);
            hidden.scale = section.scale;
            if (section.text.find(u"\r\n") != u16::npos) {
                sections[i] = paddingSection(hidden, section.text, section.startOffset);
                i = next;
                continue;
            }
            if (i == 0 || endsWithLinguistic(sections[i - 1].text, u"\r\n")) {
                sections.insert(sections.begin() + static_cast<std::ptrdiff_t>(i), paddingSection(hidden, kZwsp, section.startOffset));
                sections.insert(sections.begin() + static_cast<std::ptrdiff_t>(i + 1), paddingSection(section, kPaddingSpace, section.startOffset));
                next += 2;
            }
            if (next == sections.size() || startsWithLinguistic(at(next).text, u"\r\n")) {
                if (next > sections.size())
                    throw ConversionError("Index must be within the bounds of the List.");
                sections.insert(sections.begin() + static_cast<std::ptrdiff_t>(next), paddingSection(section, kPaddingSpace, section.startOffset));
                sections.insert(sections.begin() + static_cast<std::ptrdiff_t>(next + 1), paddingSection(hidden, kZwsp, section.startOffset));
                next += 2;
            }
            i = next;
        }
    }

    static void avoidZeroDurationKaraoke(Line &line)
    {
        for (std::size_t i = 1; i < line.sections.size(); ++i) {
            const Section &previous = line.sections[i - 1];
            Section &section = line.sections[i];
            if (section.startOffset > 0 && previous.startOffset >= section.startOffset)
                section.startOffset = previous.startOffset + kTicksPerMs;
        }
    }

    void extractAttributes()
    {
        for (const LinePtr &line : m_lines) {
            if (positionId(*line) == 0)
                m_positions.push_back(line.get());
            if (windowStyleId(*line) == 0)
                m_windowStyles.push_back(line.get());
            for (const Section &s : line->sections)
                if (penId(s) == 0)
                    m_pens.push_back(&s);
        }
    }

    int positionId(const Line &line) const
    {
        const auto pos = youTubePosition(line);
        for (std::size_t i = 0; i < m_positions.size(); ++i)
            if (m_positions[i]->anchorPoint == line.anchorPoint && youTubePosition(*m_positions[i]) == pos)
                return static_cast<int>(i + 1);
        return 0;
    }

    int windowStyleId(const Line &line) const
    {
        for (std::size_t i = 0; i < m_windowStyles.size(); ++i) {
            const Line &w = *m_windowStyles[i];
            if (justificationId(w.anchorPoint) == justificationId(line.anchorPoint) && w.horizontalTextDirection == line.horizontalTextDirection &&
                w.verticalTextType == line.verticalTextType)
                return static_cast<int>(i + 1);
        }
        return 0;
    }

    int penId(const Section &section) const
    {
        for (std::size_t i = 0; i < m_pens.size(); ++i)
            if (sameFormat(*m_pens[i], section, true))
                return static_cast<int>(i + 1);
        return 0;
    }

    void writeWindowPosition(XmlOut &xml, int id, const Line &position) const
    {
        const auto [ah, av] = youTubePosition(position);
        xml.start(u"wp");
        xml.attribute(u"id", id);
        xml.attribute(u"ap", anchorPointId(position.anchorPoint));
        xml.attribute(u"ah", ah);
        xml.attribute(u"av", av);
        xml.end();
    }

    static void writeWindowStyle(XmlOut &xml, int id, const Line &style)
    {
        xml.start(u"ws");
        xml.attribute(u"id", id);
        xml.attribute(u"ju", justificationId(style.anchorPoint));
        const auto [pd, sd] = printAndScrollDirectionIds(style.horizontalTextDirection, style.verticalTextType);
        xml.attribute(u"pd", pd);
        xml.attribute(u"sd", sd);
        xml.attribute(u"wfo", u"0");
        xml.end();
    }

    static void writePen(XmlOut &xml, int id, const Section &format)
    {
        xml.start(u"pen");
        xml.attribute(u"id", id);
        if (const int fs = fontStyleId(format.font); fs != 0)
            xml.attribute(u"fs", fs);
        xml.attribute(u"sz", youTubeFontScale(format.scale));
        if (format.offset != OffsetType::Regular)
            xml.attribute(u"of", offsetTypeId(format.offset));
        if (format.bold)
            xml.attribute(u"b", u"1");
        if (format.italic)
            xml.attribute(u"i", u"1");
        if (format.underline)
            xml.attribute(u"u", u"1");
        xml.attribute(u"fc", toHtml(format.foreColor.isEmpty() ? Color::fromArgb(0xFF000000u) : format.foreColor));
        xml.attribute(u"fo", format.foreColor.a());
        if (format.backColor.a() > 0)
            xml.attribute(u"bc", toHtml(format.backColor));
        xml.attribute(u"bo", format.backColor.a());
        if (!format.shadowColors.empty()) {
            if (format.shadowColors.size() > 1)
                throw ConversionError("YTT lines must be reduced to one shadow color before saving");
            const auto [type, colour] = format.shadowColors.entries().front();
            if (colour.a() > 0) {
                xml.attribute(u"et", edgeTypeId(type));
                if ((colour.toArgb() & 0xFFFFFF) != 0x222222 || colour.a() != format.foreColor.a())
                    xml.attribute(u"ec", toHtml(colour));
            }
        }
        if (format.rubyPart != RubyPart::None)
            xml.attribute(u"rb", rubyPartId(format.rubyPart));
        if (format.packed)
            xml.attribute(u"hg", u"1");
        xml.end();
    }

    void writeHead(XmlOut &xml) const
    {
        xml.start(u"head");
        Line dummy(kTimeBase, kTimeBase);
        dummy.position = PointF{};
        writeWindowPosition(xml, 0, dummy);
        for (std::size_t i = 0; i < m_positions.size(); ++i)
            writeWindowPosition(xml, static_cast<int>(i + 1), *m_positions[i]);
        writeWindowStyle(xml, 0, Line(kTimeBase, kTimeBase));
        for (std::size_t i = 0; i < m_windowStyles.size(); ++i)
            writeWindowStyle(xml, static_cast<int>(i + 1), *m_windowStyles[i]);
        writePen(xml, 0, Section());
        for (std::size_t i = 0; i < m_pens.size(); ++i)
            writePen(xml, static_cast<int>(i + 1), *m_pens[i]);
        xml.end();
    }

    void writeBody(XmlOut &xml) const
    {
        xml.start(u"body");
        for (const LinePtr &line : m_lines)
            writeLine(xml, *line);
        xml.end();
    }

    void writeLine(XmlOut &xml, const Line &line) const
    {
        if (line.sections.empty())
            return;
        int startMs = totalMilliseconds(line.start - kTimeBase);
        int durationMs = totalMilliseconds(line.end - line.start);
        if (startMs <= 0) {
            durationMs -= -startMs + 1;
            startMs = 1;
        }
        if (durationMs <= 0)
            return;
        xml.start(u"p");
        xml.attribute(u"t", startMs);
        xml.attribute(u"d", durationMs);
        if (line.sections.size() == 1)
            xml.attribute(u"p", penId(line.sections[0]));
        xml.attribute(u"wp", positionId(line));
        xml.attribute(u"ws", windowStyleId(line));
        if (line.sections.size() == 1) {
            xml.text(line.sections[0].text);
        } else {
            const std::size_t workaround = line.sections[0].rubyPart == RubyPart::None ? 0 : 3;
            for (std::size_t i = 0; i < line.sections.size(); ++i) {
                const Section &s = line.sections[i];
                xml.start(u"s");
                xml.attribute(u"p", penId(s));
                if (s.startOffset > 0)
                    xml.attribute(u"t", totalMilliseconds(s.startOffset));
                xml.text(s.text);
                xml.end();
                if (i == workaround)
                    xml.text(kZwsp);
            }
        }
        xml.end();
    }

    VideoSize m_video;
    LineMergeType m_mergeType;
    std::vector<LinePtr> m_lines;
    std::vector<const Line *> m_positions, m_windowStyles;
    std::vector<const Section *> m_pens;
};

}

bool sameFormat(const Section &x, const Section &y, bool normalized)
{
    auto font = [normalized](const Section &s) -> std::optional<u16> { return normalized ? std::optional<u16>(fontName(fontStyleId(s.font))) : s.font; };
    auto scale = [normalized](const Section &s) { return normalized ? realFontScale(youTubeFontScale(s.scale)) : s.scale; };
    if (x.bold != y.bold || x.italic != y.italic || x.underline != y.underline || font(x) != font(y) ||
        std::abs(static_cast<double>(scale(x) - scale(y))) > 0.001 || x.offset != y.offset || x.foreColor.toArgb() != y.foreColor.toArgb() ||
        x.backColor.toArgb() != y.backColor.toArgb() || x.rubyPart != y.rubyPart || x.packed != y.packed ||
        x.shadowColors.size() != y.shadowColors.size())
        return false;
    for (const auto &[type, colour] : x.shadowColors.entries()) {
        const Color *other = y.shadowColors.find(type);
        if (!other || other->toArgb() != colour.toArgb())
            return false;
    }
    return true;
}

}

namespace hikari::core {

std::expected<std::vector<std::byte>, std::string> srv3FromAss(std::u16string_view script, const std::vector<Srv3StyleOptions> &options)
{
    try {
        std::vector<srv3::AssStyleOptions> all = srv3::builtInStyleOptions();
        for (const Srv3StyleOptions &o : options) {
            srv3::AssStyleOptions converted;
            converted.name = o.style;
            static constexpr std::pair<std::uint8_t, srv3::ShadowType> kOrder[] = {
                {Srv3Glow, srv3::ShadowType::Glow},
                {Srv3Bevel, srv3::ShadowType::Bevel},
                {Srv3SoftShadow, srv3::ShadowType::SoftShadow},
                {Srv3HardShadow, srv3::ShadowType::HardShadow},
            };
            for (const auto &[bit, type] : kOrder)
                if (o.shadows & bit)
                    converted.shadowTypes.push_back(type);
            converted.isKaraoke = o.karaoke;
            auto colour = [](const std::optional<std::uint32_t> &rgb) {
                return rgb ? srv3::Color::fromArgb(0xFF000000u | (*rgb & 0xFFFFFF)) : srv3::Color();
            };
            converted.currentWordTextColor = colour(o.currentWordText);
            converted.currentWordOutlineColor = colour(o.currentWordOutline);
            converted.currentWordShadowColor = colour(o.currentWordShadow);
            all.push_back(std::move(converted));
        }
        if (script.starts_with(u'\uFEFF'))
            script.remove_prefix(1);
        srv3::AssDocument ass = srv3::readAss(script, std::move(all));
        const std::u8string bytes = toUtf8(srv3::YttDocument(ass).save());
        std::vector<std::byte> out(bytes.size());
        std::transform(bytes.begin(), bytes.end(), out.begin(), [](char8_t c) { return static_cast<std::byte>(c); });
        return out;
    } catch (const srv3::ConversionError &e) {
        return std::unexpected(std::string(e.what()));
    }
}

}
