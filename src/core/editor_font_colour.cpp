#include "hikari/core/editor_font_colour.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace hikari::core::legacy {

namespace {

using u16 = std::u16string;
using u16v = std::u16string_view;

u16 ascii(const char *s)
{
    u16 out;
    while (*s)
        out += static_cast<char16_t>(*s++);
    return out;
}

// wxString::ToLong(&value, base): strtol over the text (a partial number counts).
long toLong(u16v text, int base)
{
    std::string s;
    for (const char16_t c : text)
        s += c < 0x80 ? static_cast<char>(c) : '?';
    return std::strtol(s.c_str(), nullptr, base);
}

// wxString::SubString(from, to): inclusive, clamped to the text.
u16 subString(u16v s, std::size_t from, std::size_t to)
{
    if (from >= s.size())
        return {};
    return u16(s.substr(from, to - from + 1));
}

void removeAll(u16 &s, char16_t c)
{
    std::erase(s, c);
}

// wxString::IsNumber: an optional sign and digits only.
bool isNumber(u16v s)
{
    if (s.empty())
        return false;
    std::size_t i = s[0] == u'-' || s[0] == u'+' ? 1 : 0;
    if (i == s.size())
        return false;
    for (; i < s.size(); ++i)
        if (s[i] < u'0' || s[i] > u'9')
            return false;
    return true;
}

// AssColor::SetAlphaString.
int alphaOf(u16 alpha)
{
    removeAll(alpha, u'&');
    removeAll(alpha, u'H');
    return static_cast<int>(toLong(alpha, 16));
}

u16 hex2(int value)
{
    char buf[8];
    std::snprintf(buf, sizeof buf, "%02X", static_cast<unsigned>(value));
    return ascii(buf);
}

u16 number(int n)
{
    return u16(1, static_cast<char16_t>(u'0' + n));
}

u16 colourPattern(int n, bool closing)
{
    return number(n) + (n == 1 ? u"?c&(.*)" : u"c&(.*)") + (closing ? u"&" : u"");
}

} // namespace

FontValues defaultFontValues()
{
    return FontValues{u"Garamond", u"40"};
}

FontValues fontInEffect(const EditorText &state, FontValues style, long *position)
{
    TagEditor editor(state);
    if (editor.findTag(u"b(0|1)", 0, true))
        style.bold = editor.finding() == u"1";
    if (editor.findTag(u"i(0|1)", 0, true))
        style.italic = editor.finding() == u"1";
    if (editor.findTag(u"u(0|1)", 0, true))
        style.underline = editor.finding() == u"1";
    if (editor.findTag(u"s(0|1)", 0, true))
        style.strikeOut = editor.finding() == u"1";
    // GetTextResult leaves the value alone when the finding is empty.
    if (editor.findTag(u"fs([0-9.]+)", 0, true) && !editor.finding().empty())
        style.size = editor.finding();
    if (editor.findTag(u"fn(.*)", 0, true) && !editor.finding().empty())
        style.name = editor.finding();
    if (position)
        *position = editor.position().first;
    return style;
}

std::vector<EditStep> fontSteps(const FontValues &edited, const FontValues &result, const FontValues &actual, bool ass)
{
    std::vector<EditStep> steps;
    const auto flag = [](bool v) { return v ? u16(u"1") : u16(u"0"); };
    if (result.name != edited.name) {
        if (ass)
            steps.push_back({u"fn(.*)", u"\\fn" + result.name, u"\\fn" + actual.name});
        else
            steps.push_back({u"F:" + result.name, u"f:([^}]*)", {}, true});
    }
    if (result.size != edited.size) {
        if (ass)
            steps.push_back({u"fs([0-9]+)", u"\\fs" + result.size, u"\\fs" + actual.size});
        else // legacy writes the font name here
            steps.push_back({u"S:" + result.name, u"s:([^}]*)", {}, true});
    }
    if (result.bold != edited.bold) {
        if (ass)
            steps.push_back({u"b(0|1)", u"\\b" + flag(result.bold), u"\\b" + flag(actual.bold)});
        else
            steps.push_back({u"y:b", result.bold ? u16(u"Y:b") : u16(), {}, true});
    }
    if (result.italic != edited.italic) {
        if (ass)
            steps.push_back({u"i(0|1)", u"\\i" + flag(result.italic), u"\\i" + flag(actual.italic)});
        else
            steps.push_back({u"y:i", result.italic ? u16(u"Y:i") : u16(), {}, true});
    }
    if (result.underline != edited.underline)
        steps.push_back({u"u(0|1)", u"\\u" + flag(result.underline), u"\\u" + flag(actual.underline)});
    if (result.strikeOut != edited.strikeOut)
        steps.push_back({u"s(0|1)", u"\\s" + flag(result.strikeOut), u"\\s" + flag(actual.strikeOut)});
    return steps;
}

StepResult applySteps(EditorText state, const std::vector<EditStep> &steps, NonAssFormat format, long position)
{
    for (const auto &step : steps) {
        if (step.nonAss) {
            state = putInNonAss(std::move(state), format, step.pattern, step.tag);
            continue;
        }
        TagEditor editor(std::move(state));
        editor.setFocus(false);
        editor.findTag(step.pattern, 0, true);
        editor.putTagInText(step.tag, step.reset);
        position = editor.position().first;
        state = editor.state();
    }
    return {std::move(state), position};
}

