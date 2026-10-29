#include "hikari/application/shape_presets.h"

#include "hikari/application/visual_drawing.h"

#include <algorithm>
#include <optional>
#include <cerrno>
#include <climits>
#include <cwctype>

namespace hikari::application::visual {

namespace {

using u16 = std::u16string;
using u16v = std::u16string_view;

bool isBlank(char16_t c)
{
    return c == u' ' || (c >= u'\t' && c <= u'\r');
}

std::vector<u16> strtok(u16v text, char16_t delimiter)
{
    std::vector<u16> out;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && text[i] == delimiter)
            ++i;
        if (i >= text.size())
            break;
        const std::size_t start = i;
        while (i < text.size() && text[i] != delimiter)
            ++i;
        out.emplace_back(text.substr(start, i - start));
    }
    return out;
}

u16 trimStart(u16v text)
{
    std::size_t i = 0;
    while (i < text.size() && isBlank(text[i]))
        ++i;
    return u16(text.substr(i));
}

// False for nothing read, anything left over, or a value outside `long`
// (32 bits on Windows, 64 on Linux).
bool toLong(u16v text, long &value)
{
    std::size_t i = 0;
    while (i < text.size() && isBlank(text[i]))
        ++i;
    bool negative = false;
    if (i < text.size() && (text[i] == u'+' || text[i] == u'-')) {
        negative = text[i] == u'-';
        ++i;
    }
    const std::size_t digits = i;
    unsigned long long magnitude = 0;
    bool overflow = false;
    const unsigned long long limit = negative ? static_cast<unsigned long long>(LONG_MAX) + 1ULL
                                              : static_cast<unsigned long long>(LONG_MAX);
    for (; i < text.size() && text[i] >= u'0' && text[i] <= u'9'; ++i) {
        magnitude = magnitude * 10 + static_cast<unsigned long long>(text[i] - u'0');
        if (magnitude > limit) {
            overflow = true;
            magnitude = limit;
        }
    }
    if (i == digits || i != text.size() || overflow)
        return false;
    value = negative ? static_cast<long>(-static_cast<long long>(magnitude)) : static_cast<long>(magnitude);
    return true;
}

u16 number(long long value)
{
    const std::string s = std::to_string(value);
    return u16(s.begin(), s.end());
}

char16_t lower(char16_t c)
{
    return static_cast<char16_t>(std::towlower(static_cast<wint_t>(c)));
}

}

bool sameNameIgnoringCase(u16v a, u16v b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (lower(a[i]) != lower(b[i]))
            return false;
    return true;
}

u16v defaultShapePresetsText()
{
    return u"Shape: rectangle; m 0 0 l 100 0 100 100 0 100; 0; 0\n"
           u"Shape: circle; m -100 -100 b -45 -155 45 -155 100 -100 b 155 -45 155 45 100 100 b 46 155 -45 155 -100 "
           u"100 b -155 45 -155 -45 -100 -100; 1; 0\n"
           u"Shape: rounded square 1; m -100 -25 b -100 -92 -92 -100 -25 -100 l 25 -100 b 92 -100 100 -92 100 -25 l "
           u"100 25 b 100 92 92 100 25 100 l -25 100 b -92 100 -100 92 -100 25 l -100 -25; 1; 0\n"
           u"Shape: rounded square 2; m -100 -60 b -100 -92 -92 -100 -60 -100 l 60 -100 b 92 -100 100 -92 100 -60 l "
           u"100 60 b 100 92 92 100 60 100 l -60 100 b -92 100 -100 92 -100 60 l -100 -60; 1; 0\n"
           u"Shape: rounded square 3; m -100 -85 b -100 -96 -96 -100 -85 -100 l 85 -100 b 96 -100 100 -96 100 -85 l "
           u"100 85 b 100 96 96 100 85 100 l -85 100 b -96 100 -100 96 -100 85 l -100 -85; 1; 0\n";
}

std::vector<ShapePreset> defaultShapePresets()
{
    return parseShapePresets(defaultShapePresetsText());
}

