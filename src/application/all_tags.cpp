#include "hikari/application/all_tags.h"

#include "hikari/application/shape_presets.h"
#include "hikari/application/visual_transform.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <cstdio>
#include <cstdlib>

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

u16 ascii(const std::string &s)
{
    return u16(s.begin(), s.end());
}

u16 streamed(float value)
{
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%f", static_cast<double>(value));
    return ascii(buffer);
}

} // namespace

AllTagsSetting AllTagsSetting::named(std::u16string name)
{
    AllTagsSetting s;
    s.tag = name;
    s.name = std::move(name);
    s.step = 1.f;
    s.rangeMax = 100.f;
    return s;
}

std::u16string_view defaultAllTagsText()
{
    return u"HYDRA2.0\n"
           u"Tag: blur, blur,       0, 100,  0,  0.5,  1, 0, 1\n"
           u"Tag: border, bord,     0, 50,   0,  1,    1, 0, 0\n"
           u"Tag: blur edge, be,    0, 100,  0,  1,    1, 0, 1\n"
           u"Tag: alpha, alpha,     0, 255,  0,  1,    0, 0, 1\n"
           u"Tag: 1a, 1a,           0, 255,  0,  1,    0, 0, 1\n"
           u"Tag: 3a, 3a,           0, 255,  0,  1,    0, 0, 1\n"
           u"Tag: 4a, 4a,           0, 255,  0,  1,    0, 0, 1\n"
           u"Tag: color1, 1c,       0, 255,  0,  1,    0, 0, 1, 0, 0\n"
           u"Tag: color3, 3c,       0, 255,  0,  1,    0, 0, 1, 0, 0\n"
           u"Tag: color4, 4c,       0, 255,  0,  1,    0, 0, 1, 0, 0\n"
           u"Tag: fading, fad,      0, 2000, 0,  5,    0, 1, 1, 0\n"
           u"Tag: fax, fax,       -10, 10,   0,  0.01, 3, 0, 0\n"
           u"Tag: fay, fay,       -10, 10,   0,  0.01, 3, 0, 0\n"
           u"Tag: font size, fs,   20, 300,  70, 1,    1, 0, 0\n"
           u"Tag: spacing, fsp,  -100, 100,  0,  1,    1, 0, 1\n"
           u"Tag: shadow, shad,     0, 80,   0,  1,    1, 0, 0\n"
           u"Tag: xborder, xbord,   0, 80,   0,  1,    1, 0, 0\n"
           u"Tag: yborder, ybord,   0, 80,   0,  1,    1, 0, 0\n"
           u"Tag: xshadow, xshad, -80, 80,   0,  1,    1, 0, 0\n"
           u"Tag: yshadow, yshad, -80, 80,   0,  1,    1, 0, 0\n"
           u"Tag: position, pos,    0, 100,  0,  0.1,  3, 1, 2, 0\n"
           u"Tag: tanimation, t,    0, 9999, 0,  10,   0, 1, 1, 0\n";
}

std::vector<AllTagsSetting> defaultAllTags()
{
    return parseAllTags(defaultAllTagsText());
}

std::vector<AllTagsSetting> parseAllTags(std::u16string_view text, bool *writeDefaults)
{
    u16v settings = text;
    if (writeDefaults)
        *writeDefaults = false;
    if (!settings.starts_with(u"HYDRA2.0")) {
        if (writeDefaults)
            *writeDefaults = !settings.empty();
        settings = defaultAllTagsText();
    }
    std::vector<AllTagsSetting> tags;
    const std::vector<u16> lines = strtok(settings, u'\n');
    // Skip the version line.
    for (std::size_t l = 1; l < lines.size(); ++l) {
        const u16 &line = lines[l];
        if (!line.starts_with(u"Tag: "))
            continue;
        const std::vector<u16> fields = strtok(u16v(line).substr(5), u',');
        std::size_t f = 0;
        const auto more = [&] { return f < fields.size(); };
        const auto number = [&](float &out) {
            double value = 0;
            if (!transform::toDouble(trimStart(fields[f++]), value))
                return false;
            out = static_cast<float>(value);
            return true;
        };
        AllTagsSetting tmp;
        if (!more())
            continue;
        tmp.name = fields[f++];
        if (!more())
            continue;
        tmp.tag = trimStart(fields[f++]);
        if (!more() || !number(tmp.rangeMin))
            continue;
        if (!more() || !number(tmp.rangeMax))
            continue;
        if (!more() || !number(tmp.values[0]))
            continue;
        if (!more() || !number(tmp.step))
            continue;
        if (!more())
            continue;
        tmp.digitsAfterDot = static_cast<unsigned char>(transform::atoi(trimStart(fields[f++])));
        if (!more())
            continue;
        tmp.mode = static_cast<unsigned char>(transform::atoi(trimStart(fields[f++])));
        if (!more())
            continue;
        tmp.tagMode = transform::atoi(trimStart(fields[f++]));
        // Values past the fourth, which HikariSub legacy files can hold, are dropped.
        for (int i = 1; more(); i++) {
            float value = 0;
            if (number(value) && i < 4) {
                tmp.values[static_cast<std::size_t>(i)] = value;
                tmp.numOfValues = static_cast<unsigned char>(i + 1);
            }
        }
        tags.push_back(std::move(tmp));
    }
    return tags;
}

