// Ported from YTSubConverter 1.6.5, Copyright (c) 2018 arcusmaximus, MIT licence (see THIRDPARTY).
#include "srv3_ass.h"
#include "srv3_karaoke.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <set>

namespace hikari::core::srv3 {

const AssStyle *AssDocument::style(u16v name) const
{
    for (const AssStyle &s : styles)
        if (s.name == name)
            return &s;
    return nullptr;
}

const AssStyleOptions *AssDocument::styleOptions(u16v name) const
{
    for (const AssStyleOptions &o : options)
        if (o.name == name)
            return &o;
    return nullptr;
}

PointF defaultPosition(VideoSize video, AnchorPoint anchor)
{
    const float w = static_cast<float>(video.width), h = static_cast<float>(video.height);
    const float left = w * 0.02f, center = w / 2.0f, right = w * 0.98f;
    const float top = h * 0.02f, middle = h / 2.0f, bottom = h * 0.98f;
    switch (anchor) {
    case AnchorPoint::TopLeft:
        return {left, top};
    case AnchorPoint::TopCenter:
        return {center, top};
    case AnchorPoint::TopRight:
        return {right, top};
    case AnchorPoint::MiddleLeft:
        return {left, middle};
    case AnchorPoint::Center:
        return {center, middle};
    case AnchorPoint::MiddleRight:
        return {right, middle};
    case AnchorPoint::BottomLeft:
        return {left, bottom};
    case AnchorPoint::BottomCenter:
        return {center, bottom};
    case AnchorPoint::BottomRight:
        return {right, bottom};
    }
    throw ConversionError("Specified argument was out of the range of valid values.");
}

PointF AssDocument::defaultPosition(AnchorPoint anchor) const
{
    return srv3::defaultPosition(video, anchor);
}

std::vector<AssStyleOptions> builtInStyleOptions()
{
    struct Entry {
        const char16_t *name;
        std::optional<ShadowType> shadow;
    };
    static const Entry kEntries[] = {
        {u"YTPlain", std::nullopt},
        {u"YTPlainBox", std::nullopt},
        {u"YTGlow", ShadowType::Glow},
        {u"YTGlowBox", ShadowType::Glow},
        {u"YTSoftShadow", ShadowType::SoftShadow},
        {u"YTSoftShadowBox", ShadowType::SoftShadow},
        {u"YTHardShadow", ShadowType::HardShadow},
        {u"YTHardShadowBox", ShadowType::HardShadow},
        {u"YTBevel", ShadowType::Bevel},
        {u"YTBevelBox", ShadowType::Bevel},
        {u"__ytplain", std::nullopt},
        {u"__ytglow", ShadowType::Glow},
        {u"__ytsoftshadow", ShadowType::SoftShadow},
        {u"__ythardshadow", ShadowType::HardShadow},
        {u"__ytbevel", ShadowType::Bevel},
    };
    std::vector<AssStyleOptions> out;
    for (const Entry &e : kEntries) {
        AssStyleOptions o;
        o.name = e.name;
        if (e.shadow)
            o.shadowTypes.push_back(*e.shadow);
        out.push_back(std::move(o));
    }
    return out;
}

namespace {

const u16 kZwsp = u"\u200B";

AnchorPoint anchorForAlignment(int alignment)
{
    switch (alignment) {
    case 1:
        return AnchorPoint::BottomLeft;
    case 2:
        return AnchorPoint::BottomCenter;
    case 3:
        return AnchorPoint::BottomRight;
    case 4:
        return AnchorPoint::MiddleLeft;
    case 5:
        return AnchorPoint::Center;
    case 6:
        return AnchorPoint::MiddleRight;
    case 7:
        return AnchorPoint::TopLeft;
    case 8:
        return AnchorPoint::TopCenter;
    case 9:
        return AnchorPoint::TopRight;
    default:
        throw ConversionError(std::to_string(alignment) + " is not a valid alignment");
    }
}

struct FileSection {
    std::optional<std::vector<u16>> format;
    struct Item {
        u16 type;
        std::vector<u16> values;
    };
    std::vector<Item> items;

    std::size_t field(u16v name) const
    {
        if (format)
            for (std::size_t i = 0; i < format->size(); ++i)
                if ((*format)[i] == name)
                    return i;
        throw ConversionError("The given key was not present in the dictionary.");
    }
    const u16 *itemString(u16v type) const
    {
        for (const Item &item : items)
            if (item.type == type)
                return item.values.empty() ? nullptr : &item.values.front();
        return nullptr;
    }
    int itemInt(u16v type, int fallback) const
    {
        const u16 *s = itemString(type);
        if (!s)
            return fallback;
        return parseInt(*s).value_or(fallback);
    }
};

struct Item {
    const FileSection &section;
    const std::vector<u16> &values;