std::vector<ShapePreset> parseShapePresets(u16v text)
{
    std::vector<ShapePreset> shapes;
    for (const u16 &token : strtok(text, u'\n')) {
        static constexpr u16v prefix = u"Shape: ";
        if (!u16v(token).starts_with(prefix))
            continue;
        const std::vector<u16> parts = strtok(u16v(token).substr(prefix.size()), u';');
        ShapePreset tmp;
        tmp.name = parts.size() > 0 ? parts[0] : u16();
        if (parts.size() < 2)
            continue;
        tmp.shape = trimStart(parts[1]);
        long tmpval = 0;
        if (parts.size() < 3)
            continue;
        if (!toLong(trimStart(parts[2]), tmpval))
            continue;
        tmp.mode = static_cast<unsigned char>(tmpval);
        if (parts.size() < 4)
            continue;
        if (!toLong(trimStart(parts[3]), tmpval))
            continue;
        tmp.scalingMode = static_cast<unsigned char>(tmpval);
        shapes.push_back(std::move(tmp));
    }
    return shapes;
}

u16 writeShapePresets(const std::vector<ShapePreset> &presets)
{
    u16 out;
    for (const ShapePreset &p : presets)
        out += u"Shape: " + p.name + u"; " + p.shape + u"; " + number(p.mode) + u"; " + number(p.scalingMode) + u"\n";
    return out;
}

std::vector<u16> shapeListChoices(const std::vector<ShapePreset> &presets)
{
    std::vector<u16> list;
    list.push_back(u"Choose");
    for (const ShapePreset &p : presets)
        list.push_back(p.name);
    list.push_back(u"Edit");
    return list;
}

namespace {

int clampedSelection(int selection, std::size_t size)
{
    return selection < 0 || selection >= static_cast<int>(size) ? 0 : selection;
}

std::optional<u16> lastDrawing(u16v lineText)
{
    std::optional<u16> out;
    for (const drawing::Tag &tag : drawing::parseTags(lineText, {u"p"}))
        if (tag.name == u"pvector")
            out = tag.value;
    return out;
}

bool blank(u16v text)
{
    return std::ranges::all_of(text, isBlank);
}

}

ShapesEdition::ShapesEdition(std::vector<ShapePreset> presets, int selection)
    : m_history(presets, clampedSelection(selection, presets.size()))
{
}

const ShapePreset &ShapesEdition::current() const
{
    static const ShapePreset none;
    const int i = selection();
    return i >= 0 && i < static_cast<int>(presets().size()) ? presets()[static_cast<std::size_t>(i)] : none;
}

void ShapesEdition::select(int index)
{
    if (index >= 0 && index < static_cast<int>(presets().size()))
        m_history.select(index);
}

void ShapesEdition::edit(ShapePreset preset, Step step, std::string mergeKey, Renames renamed)
{
    const int i = selection();
    if (i < 0 || i >= static_cast<int>(presets().size()) || presets()[static_cast<std::size_t>(i)] == preset)
        return;
    std::vector<ShapePreset> items = presets();
    items[static_cast<std::size_t>(i)] = std::move(preset);
    if (!mergeKey.empty())
        mergeKey += ':' + std::to_string(i);
    m_history.change(std::move(items), i, step, std::move(mergeKey), std::move(renamed));
}

ShapesEdition::NameCheck ShapesEdition::setName(u16v name, u16 *taken)
{
    if (name.empty() || blank(name))
        return NameCheck::Empty;
    if (name.find_first_of(u";\r\n") != u16v::npos)
        return NameCheck::Separator;
    for (std::size_t i = 0; i < presets().size(); ++i) {
        if (static_cast<int>(i) != selection() && sameNameIgnoringCase(presets()[i].name, name)) {
            if (taken)
                *taken = presets()[i].name;
            return NameCheck::Taken;
        }
    }
    ShapePreset p = current();
    Renames renamed{{p.name, u16(name)}};
    p.name = u16(name);
    edit(std::move(p), Rename, "name", std::move(renamed));
    return NameCheck::Ok;
}

bool ShapesEdition::setShape(u16v shape)
{
    u16 text(shape);
    std::erase(text, u'\r');
    std::ranges::replace(text, u'\n', u' ');
    if (blank(text) || text.find(u';') != u16::npos)
        return false;
    ShapePreset p = current();
    p.shape = std::move(text);
    edit(std::move(p), EditDrawing, "shape");
    return true;
}