std::u16string writeAllTags(const std::vector<AllTagsSetting> &tags)
{
    u16 out;
    for (std::size_t i = 0; i < tags.size(); i++) {
        const AllTagsSetting &tag = tags[i];
        out += i == 0 ? u"HYDRA2.0\nTag: " : u"Tag: ";
        out += tag.name + u", " + tag.tag + u", " + streamed(tag.rangeMin) + u", " + streamed(tag.rangeMax) + u", " +
               streamed(tag.values[0]) + u", " + streamed(tag.step) + u", " +
               ascii(std::to_string(static_cast<int>(tag.digitsAfterDot))) + u", " +
               ascii(std::to_string(static_cast<int>(tag.mode))) + u", " + ascii(std::to_string(tag.tagMode));
        if (tag.numOfValues > 1)
            for (int v = 1; v < tag.numOfValues && v < 4; v++)
                out += u", " + streamed(tag.values[static_cast<std::size_t>(v)]);
        out += u"\n";
    }
    return out;
}

std::vector<std::u16string> allTagsNames(const std::vector<AllTagsSetting> &tags)
{
    std::vector<std::u16string> names;
    for (const AllTagsSetting &t : tags)
        names.push_back(t.name);
    return names;
}

std::u16string numCtrlText(double value)
{
    char buffer[400];
    std::snprintf(buffer, sizeof buffer, "%f", value);
    std::string s(buffer);
    std::replace(s.begin(), s.end(), ',', '.');
    std::size_t rmv = 0;
    for (std::size_t i = s.size() - 1; i > 0; i--) {
        if (s[i] == '0')
            rmv++;
        else if (s[i] == '.') {
            rmv++;
            break;
        } else
            break;
    }
    s.resize(s.size() - rmv);
    return ascii(s);
}

NumberField::NumberField(double value, double rangeFrom, double rangeTo, bool intOnly)
    : m_from(rangeFrom), m_to(rangeTo), m_intOnly(intOnly)
{
    if (m_to < m_from)
        std::swap(m_from, m_to);
    setDouble(value);
}

void NumberField::setDouble(double value)
{
    if (value > m_to)
        value = m_to;
    else if (value < m_from)
        value = m_from;
    m_value = value;
    m_oldval = numCtrlText(value);
    m_text = m_oldval;
}

void NumberField::setInt(int value)
{
    if (value > static_cast<int>(m_to))
        value = static_cast<int>(m_to);
    else if (value < static_cast<int>(m_from))
        value = static_cast<int>(m_from);
    m_value = value;
    m_oldval = ascii(std::to_string(value));
    m_text = m_oldval;
}

void NumberField::setText(std::u16string text)
{
    m_text = std::move(text);
    u16 val = m_text;
    std::replace(val.begin(), val.end(), u',', u'.');
    const auto points = std::count(val.begin(), val.end(), u'.');
    double value = 0;
    if (val == u"-" || val.empty()) {
    } else if (val.ends_with(u'.') || val.starts_with(u'.')) {
        if (points > 1)
            m_text = m_oldval;
    } else if (!transform::toDouble(val, value) || value > m_to || value < m_from) {
    } else {
        m_value = value;
        m_oldval = val;
    }
}

double NumberField::getDouble() const
{
    u16 val = m_text;
    std::replace(val.begin(), val.end(), u',', u'.');
    if (val == u"-")
        val = m_oldval;
    if (val.starts_with(u'.'))
        val.insert(val.begin(), u'0');
    if (val.ends_with(u'.'))
        val += u'0';
    double value = m_value;
    if (!transform::toDouble(val, value)) {
        val = m_oldval;
        (void)transform::toDouble(val, value);
    }
    if (value > m_to)
        value = m_to;
    if (value < m_from)
        value = m_from;
    m_value = value;
    return value;
}

