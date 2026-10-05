#include "hikari/application/shape_presets.h"

#include "hikari/application/visual_drawing.h"

#include <algorithm>
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

// wxString::ToLong(&value) (base 10, wcstol): leading blanks and a sign,
// then digits to the end; false for nothing read, anything left over or a
// value out of `long`'s range (32 bits on Windows, 64 on Linux, as each
// legacy build's long).
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

} // namespace

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
    // LoadSettings (VisualDrawingShapes.cpp:299-303).
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
        // GetNextToken past the end gives "".
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

// ---------------------------------------------------------------------------
// ShapesEdition (VisualDrawingShapes.cpp:43-288).

ShapesEdition::ShapesEdition(std::vector<ShapePreset> presets, int curShape) : m_presets(std::move(presets))
{
    if (curShape < 0 || curShape >= static_cast<int>(m_presets.size()))
        curShape = 0;
    m_selection = curShape;
    for (const ShapePreset &p : m_presets)
        m_list.push_back(p.name);
    // Legacy indexed the empty list here (a crash); the fields stay empty.
    if (!m_presets.empty())
        m_current = m_presets[static_cast<std::size_t>(m_selection)];
    else
        m_selection = -1;
    showCurrent();
}

void ShapesEdition::showCurrent()
{
    // SetShapeFromSettings through HikariTextCtrl::SetValue (HikariTextCtrl.
    // cpp:160-168): "\r" dropped, and "\n" in the one-line name field. The
    // name's 20-character limit applies to typing only.
    const auto shown = [](u16 text, bool multiline) {
        std::erase(text, u'\r');
        if (!multiline)
            std::erase(text, u'\n');
        return text;
    };
    name = shown(m_current.name, false);
    shape = shown(m_current.shape, true);
    mode = m_current.mode;
    scalingMode = m_current.scalingMode;
}

std::optional<ShapesEdition::Message> ShapesEdition::addShape(u16v newName)
{
    if (newName.empty())
        return Message{u"Enter a name for the new shape.", u"Error"};
    for (const u16 &listed : m_list)
        if (sameNameIgnoringCase(listed, newName)) // HikariChoice::FindString
            return Message{u"New shape name already exists, enter another name.", u"Error"};
    m_current = ShapePreset{};
    m_current.name = u16(newName);
    m_presets.push_back(m_current);
    m_list.push_back(m_current.name);
    m_selection = static_cast<int>(m_presets.size()) - 1;
    showCurrent();
    return std::nullopt;
}

std::optional<ShapesEdition::Message> ShapesEdition::removeShape()
{
    if (m_selection < 0 || m_selection >= static_cast<int>(m_presets.size()))
        return Message{u"Selected shape is out of range of shapeList.", u"Error"};
    if (m_presets.size() <= 1)
        return Message{u"Cannot remove all shapes from the list", u"Error"};
    m_presets.erase(m_presets.begin() + m_selection);
    m_list.erase(m_list.begin() + m_selection);
    if (m_selection >= static_cast<int>(m_list.size()))
        m_selection = static_cast<int>(m_list.size()) - 1;
    if (m_selection < 0)
        m_current = ShapePreset{};
    else
        m_current = m_presets[static_cast<std::size_t>(m_selection)];
    showCurrent();
    return std::nullopt;
}

bool ShapesEdition::modified() const
{
    return m_current.name != name || m_current.shape != shape || m_current.mode != mode ||
           m_current.scalingMode != scalingMode;
}

ShapesEdition::Message ShapesEdition::saveChangesQuestion() const
{
    // OnListChanged formats the current preset's shape into the question.
    return {u"Save changes to shape \"" + m_current.shape + u"\"?", u"Confirmation"};
}

void ShapesEdition::select(int num)
{
    if (num < 0 || num >= static_cast<int>(m_presets.size()))
        num = 0;
    if (m_presets.empty())
        return;
    m_current = m_presets[static_cast<std::size_t>(num)];
    m_selection = num;
    showCurrent();
}