    const u16 &string(u16v field) const
    {
        const std::size_t i = section.field(field);
        if (i >= values.size())
            throw ConversionError("Index was out of range. Must be non-negative and less than the size of the collection.");
        return values[i];
    }
    int integer(u16v field) const { return parseInt(string(field)).value_or(0); }
    float number(u16v field) const { return parseFloat(string(field), true).value_or(0.0f); }
    bool flag(u16v field) const { return integer(field) != 0; }
    Color colour(u16v field) const
    {
        const u16 &value = string(field);
        if (value.size() != 10 || !startsWithLinguistic(value, u"&H"))
            throw ConversionError("Not a valid color");
        const auto abgr = parseHex(u16v(value).substr(2), false);
        if (!abgr)
            throw ConversionError("Not a valid color");
        const int a = 255 - static_cast<int>(*abgr >> 24);
        return Color::fromArgb(a, static_cast<int>(*abgr & 0xFF), static_cast<int>((*abgr >> 8) & 0xFF), static_cast<int>((*abgr >> 16) & 0xFF));
    }
    Ticks timestamp(u16v field) const
    {
        static const NetRegex kTimestamp(u"^(\\d+):(\\d\\d):(\\d\\d)\\.(\\d\\d)$");
        const u16 &value = string(field);
        const auto m = kTimestamp.match(value);
        if (!m)
            throw ConversionError("Not a valid timestamp");
        auto part = [&](std::size_t g) {
            const auto v = parseInt(m->group(g));
            if (!v)
                throw ConversionError("Input string was not in a correct format.");
            return *v;
        };
        const int hours = part(1), minutes = part(2), seconds = part(3), centis = part(4);
        if (minutes > 59 || seconds > 59)
            throw ConversionError("Year, Month, and Day parameters describe an un-representable DateTime.");
        return ((static_cast<Ticks>(minutes) * 60 + seconds) * 1000 + centis * 10) * kTicksPerMs + static_cast<Ticks>(hours) * 3600 * 1000 * kTicksPerMs;
    }
};

std::vector<u16> readLines(u16v text)
{
    std::vector<u16> lines;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = start;
        while (end < text.size() && text[end] != u'\r' && text[end] != u'\n')
            ++end;
        lines.emplace_back(text.substr(start, end - start));
        if (end < text.size() && text[end] == u'\r' && end + 1 < text.size() && text[end + 1] == u'\n')
            ++end;
        start = end + 1;
    }
    return lines;
}

bool isNullOrWhiteSpace(u16v s)
{
    return std::all_of(s.begin(), s.end(), isWhiteSpace);
}

std::vector<std::pair<u16, FileSection>> readDocument(u16v text)
{
    static const NetRegex kItem(u"^([\\w ]+):\\s*(.+)$");
    std::vector<std::pair<u16, FileSection>> sections;
    FileSection *current = nullptr;
    for (const u16 &line : readLines(text)) {
        if (isNullOrWhiteSpace(line) || startsWithLinguistic(line, u";"))
            continue;
        if (startsWithLinguistic(line, u"[") && endsWithLinguistic(line, u"]")) {
            u16 name = line.size() >= 2 ? line.substr(1, line.size() - 2) : u16();
            for (const auto &[existing, _] : sections)
                if (existing == name)
                    throw ConversionError("An item with the same key has already been added.");
            sections.emplace_back(std::move(name), FileSection{});
            current = &sections.back().second;
            continue;
        }
        const auto m = kItem.match(line);
        if (!m)
            throw ConversionError("Unrecognized line in .ass");
        if (!current)
            throw ConversionError("Line is not inside a section");
        const u16 type(m->group(1));
        std::optional<std::size_t> maxItems;
        if (current->format)
            maxItems = current->format->size();
        std::vector<u16> values = splitMax(m->group(2), u",", maxItems);
        for (u16 &v : values)
            v = u16(trim(v));
        if (type == u"Format") {
            if (current->format)
                throw ConversionError("Section has multiple Format items");
            for (std::size_t i = 0; i < values.size(); ++i)
                for (std::size_t j = 0; j < i; ++j)
                    if (values[i] == values[j])
                        throw ConversionError("An item with the same key has already been added.");
            current->format = std::move(values);
        } else {
            current->items.push_back({type, std::move(values)});
        }
    }
    return sections;
}

const FileSection &sectionNamed(const std::vector<std::pair<u16, FileSection>> &sections, u16v name)
{
    for (const auto &[n, s] : sections)
        if (n == name)
            return s;
    throw ConversionError("The given key was not present in the dictionary.");
}

AssStyle styleFrom(const Item &item)
{
    AssStyle s;
    s.name = item.string(u"Name");
    s.font = item.string(u"Fontname");
    s.lineHeight = item.number(u"Fontsize");
    s.bold = item.flag(u"Bold");
    s.italic = item.flag(u"Italic");
    s.underline = item.flag(u"Underline");
    s.primaryColor = item.colour(u"PrimaryColour");
    s.secondaryColor = item.colour(u"SecondaryColour");
    s.outlineColor = item.colour(u"OutlineColour");
    s.outlineThickness = item.number(u"Outline");
    s.outlineIsBox = item.integer(u"BorderStyle") == 3;
    s.shadowColor = item.colour(u"BackColour");
    s.shadowDistance = item.number(u"Shadow");
    s.anchorPoint = anchorForAlignment(item.integer(u"Alignment"));
    return s;
}

u16 resolveEscapeSequences(u16v text)
{
    return replaceAll(replaceAll(replaceAll(text, u"\\h", u"\u00A0"), u"\\n", u"\u00A0"), u"\\N", u"\r\n");
}

}

struct TagContext {
    AssDocument *document = nullptr;
    const AssStyle *initialStyle = nullptr;
    const AssStyleOptions *initialStyleOptions = nullptr;
    const AssStyle *style = nullptr;
    const AssStyleOptions *styleOptions = nullptr;
    LinePtr line;
    Section section;
    std::vector<std::function<std::vector<LinePtr>()>> postProcessors;

    const AssStyle &currentStyle() const
    {
        if (!style)
            throw ConversionError("Object reference not set to an instance of an object.");
        return *style;
    }
    const AssStyle &firstStyle() const
    {
        if (!initialStyle)
            throw ConversionError("Object reference not set to an instance of an object.");
        return *initialStyle;
    }
};

void applyStyle(Section &section, const AssDocument &doc, const AssStyle &style, const AssStyleOptions *options)
{
    section.font = style.font;
    section.scale = style.lineHeight / doc.defaultStyle->lineHeight;
    section.bold = style.bold;
    section.italic = style.italic;
    section.underline = style.underline;
    section.foreColor = style.primaryColor;
    section.secondaryColor = style.secondaryColor;
    if (options && options->isKaraoke) {
        section.currentWordForeColor = options->currentWordTextColor;
        section.currentWordOutlineColor = options->currentWordOutlineColor;
        section.currentWordShadowColor = options->currentWordShadowColor;
    } else {
        section.currentWordForeColor = Color();
        section.currentWordOutlineColor = Color();
        section.currentWordShadowColor = Color();
    }
    section.backColor = Color();
    section.shadowColors.clear();
    if (style.hasShadow()) {
        const std::vector<ShadowType> soft{ShadowType::SoftShadow};
        for (ShadowType type : options ? options->shadowTypes : soft)
            section.shadowColors.set(type, style.shadowColor);
    }
    if (style.hasOutline()) {
        if (style.outlineIsBox)
            section.backColor = style.outlineColor;
        else
            section.shadowColors.set(ShadowType::Glow, style.outlineColor);
    }
    section.blur = 0;
}