std::vector<int> allTagsSections(const std::vector<AllTagsSetting> &tags)
{
    static const std::vector<u16> common{u"blur",    u"blur edge", u"border", u"shadow", u"font size",
                                         u"spacing", u"alpha",     u"color1", u"color3"};
    static const std::vector<u16> builtIn = allTagsNames(defaultAllTags());
    std::vector<int> out;
    out.reserve(tags.size());
    for (const AllTagsSetting &tag : tags)
        out.push_back(std::ranges::contains(common, tag.name)    ? 0
                      : std::ranges::contains(builtIn, tag.name) ? 1
                                                                 : 2);
    return out;
}

namespace {

bool isColour(u16v tag)
{
    return tag == u"1c" || tag == u"2c" || tag == u"3c" || tag == u"4c" || tag == u"c";
}

bool isAlpha(u16v tag)
{
    return tag == u"1a" || tag == u"2a" || tag == u"3a" || tag == u"4a" || tag == u"alpha";
}

u16 hexByte(float value)
{
    const int v = std::clamp(static_cast<int>(value + 0.5f), 0, 255);
    static constexpr char16_t digits[] = u"0123456789ABCDEF";
    return {digits[v >> 4], digits[v & 15]};
}

} // namespace

std::vector<u16> allTagsValueNames(const AllTagsSetting &tag)
{
    const int n = std::clamp(static_cast<int>(tag.numOfValues), 1, 4);
    if (n == 1)
        return {u"Value"};
    std::vector<u16> names;
    if (isColour(tag.tag))
        names = {u"R", u"G", u"B"};
    else if (tag.tag == u"pos")
        names = {u"X", u"Y"};
    else if (tag.tag == u"fad")
        names = {u"In", u"Out"};
    else if (tag.tag == u"t")
        names = {u"Start", u"End"};
    else
        names = {u"1", u"2", u"3", u"4"};
    std::vector<u16> out;
    for (int i = 0; i < n; ++i)
        out.push_back(names[static_cast<std::size_t>(std::min<int>(i, static_cast<int>(names.size()) - 1))]);
    return out;
}

u16 allTagsSetText(const AllTagsSetting &tag)
{
    const std::string format = "5." + std::to_string(static_cast<int>(tag.digitsAfterDot)) + "f";
    const auto &v = tag.values;
    u16 value;
    if (isAlpha(tag.tag)) {
        value = u"&H" + hexByte(v[0]) + u"&";
    } else if (isColour(tag.tag)) {
        value = u"&H" + hexByte(v[2]) + hexByte(v[1]) + hexByte(v[0]) + u"&";
    } else if (tag.numOfValues > 1) {
        value = u"(";
        for (int i = 0; i < std::min<int>(tag.numOfValues, 4); ++i)
            value += (i ? u"," : u"") + transform::getfloat(v[static_cast<std::size_t>(i)], format);
        if (tag.tag == u"t")
            value += u",";
        value += u")";
    } else {
        value = transform::getfloat(v[0], format);
    }
    return u"\\" + tag.tag + value;
}

namespace {

int clampedSelection(int selection, std::size_t size)
{
    return selection < 0 || selection >= static_cast<int>(size) ? 0 : selection;
}

bool blank(u16v text)
{
    return std::ranges::all_of(text, isBlank);
}

std::optional<double> readNumber(u16v text)
{
    u16 t = trimStart(text);
    while (!t.empty() && isBlank(t.back()))
        t.pop_back();
    std::ranges::replace(t, u',', u'.');
    double value = 0;
    if (t.empty() || !transform::toDouble(t, value) || !std::isfinite(value))
        return std::nullopt;
    return value;
}

AllTagsEdition::Check checkRange(const AllTagsSetting &t, AllTagsEdition::Number edited)
{
    using Check = AllTagsEdition::Check;
    using Number = AllTagsEdition::Number;
    if (t.rangeMax <= t.rangeMin)
        return edited == Number::Min ? Check::MinNotBelowMax : Check::MaxNotAboveMin;
    if (t.step <= 0)
        return Check::StepNotPositive;
    if ((t.rangeMax - t.rangeMin) / t.step < 2)
        return Check::StepTooLarge;
    return Check::Ok;
}

} // namespace

AllTagsEdition::AllTagsEdition(std::vector<AllTagsSetting> tags, int selection)
    : m_history(tags, clampedSelection(selection, tags.size()))
{
}

std::vector<AllTagsSetting> AllTagsEdition::usable() const
{
    std::vector<AllTagsSetting> out;
    for (const AllTagsSetting &t : tags())
        if (!t.tag.empty())
            out.push_back(t);
    return out;
}