void applyStepsToLine(u16 &text, u16 &translation, const std::vector<EditStep> &steps, NonAssFormat format)
{
    for (const auto &step : steps) {
        if (step.nonAss) {
            text = putInNonAssLine(std::move(text), format, step.pattern, step.tag);
            continue;
        }
        u16 &field = translation.empty() ? text : translation;
        field = putTagInLine(std::move(field), step.pattern, step.tag);
    }
}

long caretAfterDialog(u16v text, long position)
{
    if (position < 0)
        position = 0;
    const auto at = static_cast<std::size_t>(position);
    if (at < text.size() && text[at] == u'}')
        return position;
    const auto bracket = text.find(u'}', at);
    if (bracket != u16v::npos)
        return static_cast<long>(bracket) + 1;
    return std::min(position, static_cast<long>(text.size()));
}

TagColour parseAssColour(u16v text)
{
    TagColour c;
    if (isNumber(text)) {
        const long v = toLong(text, 10);
        c.r = static_cast<int>(v & 0xFF);
        c.g = static_cast<int>((v >> 8) & 0xFF);
        c.b = static_cast<int>((v >> 16) & 0xFF);
        c.a = static_cast<int>((v >> 24) & 0xFF);
        return c;
    }
    // Legacy calls Upper() without using its result: a lowercase "h" stays.
    u16 colour(text);
    const bool html = colour.starts_with(u'#');
    removeAll(colour, u'&');
    removeAll(colour, u'H');
    removeAll(colour, u'#');
    if (colour.size() > 7) {
        c.a = static_cast<int>(toLong(subString(colour, 0, 1), 16));
        colour = colour.substr(2);
    }
    u16 r = subString(colour, 4, 5), g = subString(colour, 2, 3), b = subString(colour, 0, 1);
    if (html)
        std::swap(r, b);
    c.r = static_cast<int>(toLong(r, 16));
    c.g = static_cast<int>(toLong(g, 16));
    c.b = static_cast<int>(toLong(b, 16));
    return c;
}

u16 assColourText(const TagColour &colour, bool alpha, bool style)
{
    u16 out = u"&H";
    if (alpha)
        out += hex2(colour.a);
    out += hex2(colour.b) + hex2(colour.g) + hex2(colour.r);
    if (!style)
        out += u"&";
    return out;
}

TagColour colourInEffect(const EditorText &state, int number_, TagColour style, long *position)
{
    TagEditor editor(state);
    u16 retTag;
    if (editor.findTag(colourPattern(number_, false), 0, true)) {
        if (!editor.finding().empty())
            retTag = editor.finding();
        const TagColour tag = parseAssColour(u"&" + retTag);
        style.r = tag.r;
        style.g = tag.g;
        style.b = tag.b;
    }
    if (editor.findTag(number(number_) + u"a&|alpha(.*)", 0, true)) {
        // GetTextResult keeps the previous result when nothing was captured.
        if (!editor.finding().empty())
            retTag = editor.finding();
        style.a = alphaOf(retTag);
    }
    if (position)
        *position = editor.position().first;
    return style;
}

StepResult changeColour(EditorText state, int n, const TagColour &actual, const TagColour &chosen)
{
    TagEditor editor(std::move(state));
    editor.setFocus(false);
    editor.findTag(colourPattern(n, true), 0, true);
    if (actual.r != chosen.r || actual.g != chosen.g || actual.b != chosen.b)
        editor.putTagInText(u"\\" + number(n) + u"c" + assColourText(chosen, false, true) + u"&",
                            u"\\" + number(n) + u"c" + assColourText(actual, false, true) + u"&");
    if (editor.findTag(u"(" + number(n) + u"a&|alpha.*)", 0, true)) {
        if (!editor.finding().starts_with(number(n) + u"a&")) {
            auto [x, y] = editor.position();
            ++y;
            editor.setPosition(y, y);
        }
    }
    if (actual.a != chosen.a)
        editor.putTagInText(u"\\" + number(n) + u"a&H" + hex2(chosen.a) + u"&",
                            u"\\" + number(n) + u"a&H" + hex2(actual.a) + u"&");
    return {editor.state(), editor.position().first};
}

u16 changeColourInLine(u16 text, int n, const TagColour &actual, const TagColour &chosen)
{
    if (actual.r != chosen.r || actual.g != chosen.g || actual.b != chosen.b)
        text = putTagInLine(std::move(text), colourPattern(n, true),
                            u"\\" + number(n) + u"c" + assColourText(chosen, false, true) + u"&");
    if (actual.a != chosen.a)
        text = putTagInLine(std::move(text), u"(" + number(n) + u"a&|alpha.*)",
                            u"\\" + number(n) + u"a&H" + hex2(chosen.a) + u"&");
    return text;
}

EditStep colourNonAssStep(const TagColour &chosen)
{
    return {u"C:" + assColourText(chosen, false, true).substr(2), u"C:([^}]*)", {}, true};
}

} // namespace hikari::core::legacy