namespace {

bool tryParseIntArg(u16v arg, int &value)
{
    const u16 cleaned = replaceAll(replaceAll(replaceAll(arg, u"(", u""), u")", u""), u" ", u"");
    const auto v = parseInt(cleaned);
    value = v.value_or(0);
    return v.has_value();
}

int parseIntArg(u16v arg)
{
    int v = 0;
    tryParseIntArg(arg, v);
    return v;
}

bool tryParseFloatArg(u16v arg, float &value)
{
    const u16 cleaned = replaceAll(replaceAll(replaceAll(arg, u"(", u""), u")", u""), u" ", u"");
    const auto v = parseFloat(cleaned);
    value = v.value_or(0.0f);
    return v.has_value();
}

int parseHexArg(u16v arg)
{
    const u16 cleaned = replaceAll(replaceAll(replaceAll(replaceAll(arg, u"&", u""), u"H", u""), u"(", u""), u")", u"");
    return static_cast<int>(parseHex(cleaned, true).value_or(0));
}

std::optional<std::vector<u16>> parseStringList(u16v arg)
{
    if (isNullOrWhiteSpace(arg))
        return std::vector<u16>{};
    arg = trim(arg);
    if (!startsWithLinguistic(arg, u"("))
        return std::nullopt;
    std::vector<u16> items = split(replaceAll(replaceAll(arg, u"(", u""), u")", u""), u',');
    for (u16 &item : items)
        item = u16(trim(item));
    return items;
}

std::optional<std::vector<float>> parseFloatList(u16v arg)
{
    const auto items = parseStringList(arg);
    if (!items)
        return std::nullopt;
    std::vector<float> out;
    for (const u16 &item : *items)
        out.push_back(parseFloat(replaceAll(item, u" ", u"")).value_or(0.0f));
    return out;
}

Color parseColourArg(u16v arg, int alpha)
{
    const int bgr = parseHexArg(arg);
    return Color::fromArgb(alpha, bgr & 0xFF, (bgr >> 8) & 0xFF, (bgr >> 16) & 0xFF);
}

int alphaArg(u16v arg)
{
    return 255 - (parseHexArg(arg) & 255);
}

using Kind = Animation::Kind;

template<typename Pred, typename Current, typename Make>
ColourAnimation &fetchColourAnimation(TagContext &ctx, Kind kind, Ticks start, Ticks end, Pred pred, Current sectionColour, Make make)
{
    Section &section = ctx.section;
    section.animations.removeIf([&](const Animation &a) { return a.kind() == kind && a.startTime > start; });
    for (const auto &a : section.animations.items)
        if (a->kind() == kind && a->startTime == start && a->endTime == end && pred(static_cast<ColourAnimation &>(*a)))
            return static_cast<ColourAnimation &>(*a);
    const ColourAnimation *previous = nullptr;
    for (const auto &a : section.animations.items)
        if (a->kind() == kind && pred(static_cast<ColourAnimation &>(*a)))
            previous = static_cast<ColourAnimation *>(a.get());
    auto anim = make(previous ? previous->endColor : sectionColour());
    section.animations.add(anim);
    return *anim;
}

ColourAnimation &fetchPlain(TagContext &ctx, Kind kind, Ticks start, Ticks end, float accel, Color sectionColour)
{
    return fetchColourAnimation(
        ctx, kind, start, end, [](const ColourAnimation &) { return true; }, [&] { return sectionColour; },
        [&](Color c) { return std::make_shared<ColourAnimation>(kind, start, c, end, c, accel); });
}

ColourAnimation &fetchShadow(TagContext &ctx, ShadowType type, Ticks start, Ticks end, float accel)
{
    return fetchColourAnimation(
        ctx, Kind::ShadowColor, start, end, [type](const ColourAnimation &a) { return a.shadowType == type; },
        [&] { return ctx.section.shadowColors.at(type); },
        [&](Color c) { return std::make_shared<ColourAnimation>(Kind::ShadowColor, start, c, end, c, accel, type); });
}

bool shadowTagApplies(const TagContext &ctx, ShadowType type)
{
    const AssStyle &s = ctx.currentStyle();
    return type != ShadowType::Glow || !s.hasOutline() || s.hasOutlineBox();
}

void transformShadowColour(TagContext &ctx, ShadowType type, Ticks start, Ticks end, float accel, u16v arg)
{
    ColourAnimation &anim = fetchShadow(ctx, type, start, end, accel);
    anim.endColor = parseColourArg(arg, anim.endColor.a());
}

void transformShadowAlpha(TagContext &ctx, ShadowType type, Ticks start, Ticks end, float accel, u16v arg)
{
    ColourAnimation &anim = fetchShadow(ctx, type, start, end, accel);
    anim.endColor = changeAlpha(anim.endColor, alphaArg(arg));
}

void transformForeAlpha(TagContext &ctx, Ticks start, Ticks end, float accel, u16v arg)
{
    ColourAnimation &anim = fetchPlain(ctx, Kind::ForeColor, start, end, accel, ctx.section.foreColor);
    anim.endColor = changeAlpha(anim.endColor, alphaArg(arg));
}

void transformSecondaryAlpha(TagContext &ctx, Ticks start, Ticks end, float accel, u16v arg)
{
    ColourAnimation &anim = fetchPlain(ctx, Kind::SecondaryColor, start, end, accel, ctx.section.secondaryColor);
    anim.endColor = changeAlpha(anim.endColor, alphaArg(arg));
}

void transformOutlineAlpha(TagContext &ctx, Ticks start, Ticks end, float accel, u16v arg)
{
    const AssStyle &s = ctx.currentStyle();
    if (!s.hasOutline())
        return;
    if (s.outlineIsBox) {
        ColourAnimation &anim = fetchPlain(ctx, Kind::BackColor, start, end, accel, ctx.section.backColor);
        anim.endColor = changeAlpha(anim.endColor, alphaArg(arg));
    } else {
        transformShadowAlpha(ctx, ShadowType::Glow, start, end, accel, arg);
    }
}

void transformAllShadowAlpha(TagContext &ctx, Ticks start, Ticks end, float accel, u16v arg)
{
    for (ShadowType type : ctx.section.shadowColors.keys())
        if (shadowTagApplies(ctx, type))
            transformShadowAlpha(ctx, type, start, end, accel, arg);
}

void handleTransformTag(TagContext &ctx, u16v tag, Ticks start, Ticks end, float accel, u16v arg)
{
    if (tag == u"c" || tag == u"1c") {
        ColourAnimation &anim = fetchPlain(ctx, Kind::ForeColor, start, end, accel, ctx.section.foreColor);
        anim.endColor = parseColourArg(arg, anim.endColor.a());
    } else if (tag == u"2c") {
        ColourAnimation &anim = fetchPlain(ctx, Kind::SecondaryColor, start, end, accel, ctx.section.secondaryColor);
        anim.endColor = parseColourArg(arg, anim.endColor.a());
    } else if (tag == u"3c") {
        const AssStyle &s = ctx.currentStyle();
        if (!s.hasOutline())
            return;
        if (s.outlineIsBox) {
            ColourAnimation &anim = fetchPlain(ctx, Kind::BackColor, start, end, accel, ctx.section.backColor);
            anim.endColor = parseColourArg(arg, anim.endColor.a());
        } else {
            transformShadowColour(ctx, ShadowType::Glow, start, end, accel, arg);
        }
    } else if (tag == u"4c") {
        for (ShadowType type : ctx.section.shadowColors.keys())
            if (shadowTagApplies(ctx, type))
                transformShadowColour(ctx, type, start, end, accel, arg);
    } else if (tag == u"alpha") {
        transformForeAlpha(ctx, start, end, accel, arg);
        transformSecondaryAlpha(ctx, start, end, accel, arg);
        transformOutlineAlpha(ctx, start, end, accel, arg);
        transformAllShadowAlpha(ctx, start, end, accel, arg);
    } else if (tag == u"1a") {
        transformForeAlpha(ctx, start, end, accel, arg);
    } else if (tag == u"2a") {
        transformSecondaryAlpha(ctx, start, end, accel, arg);
    } else if (tag == u"3a") {
        transformOutlineAlpha(ctx, start, end, accel, arg);
    } else if (tag == u"4a") {
        transformAllShadowAlpha(ctx, start, end, accel, arg);
    } else if (tag == u"fs") {
        float lineHeight = 0;
        if (!tryParseFloatArg(arg, lineHeight))
            return;
        Section &section = ctx.section;
        section.animations.removeIf([&](const Animation &a) { return a.kind() == Kind::Scale && a.startTime >= start; });
        const ScaleAnimation *previous = nullptr;
        for (const auto &a : section.animations.items)
            if (a->kind() == Kind::Scale)
                previous = static_cast<const ScaleAnimation *>(a.get());
        const float startScale = previous ? previous->endScale : section.scale;
        const float endScale = lineHeight / ctx.document->defaultStyle->lineHeight;
        section.animations.add(std::make_shared<ScaleAnimation>(start, startScale, end, endScale, accel));
    }
}

void handleTransform(TagContext &ctx, u16v arg)
{
    Ticks start = ctx.line->start, end = ctx.line->end;
    float accel = 1;
    u16 modifiers;
    const auto args = parseStringList(arg);
    if (!args)
        return;
    int t1 = 0, t2 = 0;
    switch (args->size()) {
    case 1:
        modifiers = (*args)[0];
        break;
    case 2:
        if (!tryParseFloatArg((*args)[0], accel))
            return;
        modifiers = (*args)[1];
        break;
    case 3:
        if (!tryParseIntArg((*args)[0], t1) || !tryParseIntArg((*args)[1], t2))
            return;
        start = addMilliseconds(ctx.line->start, t1);
        end = addMilliseconds(ctx.line->start, t2);
        modifiers = (*args)[2];
        break;
    case 4:
        if (!tryParseIntArg((*args)[0], t1) || !tryParseIntArg((*args)[1], t2) || !tryParseFloatArg((*args)[2], accel))
            return;
        start = addMilliseconds(ctx.line->start, t1);
        end = addMilliseconds(ctx.line->start, t2);
        modifiers = (*args)[3];
        break;
    default:
        return;
    }
    ctx.line->androidDarkTextHackAllowed = false;
    static const NetRegex kModifier(u"\\\\(?<tag>\\d?[a-z]+)(?<arg>[^\\\\]*)");
    static const std::size_t kTag = kModifier.groupNumber("tag"), kArg = kModifier.groupNumber("arg");
    for (const auto &m : kModifier.matches(modifiers))
        handleTransformTag(ctx, m.group(kTag), start, end, accel, trim(m.group(kArg)));
}

void setStyleFlag(bool &flag, u16v arg, const TagContext &ctx, bool AssStyle::*initial)
{
    if (arg.empty()) {
        flag = ctx.firstStyle().*initial;
        return;
    }
    int value = 0;
    if (!tryParseIntArg(arg, value)) {
        flag = false;
        return;
    }
    flag = value == 0 ? false : value == 1 ? true : ctx.firstStyle().*initial;
}

std::shared_ptr<const KaraokeType> karaokeTypeFor(u16v arg)
{
    static const auto simple = simpleKaraokeType();
    static const auto fade = fadeKaraokeType();
    static const auto glitch = glitchKaraokeType();
    const auto args = parseStringList(arg);
    const u16 typeName = toLowerInvariant(args && !args->empty() ? u16v((*args)[0]) : arg);
    if (typeName == u"fade")
        return fade;
    if (typeName == u"glitch")
        return glitch;
    if (typeName == u"cursor" || typeName == u"lcursor") {
        const bool before = typeName == u"lcursor";
        constexpr Ticks kHour = 3600LL * 1000 * kTicksPerMs;
        Ticks interval = kHour;
        std::vector<u16> cursors;
        if (!args || args->size() == 1) {
            cursors.push_back(u"_");
        } else if (args->size() == 2) {
            cursors.push_back((*args)[1]);
        } else if (args->size() == 3) {
            cursors.push_back(u"{" + (*args)[1] + u"}" + (*args)[2]);
        } else {
            const int ms = parseInt((*args)[1]).value_or(std::numeric_limits<int>::max());
            interval = static_cast<Ticks>(ms) * kTicksPerMs;
            for (std::size_t i = 2; i < args->size(); i += 2)
                cursors.push_back(u"{" + (*args)[i] + u"}" + (i + 1 < args->size() ? (*args)[i + 1] : u"_"));
        }
        return cursorKaraokeType(std::move(cursors), interval, before);
    }
    return simple;
}

std::vector<LinePtr> chromaLines(const LinePtr &original, std::vector<Color> &colours, PointF center,
                                 int maxOffsetX, int maxOffsetY, int durationMs, bool moveIn)
{
    std::vector<LinePtr> out;
    if (original->sections.empty())
        return out;
    for (std::size_t i = 0; i < colours.size(); ++i) {
        if (colours[i].a() == 0)
            continue;
        LinePtr line = cloneLine(*original);
        for (Section &section : line->sections) {
            const int alpha = static_cast<int>(static_cast<float>(section.foreColor.a()) * (static_cast<float>(colours[i].a()) / 255.0f));
            section.foreColor = Color::fromArgb(alpha, colours[i].r(), colours[i].g(), colours[i].b());
            section.backColor = Color();
            section.shadowColors.clear();
        }
        const float factor = colours.size() > 1 ? static_cast<float>(i) / static_cast<float>(colours.size() - 1) : 0.5f;
        const float offsetX = factor * static_cast<float>(-maxOffsetX * 2) + static_cast<float>(maxOffsetX);
        const float offsetY = factor * static_cast<float>(-maxOffsetY * 2) + static_cast<float>(maxOffsetY);
        const PointF far{center.x + offsetX, center.y + offsetY};
        const PointF near{center.x + offsetX / 5, center.y + offsetY / 5};
        if (moveIn) {
            line->end = roundTimeToFrameCenter(addMilliseconds(original->start, durationMs));
            line->animations.add(std::make_shared<MoveAnimation>(line->start, far, line->end, near));
        } else {
            line->start = roundTimeToFrameCenter(addMilliseconds(original->end, -durationMs));
            line->animations.add(std::make_shared<MoveAnimation>(line->start, near, line->end, far));
        }
        out.push_back(std::move(line));
    }
    return out;
}

void handleChroma(TagContext &ctx, u16v arg)
{
    std::vector<Color> colours;
    int alpha = 128, maxOffsetX = 20, maxOffsetY = 0, inMs = 270, outMs = 270;
    const auto args = parseStringList(arg);
    if (!args)
        return;
    const std::size_t n = args->size();
    auto intAt = [&](std::size_t fromEnd, int &value) {
        const auto v = parseInt((*args)[n - fromEnd]);
        value = v.value_or(0);
        return v.has_value();
    };
    if (n >= 5)
        alpha = parseHexArg((*args)[n - 5]) & 255;
    if (n >= 4 && !intAt(4, maxOffsetX))
        return;
    if (n >= 3 && !intAt(3, maxOffsetY))
        return;
    if (n >= 2 && !intAt(2, inMs))
        return;
    if (n >= 1 && !intAt(1, outMs))
        return;
    alpha = 255 - alpha;
    for (std::size_t i = 0; i + 5 < n; ++i)
        colours.push_back(parseColourArg((*args)[i], alpha));

    AssDocument *doc = ctx.document;
    LinePtr line = ctx.line;
    ctx.postProcessors.push_back([doc, line, colours, alpha, maxOffsetX, maxOffsetY, inMs, outMs]() mutable {
        const PointF center = line->position ? *line->position : doc->defaultPosition(line->anchorPoint);
        std::vector<LinePtr> lines;
        if (colours.empty()) {
            const Color base = !line->sections.empty() ? line->sections[0].foreColor : Color::fromArgb(0xFFFFFFFFu);
            auto part = [alpha](int component) { return static_cast<int>(static_cast<float>(component * alpha) / 255.0f); };
            colours.push_back(Color::fromArgb(part(base.r()), 255, 0, 0));
            colours.push_back(Color::fromArgb(part(base.g()), 0, 255, 0));
            colours.push_back(Color::fromArgb(part(base.b()), 0, 0, 255));
        }
        if (inMs > 0) {
            auto more = chromaLines(line, colours, center, maxOffsetX, maxOffsetY, inMs, true);
            lines.insert(lines.end(), more.begin(), more.end());
            line->start = roundTimeToFrameCenter(addMilliseconds(line->start, inMs));
        }
        if (outMs > 0) {
            auto more = chromaLines(line, colours, center, maxOffsetX, maxOffsetY, outMs, false);
            lines.insert(lines.end(), more.begin(), more.end());
            line->end = roundTimeToFrameCenter(addMilliseconds(line->end, -outMs));
        }
        return lines;
    });
}

void handleShake(TagContext &ctx, u16v arg)
{
    PointF radius{20, 20};
    Ticks start = ctx.line->start, end = ctx.line->end;
    if (!isNullOrWhiteSpace(arg)) {
        const auto args = parseFloatList(arg);
        if (!args)
            return;
        switch (args->size()) {
        case 0:
            break;
        case 1:
            radius = {(*args)[0], (*args)[0]};
            break;
        case 2:
            radius = {(*args)[0], (*args)[1]};
            break;
        case 3:
            radius = {(*args)[0], (*args)[0]};
            start = addMilliseconds(ctx.line->start, (*args)[1]);
            end = addMilliseconds(ctx.line->start, (*args)[2]);
            break;
        case 4:
            radius = {(*args)[0], (*args)[1]};
            start = addMilliseconds(ctx.line->start, (*args)[2]);
            end = addMilliseconds(ctx.line->start, (*args)[3]);
            break;
        default:
            return;
        }
    }
    LinePtr line = ctx.line;
    ctx.postProcessors.push_back([line, start, end, radius]() {
        line->animations.add(std::make_shared<ShakeAnimation>(start, end, radius));
        return std::vector<LinePtr>{};
    });
}

void handleTag(TagContext &ctx, u16v tag, u16v arg)
{
    Section &s = ctx.section;
    Line &line = *ctx.line;
    if (tag == u"an") {
        const int alignment = parseIntArg(arg);
        if (alignment >= 1 && alignment <= 9)
            line.anchorPoint = anchorForAlignment(alignment);
    } else if (tag == u"alpha") {
        const int alpha = alphaArg(arg);
        s.foreColor = changeAlpha(s.foreColor, alpha);
        s.secondaryColor = changeAlpha(s.secondaryColor, alpha);
        if (ctx.currentStyle().hasOutlineBox())
            s.backColor = changeAlpha(s.backColor, alpha);
        for (ShadowType type : s.shadowColors.keys())
            s.shadowColors.set(type, changeAlpha(s.shadowColors.at(type), alpha));
        s.animations.removeIf([](const Animation &a) { return a.isColour(); });
    } else if (tag == u"b") {
        setStyleFlag(s.bold, arg, ctx, &AssStyle::bold);
    } else if (tag == u"ytchroma") {
        handleChroma(ctx, arg);
    } else if (tag == u"fade") {
        const auto args = parseFloatList(arg);
        if (!args || args->size() != 7)
            return;
        const auto &a = *args;
        const int initial = 255 - static_cast<int>(a[0]), mid = 255 - static_cast<int>(a[1]), final = 255 - static_cast<int>(a[2]);
        const Ticks inStart = addMilliseconds(line.start, a[3]), inEnd = addMilliseconds(line.start, a[4]);
        const Ticks outStart = addMilliseconds(line.start, a[5]), outEnd = addMilliseconds(line.start, a[6]);
        if (inEnd > inStart)
            line.animations.add(std::make_shared<FadeAnimation>(inStart, initial, inEnd, mid));
        if (outEnd > outStart)
            line.animations.add(std::make_shared<FadeAnimation>(outStart, mid, outEnd, final));
    } else if (tag == u"fs") {
        float lineHeight = 0;
        if (!tryParseFloatArg(arg, lineHeight))
            lineHeight = ctx.currentStyle().lineHeight;
        s.scale = lineHeight / ctx.document->defaultStyle->lineHeight;
    } else if (tag == u"fn") {
        s.font = !arg.empty() ? u16(arg) : ctx.currentStyle().font;
    } else if (tag == u"1a") {
        s.foreColor = changeAlpha(s.foreColor, !arg.empty() ? alphaArg(arg) : ctx.currentStyle().primaryColor.a());
        s.animations.removeKind(Kind::ForeColor);
    } else if (tag == u"c" || tag == u"1c") {
        s.foreColor = !arg.empty() ? parseColourArg(arg, s.foreColor.a()) : ctx.currentStyle().primaryColor;
        s.animations.removeKind(Kind::ForeColor);
    } else if (tag == u"ytdir") {
        const int direction = parseInt(arg).value_or(6);
        line.horizontalTextDirection = direction == 4 ? HorizontalTextDirection::RightToLeft : HorizontalTextDirection::LeftToRight;
    } else if (tag == u"i") {
        setStyleFlag(s.italic, arg, ctx, &AssStyle::italic);
    } else if (tag == u"k") {
        const int centis = parseInt(arg).value_or(0);
        s.duration = static_cast<Ticks>(centis) * 10 * kTicksPerMs;
    } else if (tag == u"ytkt") {
        line.karaokeType = karaokeTypeFor(arg);
    } else if (tag == u"move") {
        const auto args = parseFloatList(arg);
        if (!args || args->size() < 4)
            return;
        const auto &a = *args;
        Ticks start = line.start, end = line.end;
        if (a.size() >= 6) {
            start = addMilliseconds(line.start, a[4]);
            end = addMilliseconds(line.start, a[5]);
        }
        if (end > start)
            line.animations.add(std::make_shared<MoveAnimation>(start, PointF{a[0], a[1]}, end, PointF{a[2], a[3]}));
    } else if (tag == u"3a") {
        const AssStyle &st = ctx.currentStyle();
        if (!st.hasOutline())
            return;
        const int alpha = !arg.empty() ? alphaArg(arg) : st.outlineColor.a();
        if (st.outlineIsBox) {
            s.backColor = changeAlpha(s.backColor, alpha);
            s.animations.removeKind(Kind::BackColor);
        } else {
            s.shadowColors.set(ShadowType::Glow, changeAlpha(s.shadowColors.at(ShadowType::Glow), alpha));
            s.animations.removeKind(Kind::ShadowColor);
        }
    } else if (tag == u"3c") {
        const AssStyle &st = ctx.currentStyle();
        if (!st.hasOutline())
            return;
        if (st.outlineIsBox) {
            s.backColor = !arg.empty() ? parseColourArg(arg, s.backColor.a()) : st.outlineColor;
            s.animations.removeKind(Kind::BackColor);
        } else {
            s.shadowColors.set(ShadowType::Glow, !arg.empty() ? parseColourArg(arg, s.shadowColors.at(ShadowType::Glow).a()) : st.outlineColor);
            s.animations.removeKind(Kind::ShadowColor);
        }
    } else if (tag == u"ytpack") {
        s.packed = arg != u"0";
    } else if (tag == u"pos") {
        const auto coords = parseFloatList(arg);
        if (!coords || coords->size() != 2 || line.position)
            return;
        line.position = PointF{(*coords)[0], (*coords)[1]};
    } else if (tag == u"ytsur") {
        s.offset = OffsetType::Regular;
    } else if (tag == u"r") {
        const AssStyle *named = ctx.document->style(arg);
        ctx.style = named ? named : ctx.initialStyle;
        const AssStyleOptions *options = ctx.document->styleOptions(arg);
        ctx.styleOptions = options ? options : ctx.initialStyleOptions;
        applyStyle(s, *ctx.document, ctx.currentStyle(), ctx.styleOptions);
        s.offset = OffsetType::Regular;
        s.rubyPart = RubyPart::None;
        s.animations.clear();
    } else if (tag == u"ytruby") {
        const int position = parseInt(arg).value_or(0);
        s.rubyPart = position == 2 ? RubyPart::TextAfter : RubyPart::TextBefore;
    } else if (tag == u"2a") {
        s.secondaryColor = changeAlpha(s.secondaryColor, !arg.empty() ? alphaArg(arg) : ctx.currentStyle().secondaryColor.a());
        s.animations.removeKind(Kind::SecondaryColor);
    } else if (tag == u"2c") {
        s.secondaryColor = !arg.empty() ? parseColourArg(arg, s.secondaryColor.a()) : ctx.currentStyle().secondaryColor;
        s.animations.removeKind(Kind::SecondaryColor);
    } else if (tag == u"4a") {
        const AssStyle &st = ctx.currentStyle();
        if (!st.hasShadow())
            return;
        const int alpha = !arg.empty() ? alphaArg(arg) : st.shadowColor.a();
        for (const auto &[type, colour] : s.shadowColors.entries())
            if (shadowTagApplies(ctx, type))
                s.shadowColors.set(type, changeAlpha(colour, alpha));
        s.animations.removeKind(Kind::ShadowColor);
    } else if (tag == u"4c") {
        const AssStyle &st = ctx.currentStyle();
        if (!st.hasShadow())
            return;
        for (const auto &[type, colour] : s.shadowColors.entries())
            if (shadowTagApplies(ctx, type))
                s.shadowColors.set(type, !arg.empty() ? parseColourArg(arg, colour.a()) : st.shadowColor);
        s.animations.removeKind(Kind::ShadowColor);
    } else if (tag == u"ytshake") {
        handleShake(ctx, arg);
    } else if (tag == u"fad") {
        const auto times = parseFloatList(arg);
        if (!times || times->size() != 2)
            return;
        const Ticks inStart = line.start, inEnd = addMilliseconds(line.start, (*times)[0]);
        const Ticks outStart = addMilliseconds(line.end, -(*times)[1]), outEnd = line.end;
        if (inEnd > inStart)
            line.animations.add(std::make_shared<FadeAnimation>(inStart, 0, inEnd, 255));
        if (outEnd > outStart)
            line.animations.add(std::make_shared<FadeAnimation>(outStart, 255, outEnd, 0));
    } else if (tag == u"ytsub") {
        s.offset = OffsetType::Subscript;
    } else if (tag == u"ytsup") {
        s.offset = OffsetType::Superscript;
    } else if (tag == u"t") {
        handleTransform(ctx, arg);
    } else if (tag == u"u") {
        setStyleFlag(s.underline, arg, ctx, &AssStyle::underline);
    } else if (tag == u"ytvert") {
        const int id = parseInt(arg).value_or(9);
        switch (id) {
        case 9:
            line.horizontalTextDirection = HorizontalTextDirection::RightToLeft;
            line.verticalTextType = VerticalTextType::Positioned;
            break;
        case 7:
            line.horizontalTextDirection = HorizontalTextDirection::LeftToRight;
            line.verticalTextType = VerticalTextType::Positioned;
            break;
        case 1:
            line.horizontalTextDirection = HorizontalTextDirection::LeftToRight;
            line.verticalTextType = VerticalTextType::Rotated;
            break;
        case 3:
            line.horizontalTextDirection = HorizontalTextDirection::RightToLeft;
            line.verticalTextType = VerticalTextType::Rotated;
            break;
        default:
            line.horizontalTextDirection = HorizontalTextDirection::LeftToRight;
            line.verticalTextType = VerticalTextType::None;
            break;
        }
    }
}

bool affectsWholeLine(u16v tag)
{
    static const std::set<u16, std::less<>> kWholeLine{u"an", u"ytchroma", u"fade", u"ytdir", u"ytkt", u"move", u"pos", u"ytshake", u"fad", u"ytvert"};
    return kWholeLine.contains(tag);
}

bool isKnownTag(u16v tag)
{
    static const std::set<u16, std::less<>> kTags{u"an",  u"alpha", u"b",      u"ytchroma", u"fade", u"fs",     u"fn",    u"1a",   u"c",
                                                  u"1c",  u"ytdir", u"i",      u"k",        u"ytkt", u"move",   u"3a",    u"3c",   u"ytpack",
                                                  u"pos", u"ytsur", u"r",      u"ytruby",   u"2a",   u"2c",     u"4a",    u"4c",   u"ytshake",
                                                  u"fad", u"ytsub", u"ytsup",  u"t",        u"u",    u"ytvert"};
    return kTags.contains(tag);
}

}