bool ShapesEdition::getShapeFromLine(u16v lineText)
{
    bool found = false;
    for (const drawing::Tag &tag : drawing::parseTags(lineText, {u"p"})) {
        if (tag.name != u"pvector")
            continue;
        found = true;
        // If a shape exists then create a new one.
        if (!m_current.shape.empty()) {
            m_current = ShapePreset{};
            m_current.name = u"Untitled";
            m_current.shape = tag.value;
            name = m_current.name;
            m_presets.push_back(m_current);
            m_list.push_back(m_current.name);
            m_selection = static_cast<int>(m_presets.size()) - 1;
        } else {
            m_current.shape = tag.value;
        }
        shape = tag.value;
    }
    return found;
}

ShapesEdition::SaveResult ShapesEdition::save()
{
    SaveResult result;
    // UpdateShape: `current` takes the fields.
    const ShapePreset previous = m_current;
    m_current.name = name;
    m_current.shape = shape;
    m_current.mode = mode;
    m_current.scalingMode = scalingMode;
    if (m_current.shape.empty()) {
        result.error = Message{u"Field \"shape\" cannot be empty.", u"Error"};
        return result;
    }
    if (m_current.name.empty()) {
        m_current.name = u"Untitled";
        name = m_current.name;
    }
    if (m_selection < 0 || m_selection >= static_cast<int>(m_presets.size())) {
        result.error = Message{u"Selected shape is out of range of shapeList.", u"Error"};
        return result;
    }
    // Accepted on #55: another preset of that name is replaced or this one
    // renamed (legacy kept both).
    for (std::size_t i = 0; i < m_presets.size(); ++i) {
        if (static_cast<int>(i) != m_selection && sameNameIgnoringCase(m_presets[i].name, m_current.name)) {
            // Nothing is kept until the answer: the edit stays unsaved.
            m_pending = m_current;
            m_current = previous;
            result.clash = m_presets[i].name;
            return result;
        }
    }
    m_presets[static_cast<std::size_t>(m_selection)] = m_current;
    return result;
}

int ShapesEdition::replaceClash(int *pending)
{
    if (!m_pending)
        return m_selection;
    m_current = *m_pending;
    m_pending.reset();
    for (std::size_t i = 0; i < m_presets.size(); ++i) {
        if (static_cast<int>(i) == m_selection || !sameNameIgnoringCase(m_presets[i].name, m_current.name))
            continue;
        const int removed = static_cast<int>(i);
        m_presets.erase(m_presets.begin() + removed);
        m_list.erase(m_list.begin() + removed);
        if (removed < m_selection)
            --m_selection;
        if (pending) {
            if (*pending == removed)
                *pending = -1;
            else if (*pending > removed)
                --*pending;
        }
        break;
    }
    if (m_selection >= 0 && m_selection < static_cast<int>(m_presets.size())) {
        m_presets[static_cast<std::size_t>(m_selection)] = m_current;
        // The replaced name is the one listed now.
        m_list[static_cast<std::size_t>(m_selection)] = m_current.name;
    }
    return m_selection;
}

void ShapesEdition::restoreDefaults(std::vector<ShapePreset> defaults)
{
    // OnResetDefault (VisualDrawingShapes.cpp:126-139) after the question
    // and the file's removal: LoadSettings, then HikariChoice::PutArray
    // (ListControls.cpp:498-520) keeps the selected name's place when it
    // is listed, else the first; SetShape shows it.
    const u16 ce = (m_selection >= 0 && m_selection < static_cast<int>(m_list.size()))
                       ? m_list[static_cast<std::size_t>(m_selection)]
                       : u16();
    m_presets = std::move(defaults);
    m_list.clear();
    for (const ShapePreset &p : m_presets)
        m_list.push_back(p.name);
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
    select(choice);
}

ShapesEdition::Message ShapesEdition::restoreQuestion()
{
    return {u"Are you sure you want to reset to default?", u"Confirmation"};
}

} // namespace hikari::application::visual