void ShapesEdition::setMode(int mode)
{
    if (mode < ShapePreset::SeparateScaleXY || mode > ShapePreset::OnlyScaleX)
        return;
    ShapePreset p = current();
    p.mode = mode;
    edit(std::move(p), ChangeScaling);
}

u16 ShapesEdition::freeName(u16 base, bool copy) const
{
    const auto taken = [this](u16v name) {
        return std::ranges::any_of(presets(), [&](const ShapePreset &p) { return sameNameIgnoringCase(p.name, name); });
    };
    for (int n = 1;; ++n) {
        u16 suffix = copy ? u16(u" copy") : u16();
        if (n > 1)
            suffix += u" " + number(n);
        // A copy's name is cut to fit the 20 characters the field types.
        u16 stem = base;
        if (copy && stem.size() + suffix.size() > static_cast<std::size_t>(kNameMaxLength)) {
            stem.resize(static_cast<std::size_t>(std::max<int>(1, kNameMaxLength - static_cast<int>(suffix.size()))));
            while (stem.size() > 1 && isBlank(stem.back()))
                stem.pop_back();
        }
        const u16 name = stem + suffix;
        if (!taken(name))
            return name;
    }
}

void ShapesEdition::addShape(u16v lineText)
{
    ShapePreset p;
    p.name = freeName(u"New shape", false);
    p.shape = lastDrawing(lineText).value_or(u"m 0 0 l 100 0 100 100 0 100");
    std::vector<ShapePreset> items = presets();
    items.push_back(std::move(p));
    const int at = static_cast<int>(items.size()) - 1;
    m_history.change(std::move(items), at, NewShape);
}

void ShapesEdition::duplicate()
{
    if (presets().empty())
        return;
    ShapePreset p = current();
    p.name = freeName(p.name, true);
    std::vector<ShapePreset> items = presets();
    const int at = selection() + 1;
    items.insert(items.begin() + at, std::move(p));
    m_history.change(std::move(items), at, DuplicateShape);
}

bool ShapesEdition::remove()
{
    if (!canRemove())
        return false;
    std::vector<ShapePreset> items = presets();
    const int at = selection();
    items.erase(items.begin() + at);
    // Before the move below: argument evaluation order is unspecified.
    const int next = std::min(at, static_cast<int>(items.size()) - 1);
    m_history.change(std::move(items), next, DeleteShape);
    return true;
}

bool ShapesEdition::takeFromLine(u16v lineText)
{
    const auto shape = lastDrawing(lineText);
    if (!shape)
        return false;
    ShapePreset p = current();
    p.shape = *shape;
    edit(std::move(p), TakeFromLine);
    return true;
}

void ShapesEdition::replaceNamed(u16v name)
{
    std::vector<ShapePreset> items = presets();
    int at = selection();
    if (at < 0 || at >= static_cast<int>(items.size()) || name.empty())
        return;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (static_cast<int>(i) != at && sameNameIgnoringCase(items[i].name, name)) {
            items.erase(items.begin() + static_cast<std::ptrdiff_t>(i));
            if (static_cast<int>(i) < at)
                --at;
            break;
        }
    }
    Renames renamed{{items[static_cast<std::size_t>(at)].name, u16(name)}};
    items[static_cast<std::size_t>(at)].name = u16(name);
    m_history.change(std::move(items), at, ReplaceShape, {}, std::move(renamed));
}

void ShapesEdition::restoreDefaults(std::vector<ShapePreset> defaults)
{
    const u16 name = current().name;
    int choice = selection() < static_cast<int>(defaults.size()) ? selection() : 0;
    if (!defaults.empty() && defaults[static_cast<std::size_t>(choice)].name != name) {
        choice = 0;
        for (std::size_t i = 0; i < defaults.size(); ++i)
            if (defaults[i].name == name) {
                choice = static_cast<int>(i);
                break;
            }
    }
    if (defaults == presets())
        return;
    m_history.change(std::move(defaults), choice, RestoreDefaults);
}

}