void createTagSections(Line &line, u16v sourceText, TagContext &ctx)
{
    static const NetRegex kTrailingBreaks(u"(?:\\\\N)+$");
    static const NetRegex kTagGroup(u"\\{(.*?)\\}");
    static const NetRegex kKaraoke(u"\\\\k\\s*\\d+");
    static const NetRegex kTag(u"\\\\(?<tag>fn|r|\\d?[a-z]+)\\s*(?<arg>\\([^\\(\\)]*(?:\\)|$)|[^\\\\\\(\\)]*)");
    static const std::size_t kTagName = kTag.groupNumber("tag"), kTagArg = kTag.groupNumber("arg");

    const u16 text = kTrailingBreaks.replace(sourceText, [](const NetRegex::Match &) { return u16(); });
    std::set<u16, std::less<>> handledWholeLineTags;
    std::size_t start = 0;
    for (const auto &group : kTagGroup.matches(text)) {
        const std::size_t end = group.index;
        if (end > start || (ctx.section.duration > 0 && kKaraoke.isMatch(group.group(1)))) {
            ctx.section.text = end == start ? kZwsp : resolveEscapeSequences(u16v(text).substr(start, end - start));
            line.sections.push_back(ctx.section);
            ctx.section.text.clear();
            ctx.section.duration = 0;
        }
        for (const auto &tag : kTag.matches(group.group(1))) {
            const u16v name = tag.group(kTagName);
            if (!isKnownTag(name))
                continue;
            if (affectsWholeLine(name) && !handledWholeLineTags.emplace(name).second)
                continue;
            handleTag(ctx, name, trim(tag.group(kTagArg)));
        }
        start = group.index + group.length;
    }
    if (start < text.size()) {
        ctx.section.text = resolveEscapeSequences(u16v(text).substr(start));
        line.sections.push_back(ctx.section);
    }
}

