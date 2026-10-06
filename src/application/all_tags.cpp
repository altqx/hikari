#include "hikari/application/all_tags.h"

#include "hikari/application/shape_presets.h"
#include "hikari/application/visual_transform.h"

#include <algorithm>
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

// wxStringTokenizer(text, delimiters, wxTOKEN_STRTOK): the non-empty runs
// between delimiters.
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

// wxString::Trim(false): blanks off the start.
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

// wxString << float (wx 3.2: Format("%f")).
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
    // VisualAllTagsEdition.cpp:467-489, unchanged.
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
    // LoadSettings (VisualAllTagsEdition.cpp:457-547).
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
    // The first line (the version) is skipped without another look.
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
        // Legacy wrote a fifth and later value past the four it holds; they
        // are not read here.
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
    // SaveSettings (VisualAllTagsEdition.cpp:557-575).
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
    // getdouble (NumCtrl.cpp:22-35).
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
    // NumCtrl::OnNumWrite (NumCtrl.cpp): a text that reads as a number in
    // range is the one to fall back to; a second point after or before is
    // refused (the old text comes back).
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
    // NumCtrl::GetString / GetDouble (NumCtrl.cpp).
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

AllTagsEdition::AllTagsEdition(std::vector<AllTagsSetting> tags, int curTag)
    : m_tags(std::move(tags))
{
    // AllTagsEdition::AllTagsEdition (VisualAllTagsEdition.cpp:155-271).
    if (curTag < 0 || curTag >= static_cast<int>(m_tags.size()))
        curTag = 0;
    m_selection = curTag;
    m_current = m_tags[static_cast<std::size_t>(m_selection)];
    m_list = allTagsNames(m_tags);
    name = m_current.name;
    tag = m_current.tag;
    minValue.setDouble(m_current.rangeMin);
    maxValue.setDouble(m_current.rangeMax);
    values[0].setDouble(m_current.values[0]);
    step.setDouble(m_current.step);
    placing = m_current.mode;
    digitsAfterDot.setInt(m_current.digitsAfterDot);
    additionalValues = m_current.numOfValues - 1;
    changeOption = m_current.tagMode;
    for (std::size_t i = 1; i < 4; i++)
        values[i].setDouble(m_current.values[i]);
}

std::optional<AllTagsEdition::Message> AllTagsEdition::addTag()
{
    // OnAddTag (VisualAllTagsEdition.cpp:293-310).
    if (newTagName.empty())
        return Message{u"Enter a name for the new tag.", u"Error"};
    for (const u16 &listed : m_list)
        if (sameNameIgnoringCase(listed, newTagName)) // HikariChoice::FindString
            return Message{u"New tag already exists, enter another name.", u"Error"};
    m_current = AllTagsSetting::named(newTagName);
    m_tags.push_back(m_current);
    m_list.push_back(m_current.name);
    m_selection = static_cast<int>(m_tags.size()) - 1;
    showCurrent();
    return std::nullopt;
}

std::optional<AllTagsEdition::Message> AllTagsEdition::removeTag()
{
    // OnRemoveTag (VisualAllTagsEdition.cpp:312-339).
    if (m_selection < 0 || m_selection >= static_cast<int>(m_tags.size()))
        return Message{u"Selected tag is out of range of tagList.", u"Error"};
    if (m_tags.size() <= 1)
        return Message{u"Cannot remove all tags from list", u"Error"};
    m_tags.erase(m_tags.begin() + m_selection);
    m_list.erase(m_list.begin() + m_selection);
    if (m_selection >= static_cast<int>(m_list.size()))
        m_selection = static_cast<int>(m_list.size()) - 1;
    m_current = m_selection < 0 ? AllTagsSetting() : m_tags[static_cast<std::size_t>(m_selection)];
    showCurrent();
    return std::nullopt;
}

bool AllTagsEdition::modified() const
{
    // CheckModified (VisualAllTagsEdition.cpp:400-420).
    if (m_current.name != name || m_current.tag != tag ||
        m_current.rangeMin != static_cast<float>(minValue.getDouble()) ||
        m_current.rangeMax != static_cast<float>(maxValue.getDouble()) ||
        m_current.step != static_cast<float>(step.getDouble()) || m_current.mode != placing ||
        m_current.digitsAfterDot != digitsAfterDot.getInt() || m_current.numOfValues != additionalValues + 1 ||
        m_current.tagMode != changeOption)
        return true;
    for (int i = 0; i < m_current.numOfValues && i < 4; i++)
        if (m_current.values[static_cast<std::size_t>(i)] != static_cast<float>(values[static_cast<std::size_t>(i)].getDouble()))
            return true;
    return false;
}

AllTagsEdition::Message AllTagsEdition::saveChangesQuestion() const
{
    return {u"Save changes to tag \"" + m_current.tag + u"\"?", u"Confirmation"};
}

void AllTagsEdition::select(int index)
{
    setTag(index);
}

void AllTagsEdition::setTag(int num)
{
    // SetTag (VisualAllTagsEdition.cpp:389-398).
    if (num < 0 || num >= static_cast<int>(m_tags.size()))
        num = 0;
    m_current = m_tags[static_cast<std::size_t>(num)];
    m_selection = num;
    showCurrent();
}