const AllTagsSetting &AllTagsEdition::current() const
{
    static const AllTagsSetting none;
    const int i = selection();
    return i >= 0 && i < static_cast<int>(tags().size()) ? tags()[static_cast<std::size_t>(i)] : none;
}

void AllTagsEdition::select(int index)
{
    if (index >= 0 && index < static_cast<int>(tags().size()))
        m_history.select(index);
}

void AllTagsEdition::edit(AllTagsSetting tag, Step step, std::string mergeKey, Renames renamed)
{
    const int i = selection();
    if (i < 0 || i >= static_cast<int>(tags().size()) || tags()[static_cast<std::size_t>(i)] == tag)
        return;
    std::vector<AllTagsSetting> items = tags();
    items[static_cast<std::size_t>(i)] = std::move(tag);
    if (!mergeKey.empty())
        mergeKey += ':' + std::to_string(i);
    m_history.change(std::move(items), i, step, std::move(mergeKey), std::move(renamed));
}

AllTagsEdition::Check AllTagsEdition::setName(u16v name, u16 *taken)
{
    if (name.empty() || blank(name))
        return Check::Empty;
    if (name.find_first_of(u",\r\n") != u16v::npos)
        return Check::Separator;
    for (std::size_t i = 0; i < tags().size(); ++i) {
        if (static_cast<int>(i) != selection() && sameNameIgnoringCase(tags()[i].name, name)) {
            if (taken)
                *taken = tags()[i].name;
            return replacesLastUsable(i) ? Check::TakenLastUsable : Check::Taken;
        }
    }
    AllTagsSetting t = current();
    Renames renamed{{t.name, u16(name)}};
    t.name = u16(name);
    edit(std::move(t), Rename, "name", std::move(renamed));
    return Check::Ok;
}

AllTagsEdition::Check AllTagsEdition::setTag(u16v tag)
{
    while (!tag.empty() && (tag.front() == u'\\' || isBlank(tag.front())))
        tag.remove_prefix(1);
    while (!tag.empty() && isBlank(tag.back()))
        tag.remove_suffix(1);
    if (tag.empty())
        return Check::Empty;
    if (tag.find_first_of(u",\r\n") != u16v::npos)
        return Check::Separator;
    AllTagsSetting t = current();
    t.tag = u16(tag);
    edit(std::move(t), EditTag, "tag");
    return Check::Ok;
}

AllTagsEdition::Check AllTagsEdition::setNumber(Number field, u16v text, float *limit)
{
    const auto value = readNumber(text);
    if (!value)
        return Check::NotANumber;
    AllTagsSetting t = current();
    if (field == Number::Digits) {
        if (*value != std::floor(*value) || *value < 0 || *value > kMaxDigits)
            return Check::OutOfRange;
        t.digitsAfterDot = static_cast<unsigned char>(*value);
        edit(std::move(t), EditDecimals, "digits");
        return Check::Ok;
    }
    if (std::abs(*value) > kNumberLimit)
        return Check::OutOfRange;
    const auto v = static_cast<float>(*value);
    switch (field) {
    case Number::Min:
    case Number::Max:
    case Number::Step: {
        (field == Number::Min ? t.rangeMin : field == Number::Max ? t.rangeMax : t.step) = v;
        if (const Check c = checkRange(t, field); c != Check::Ok) {
            if (limit)
                *limit = c == Check::MaxNotAboveMin ? t.rangeMin
                       : c == Check::MinNotBelowMax ? t.rangeMax
                                                    : (t.rangeMax - t.rangeMin) / 2;
            return c;
        }
        const char *key = field == Number::Min ? "min" : field == Number::Max ? "max" : "step";
        edit(std::move(t), field == Number::Step ? EditStep : EditRange, key);
        return Check::Ok;
    }
    default: {
        const auto at = static_cast<std::size_t>(static_cast<int>(field) - static_cast<int>(Number::Value0));
        t.values[at] = v;
        edit(std::move(t), EditValue, "value" + std::to_string(at));
        return Check::Ok;
    }
    }
}

void AllTagsEdition::setCount(int count)
{
    if (count < 1 || count > 4)
        return;
    AllTagsSetting t = current();
    t.numOfValues = static_cast<unsigned char>(count);
    edit(std::move(t), EditCount);
}

void AllTagsEdition::setChangeOption(int option)
{
    if (option < PasteAdd || option > PasteGradientLineDecrease)
        return;
    AllTagsSetting t = current();
    t.tagMode = option;
    edit(std::move(t), ChangeOption);
}

void AllTagsEdition::setPlacing(int placing)
{
    if (placing < 0 || placing > 2)
        return;
    AllTagsSetting t = current();
    t.mode = static_cast<unsigned char>(placing);
    edit(std::move(t), ChangePlacing);
}