std::vector<Section> cursorSections(AssDocument &doc, const Section &initialFormatting, u16v cursor)
{
    TagContext ctx;
    ctx.document = &doc;
    ctx.line = std::make_shared<Line>(kTimeBase, kTimeBase);
    ctx.section = initialFormatting;
    ctx.section.text.clear();
    createTagSections(*ctx.line, cursor, ctx);
    return std::move(ctx.line->sections);
}

namespace {

void insertRubySection(Line &line, const Section &format, u16v text, RubyPart part, std::size_t index, std::size_t &count)
{
    Section section = format;
    section.text = u16(text);
    section.rubyPart = part;
    section.duration = 0;
    line.sections.insert(line.sections.begin() + static_cast<std::ptrdiff_t>(index + count), std::move(section));
    ++count;
}

void createRubySections(Line &line)
{
    static const NetRegex kRuby(u"\\[(?<text>.+?)/(?<ruby>.+?)\\]");
    static const std::size_t kText = kRuby.groupNumber("text"), kRubyText = kRuby.groupNumber("ruby");
    for (std::size_t idx = line.sections.size(); idx-- > 0;) {
        if (line.sections[idx].rubyPart == RubyPart::None)
            continue;
        const Section section = line.sections[idx];
        const u16v text = section.text;
        const auto found = kRuby.matches(text);
        if (found.empty()) {
            line.sections[idx].rubyPart = RubyPart::None;
            continue;
        }
        line.sections.erase(line.sections.begin() + static_cast<std::ptrdiff_t>(idx));
        std::size_t inter = 0, count = 0;
        for (const auto &m : found) {
            if (m.index > inter)
                insertRubySection(line, section, text.substr(inter, m.index - inter), RubyPart::None, idx, count);
            insertRubySection(line, section, m.group(kText), RubyPart::Base, idx, count);
            insertRubySection(line, section, u"(", RubyPart::Parenthesis, idx, count);
            insertRubySection(line, section, m.group(kRubyText), section.rubyPart, idx, count);
            insertRubySection(line, section, u")", RubyPart::Parenthesis, idx, count);
            inter = m.index + m.length;
        }
        if (inter < text.size())
            insertRubySection(line, section, text.substr(inter), RubyPart::None, idx, count);
        line.sections[idx].duration = section.duration;
    }
}

std::vector<LinePtr> parseLine(AssDocument &doc, const Item &dialogue, const AssStyle &style, const AssStyleOptions *options)
{
    const Ticks start = dialogue.timestamp(u"Start"), end = dialogue.timestamp(u"End");
    auto line = std::make_shared<Line>(roundTimeToFrameCenter(start), roundTimeToFrameCenter(end));
    line->layer = dialogue.integer(u"Layer");
    line->anchorPoint = style.anchorPoint;
    line->karaokeType = simpleKaraokeType();
    for (const u16 &effect : split(dialogue.string(u"Effect"), u';'))
        if (effect == u"no_android_dark_text_hack")
            line->androidDarkTextHackAllowed = false;

    TagContext ctx;
    ctx.document = &doc;
    ctx.initialStyle = ctx.style = &style;
    ctx.initialStyleOptions = ctx.styleOptions = options;
    ctx.line = line;
    applyStyle(ctx.section, doc, style, options);
    createTagSections(*line, dialogue.string(u"Text"), ctx);
    createRubySections(*line);

    std::vector<LinePtr> lines{line};
    for (auto &post : ctx.postProcessors) {
        auto extra = post();
        lines.insert(lines.end(), extra.begin(), extra.end());
    }
    return lines;
}

bool canUseNativeKaraoke(const Line &line)
{
    if (!line.karaokeType->isPlainSimple() || !line.animations.empty())
        return false;
    for (const Section &s : line.sections)
        if (!(s.secondaryColor.a() == 0 && s.currentWordForeColor.isEmpty() && s.currentWordOutlineColor.isEmpty() &&
              s.currentWordShadowColor.isEmpty() && s.animations.empty() && s.duration != 0))
            return false;
    return true;
}

std::vector<LinePtr> expandLineForKaraoke(AssDocument &doc, const LinePtr &line)
{
    if (std::all_of(line->sections.begin(), line->sections.end(), [](const Section &s) { return s.duration == 0; }))
        return {line};
    if (canUseNativeKaraoke(*line)) {
        Ticks offset = 0;
        for (Section &s : line->sections) {
            s.startOffset = offset;
            offset += s.duration;
        }
        return {line};
    }
    return emulatedKaraokeLines(doc, line);
}

void emulateKaraokeForSimultaneousLines(AssDocument &doc)
{
    auto &lines = doc.lines;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const LinePtr line = lines[i];
        if (line->position || std::none_of(line->sections.begin(), line->sections.end(), [](const Section &s) { return s.startOffset > 0; }))
            continue;
        const bool overlaps = std::any_of(lines.begin(), lines.end(), [&](const LinePtr &l) {
            return l->start < line->end && l->end > line->start && !l->position && l->anchorPoint == line->anchorPoint;
        });
        if (!overlaps)
            continue;
        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(i));
        auto emulated = emulatedKaraokeLines(doc, line);
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(i), emulated.begin(), emulated.end());
    }
}

}