void AllTagsEdition::updateTag()
{
    // UpdateTag (VisualAllTagsEdition.cpp:352-368).
    m_current.name = name;
    m_current.tag = tag;
    m_current.rangeMin = static_cast<float>(minValue.getDouble());
    m_current.rangeMax = static_cast<float>(maxValue.getDouble());
    m_current.step = static_cast<float>(step.getDouble());
    m_current.mode = static_cast<unsigned char>(placing);
    m_current.digitsAfterDot = static_cast<unsigned char>(digitsAfterDot.getInt());
    m_current.numOfValues = static_cast<unsigned char>(additionalValues + 1);
    m_current.tagMode = changeOption;
    for (int i = 0; i < m_current.numOfValues && i < 4; i++)
        m_current.values[static_cast<std::size_t>(i)] = static_cast<float>(values[static_cast<std::size_t>(i)].getDouble());
}

void AllTagsEdition::showCurrent()
{
    // SetTagFromSettings (VisualAllTagsEdition.cpp:370-387): the values past
    // the count keep what the fields showed.
    name = m_current.name;
    tag = m_current.tag;
    minValue.setDouble(m_current.rangeMin);
    maxValue.setDouble(m_current.rangeMax);
    step.setDouble(m_current.step);
    placing = m_current.mode;
    digitsAfterDot.setInt(m_current.digitsAfterDot);
    additionalValues = m_current.numOfValues - 1;
    changeOption = m_current.tagMode;
    for (int i = 0; i < m_current.numOfValues && i < 4; i++)
        values[static_cast<std::size_t>(i)].setDouble(m_current.values[static_cast<std::size_t>(i)]);
}

std::optional<AllTagsEdition::Message> AllTagsEdition::save()
{
    // Save (VisualAllTagsEdition.cpp:422-455).
    updateTag();
    if (m_current.tag.empty())
        return Message{u"Field \"Tag\" cannot be empty.", u"Error"};
    if (m_current.name.empty()) {
        m_current.name = m_current.tag;
        name = m_current.name;
    }
    if (m_current.rangeMax <= m_current.rangeMin)
        return Message{u"Field \"Maximum value\" must contain a value\ngreater than field \"Minimum value\".", u"Error"};
    if (m_current.step <= 0)
        return Message{u"Field \"Step\" must contain a value greater than zero.", u"Error"};
    if (((m_current.rangeMax - m_current.rangeMin) / m_current.step) < 2)
        return Message{u"Field \"Step\" contains a number too large for the current min-max range.", u"Error"};
    if (m_selection < 0 || m_selection >= static_cast<int>(m_tags.size()))
        return Message{u"Selected tag is out of range of tagList.", u"Error"};
    m_tags[static_cast<std::size_t>(m_selection)] = m_current;
    // The list takes a rename (T6-dialog-list-stale: legacy's kept the old
    // name).
    m_list[static_cast<std::size_t>(m_selection)] = m_current.name;
    return std::nullopt;
}

void AllTagsEdition::restoreDefaults()
{
    // OnResetDefault (VisualAllTagsEdition.cpp:278-291) after the question
    // and the file's removal: LoadSettings, then HikariChoice::PutArray
    // (ListControls.cpp:498-520) keeps the selected name's place when it is
    // listed, else the first; SetTag shows it.
    const u16 ce = (m_selection >= 0 && m_selection < static_cast<int>(m_list.size()))
                       ? m_list[static_cast<std::size_t>(m_selection)]
                       : u16();
    m_tags = defaultAllTags();
    m_list = allTagsNames(m_tags);
    int choice = m_selection;
    if (m_list.empty()) {
        choice = -1;
    } else if (!ce.empty()) {
        if (choice >= static_cast<int>(m_list.size()))
            choice = 0;
        if (ce != m_list[static_cast<std::size_t>(choice)]) {
            int ichoice = -1;
            for (std::size_t i = 0; i < m_list.size(); ++i)
                if (m_list[i] == ce) { // wxArrayString::Index, case-sensitive
                    ichoice = static_cast<int>(i);
                    break;
                }
            choice = ichoice == -1 ? 0 : ichoice;
        }
    }
    setTag(choice);
}

AllTagsEdition::Message AllTagsEdition::restoreQuestion()
{
    return {u"Are you sure you want to reset to default?", u"Confirmation"};
}

const std::array<std::u16string_view, 3> &AllTagsEdition::placings()
{
    static const std::array<std::u16string_view, 3> list{
        u"Inserted at the cursor position", u"Insert Tag at text beginning of text", u"Clip rectangle with \\t animation"};
    return list;
}

const std::array<std::u16string_view, 4> &AllTagsEdition::valueCounts()
{
    static const std::array<std::u16string_view, 4> list{u"No additional values", u"One additional value",
                                                         u"Two additional values", u"Three additional values"};
    return list;
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
    // AllTagsItem::ShowContols (VideoToolbar.cpp:759-767).
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