u16 AllTagsEdition::freeName(u16 base, bool copy) const
{
    const auto taken = [this](u16v name) {
        return std::ranges::any_of(tags(), [&](const AllTagsSetting &t) { return sameNameIgnoringCase(t.name, name); });
    };
    for (int n = 1;; ++n) {
        u16 name = base + (copy ? u16(u" copy") : u16());
        if (n > 1)
            name += u" " + ascii(std::to_string(n));
        if (!taken(name))
            return name;
    }
}

void AllTagsEdition::addTag()
{
    AllTagsSetting t = AllTagsSetting::named(freeName(u"New tag", false));
    t.tag.clear();
    std::vector<AllTagsSetting> items = tags();
    items.push_back(std::move(t));
    const int at = static_cast<int>(items.size()) - 1;
    m_history.change(std::move(items), at, NewTag);
}

void AllTagsEdition::duplicate()
{
    if (tags().empty())
        return;
    AllTagsSetting t = current();
    t.name = freeName(t.name, true);
    std::vector<AllTagsSetting> items = tags();
    const int at = selection() + 1;
    items.insert(items.begin() + at, std::move(t));
    m_history.change(std::move(items), at, DuplicateTag);
}

bool AllTagsEdition::canRemove() const
{
    if (tags().size() <= 1)
        return false;
    return current().tag.empty() || usable().size() > 1;
}

bool AllTagsEdition::remove()
{
    if (!canRemove())
        return false;
    std::vector<AllTagsSetting> items = tags();
    const int at = selection();
    items.erase(items.begin() + at);
    // Before the move below: argument evaluation order is unspecified.
    const int next = std::min(at, static_cast<int>(items.size()) - 1);
    m_history.change(std::move(items), next, DeleteTag);
    return true;
}

bool AllTagsEdition::replacesLastUsable(std::size_t other) const
{
    return !tags()[other].tag.empty() && current().tag.empty() && usable().size() <= 1;
}

void AllTagsEdition::replaceNamed(u16v name)
{
    std::vector<AllTagsSetting> items = tags();
    int at = selection();
    if (at < 0 || at >= static_cast<int>(items.size()) || name.empty())
        return;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (static_cast<int>(i) != at && sameNameIgnoringCase(items[i].name, name)) {
            if (replacesLastUsable(i))
                return;
            items.erase(items.begin() + static_cast<std::ptrdiff_t>(i));
            if (static_cast<int>(i) < at)
                --at;
            break;
        }
    }
    Renames renamed{{items[static_cast<std::size_t>(at)].name, u16(name)}};
    items[static_cast<std::size_t>(at)].name = u16(name);
    m_history.change(std::move(items), at, ReplaceTag, {}, std::move(renamed));
}

void AllTagsEdition::restoreDefaults()
{
    std::vector<AllTagsSetting> defaults = defaultAllTags();
    if (defaults == tags())
        return;
    const u16 name = current().name;
    int choice = selection() < static_cast<int>(defaults.size()) ? selection() : 0;
    if (defaults[static_cast<std::size_t>(choice)].name != name) {
        choice = 0;
        for (std::size_t i = 0; i < defaults.size(); ++i)
            if (defaults[i].name == name) {
                choice = static_cast<int>(i);
                break;
            }
    }
    m_history.change(std::move(defaults), choice, RestoreDefaults);
}

const std::array<std::u16string_view, 8> &allTagsChangeOptions()
{
    static const std::array<std::u16string_view, 8> list{u"Add",
                                                         u"Insert",
                                                         u"Multiply",
                                                         u"Multiply+",
                                                         u"Gradient text increasing",
                                                         u"Gradient text decreasing",
                                                         u"Gradient line increasing",
                                                         u"Gradient line decreasing"};
    return list;
}

std::u16string_view allTagsChangeOptionsHelp()
{
    return u"Tag change options:\nAdd - changes all tags by adding the slider value.\n"
           u"Insert - inserts the tag at the cursor position for one line,\n"
           u"or at the start for multiple lines.\n"
           u"Multiply - multiplies the slider value by the selected line number;\n"
           u"the first line is not changed.\n"
           u"Gradient text increasing - inserts a tag at each character, increasing the value.\n"
           u"Gradient text decreasing - inserts a tag at each character, decreasing the value.\n"
           u"Gradient line increasing - inserts a tag in selected lines, increasing the value.\n"
           u"Gradient line decreasing - inserts a tag in selected lines, decreasing the value.";
}

} // namespace hikari::application::visual