AssDocument readAss(u16v script, std::vector<AssStyleOptions> options)
{
    AssDocument doc;
    const auto sections = readDocument(script);
    const FileSection &info = sectionNamed(sections, u"Script Info");
    doc.video = {info.itemInt(u"PlayResX", 384), info.itemInt(u"PlayResY", 288)};
    const u16 *collisions = info.itemString(u"Collisions");
    doc.lineMergeType = collisions && *collisions == u"Reverse" ? LineMergeType::MoveExisting : LineMergeType::MoveNew;

    const FileSection &styles = sectionNamed(sections, u"V4+ Styles");
    for (const auto &item : styles.items) {
        if (item.type != u"Style")
            continue;
        AssStyle style = styleFrom(Item{styles, item.values});
        auto existing = std::find_if(doc.styles.begin(), doc.styles.end(), [&](const AssStyle &s) { return s.name == style.name; });
        if (existing != doc.styles.end())
            *existing = std::move(style);
        else
            doc.styles.push_back(std::move(style));
    }
    for (AssStyleOptions &o : options) {
        auto existing = std::find_if(doc.options.begin(), doc.options.end(), [&](const AssStyleOptions &e) { return e.name == o.name; });
        if (existing != doc.options.end())
            *existing = std::move(o);
        else
            doc.options.push_back(std::move(o));
    }
    doc.defaultStyle = doc.style(u"Default");
    if (!doc.defaultStyle) {
        if (doc.styles.empty())
            throw ConversionError("Sequence contains no elements");
        doc.defaultStyle = &doc.styles.front();
    }

    const FileSection &events = sectionNamed(sections, u"Events");
    for (const auto &item : events.items) {
        if (item.type != u"Dialogue")
            continue;
        const Item dialogue{events, item.values};
        const u16 &styleName = dialogue.string(u"Style");
        dialogue.integer(u"Layer");
        dialogue.timestamp(u"Start");
        dialogue.timestamp(u"End");
        dialogue.string(u"Effect");
        dialogue.string(u"Text");
        const AssStyle *style = doc.style(styleName);
        if (!style)
            throw ConversionError("A Line refers to a style which doesn't exist.");
        for (const LinePtr &line : parseLine(doc, dialogue, *style, doc.styleOptions(styleName)))
            for (const LinePtr &karaoke : expandLineForKaraoke(doc, line))
                for (LinePtr &frame : animatorExpand(doc, karaoke))
                    doc.lines.push_back(std::move(frame));
    }

    emulateKaraokeForSimultaneousLines(doc);
    for (const LinePtr &line : doc.lines)
        line->normalizeAlpha();
    return doc;
}

}
