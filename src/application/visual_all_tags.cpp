#include "hikari/application/visual_all_tags.h"

#include "hikari/application/hotkeys.h"
#include "hikari/application/visual_script.h"
#include "hikari/core/style.h"
#include "hikari/core/text_projection.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>

// Built without floating-point contraction, as visual_view.cpp.

namespace hikari::application::visual {

using namespace transform;

namespace {

using u16 = std::u16string;
using u16v = std::u16string_view;

u16 u16of(const std::u8string &text)
{
    return core::toUtf16(text);
}

std::u8string u8(const u16 &text)
{
    return core::toUtf8(text);
}

u16 ascii(const std::string &s)
{
    return u16(s.begin(), s.end());
}

bool isBlank(char16_t c)
{
    return c == u' ' || (c >= u'\t' && c <= u'\r');
}

// wxString::Trim(false).Trim(): blanks off both ends.
u16 trim(u16v text)
{
    std::size_t a = 0, b = text.size();
    while (a < b && isBlank(text[a]))
        ++a;
    while (b > a && isBlank(text[b - 1]))
        --b;
    return u16(text.substr(a, b - a));
}

// wxStringTokenizer(text, delimiter, wxTOKEN_STRTOK).
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

// wxString::Mid(first, count).
u16 midText(u16v text, std::size_t first, std::size_t count = u16::npos)
{
    if (first >= text.size())
        return {};
    return u16(text.substr(first, count));
}

// config.h's MID(a, b, c): MAX(a, MIN(b, c)).
template <typename T>
T mid3(T a, T b, T c)
{
    const T m = b < c ? b : c;
    return a > m ? a : m;
}

// AssColor (styles.cpp:23-77): an ASS colour's red, green and blue
// (SetAss), and SetAlphaString's alpha (a base-16 ToLong of the text without
// '&' and 'H'; one that does not read leaves the alpha it had).
struct Rgb {
    long r = 0, g = 0, b = 0;
};
Rgb assColour(const u16 &text)
{
    const core::Colour c = core::legacy::colour(u8(text));
    return {static_cast<long>(c.r), static_cast<long>(c.g), static_cast<long>(c.b)};
}
long alphaOf(u16 alpha, long previous)
{
    std::erase(alpha, u'&');
    std::erase(alpha, u'H');
    if (alpha.empty())
        return previous;
    std::string s;
    for (const char16_t c : alpha) {
        if (c > 0x7F)
            return previous;
        s += static_cast<char>(c);
    }
    char *end = nullptr;
    errno = 0;
    const long value = std::strtol(s.c_str(), &end, 16);
    if (end == s.c_str() || *end != '\0' || errno == ERANGE)
        return previous;
    return value;
}

u16 hexByte(int value)
{
    char buffer[8];
    std::snprintf(buffer, sizeof buffer, "%02X", static_cast<unsigned int>(value));
    return ascii(buffer);
}

const u16 kValuePattern = u"([-0-9.,\\(\\) &A-FH]+)";

// TagFindReplace::ReplaceValue (TagFindReplace.cpp:494-508).
int replaceValue(u16 &txt, const u16 &what, const FindData &fdata)
{
    u16 changedValue = what;
    if (!fdata.inBracket)
        changedValue = u"{" + changedValue + u"}";
    if (fdata.y)
        txt.erase(static_cast<std::size_t>(fdata.x), static_cast<std::size_t>(fdata.y));
    if (!changedValue.empty())
        txt.insert(static_cast<std::size_t>(fdata.x), changedValue);
    return static_cast<int>(changedValue.size()) - static_cast<int>(fdata.y);
}

// TagFindReplace::ReplaceAllByChar (TagFindReplace.cpp:306-421): the tag
// before every visible character (a block's own tag replaced), `func`
// giving each value with the number of characters counted first.
void replaceAllByChar(TagFind &find, u16v pattern, u16v tag, u16 &text,
                      const std::function<void(const FindData &, u16 &, std::size_t)> &func)
{
    const std::size_t len = text.size();
    bool block = false;
    std::size_t numOfChars = 0;
    std::size_t i = 0;
    // Count the characters.
    while (i < len) {
        const char16_t ch = text[i];
        if (ch == u'{') {
            block = true;
        } else if (ch == u'}') {
            block = false;
        } else if (ch == u'\\' && !block) {
            if (i < len - 1) {
                const char16_t nch = text[i + 1];
                if (nch == u'h') {
                    // \h is one character, not counted at the start.
                    i++;
                } else if (nch == u'N' || nch == u'n') {
                    i++;
                } else {
                    numOfChars++;
                }
            }
        } else if (!block && (ch != u' ')) {
            numOfChars++;
        }
        i++;
    }
    // Every character.
    i = 0;
    std::size_t lastTagBlockStart = 0;
    const auto put = [&](const FindData &res) {
        u16 changedValue;
        func(res, changedValue, numOfChars);
        changedValue = u"\\" + u16(tag) + changedValue;
        return replaceValue(text, changedValue, res);
    };
    while (i < text.size()) {
        const char16_t ch = text[i];
        if (ch == u'{') {
            block = true;
            lastTagBlockStart = i;
        } else if (ch == u'}') {
            block = false;
            const u16 tagsBlock = midText(text, lastTagBlockStart, (i - lastTagBlockStart) + 1);
            FindData res;
            if (find.findTag(pattern, tagsBlock, 1)) {
                res = find.result();
                res.y -= (res.x - 1);
                res.x += static_cast<long>(lastTagBlockStart);
            } else {
                res = FindData{{}, static_cast<long>(i), 0, true};
            }
            i = static_cast<std::size_t>(static_cast<long>(i) + put(res));
            // Not taken as a normal character.
            i++;
            // A \h after the block takes the next visible character too, as
            // at the start (T6-gradient-hard-space: legacy stepped onto its
            // "h" and put a tag inside it); a block or tag after it is read.
            if (i + 1 < text.size() && text[i] == u'\\' && text[i + 1] == u'h') {
                i++;
                if (i + 1 < text.size() && text[i + 1] != u'{' && text[i + 1] != u'\\')
                    i++;
            }
        } else if (ch == u'\\' && !block) {
            if (i < text.size() - 1) {
                const char16_t nch = text[i + 1];
                if (nch == u'h') {
                    if (i == 0) {
                        // \h at the start takes the next visible character.
                        i = static_cast<std::size_t>(static_cast<long>(i) + put(FindData{{}, static_cast<long>(i), 0, false}));
                        i++;
                    }
                    i++;
                } else if (nch == u'N' || nch == u'n') {
                    i++;
                } else {
                    i = static_cast<std::size_t>(static_cast<long>(i) + put(FindData{{}, static_cast<long>(i), 0, false}));
                }
            }
        } else if (!block && (ch != u' ' || i == 0)) {
            i = static_cast<std::size_t>(static_cast<long>(i) + put(FindData{{}, static_cast<long>(i), 0, false}));
            if (i == 0 && ch == u' ')
                i++;
        }
        i++;
    }
}

} // namespace

bool keyMatchesAccelerator(const Key &key, std::string_view accel)
{
    // Hotkeys::GetHKey (Hotkeys.cpp:340-380): the modifiers found in the
    // text and the key after the last '-'; GetModifiers() must equal them.
    if (accel.empty())
        return false;
    const bool alt = accel.find("Alt-") != std::string_view::npos;
    const bool ctrl = accel.find("Ctrl-") != std::string_view::npos;
    const bool shift = accel.find("Shift-") != std::string_view::npos;
    std::string_view keyText = accel;
    if (accel.ends_with('-'))
        keyText = "-";
    else if (const auto dash = accel.rfind('-'); dash != std::string_view::npos)
        keyText = accel.substr(dash + 1);
    if (keyText.size() != 1)
        return false;
    char c = keyText[0];
    if (c >= 'a' && c <= 'z')
        c = static_cast<char>(c - 'a' + 'A');
    return key.key == c && key.alt == alt && key.control == ctrl && key.shift == shift;
}

void AllTagsTool::selected(VisualHost &host)
{
    // Visuals::Get made a new AllTags (VisualAllTags.cpp:22-32): its state
    // starts over; the toolbar's list and change option stay.
    (void)host;
    const int selection = m_listSelection, mode = m_listMode;
    *this = AllTagsTool();
    m_listSelection = selection;
    m_listMode = mode;
}

const std::vector<AllTagsSetting> &AllTagsTool::tags(const VisualHost &host) const
{
    // VideoToolbar::GetTagsSettings: the host's, else legacy's defaults.
    if (const auto *t = host.allTagsSettings(); t && !t->empty())
        return *t;
    if (m_ownTags.empty())
        m_ownTags = defaultAllTags();
    return m_ownTags;
}

void AllTagsTool::takeView(const VideoView &view)
{
    m_coeffW = view.coeffW();
    m_coeffH = view.coeffH();
    m_videoWidth = view.videoRect().right;
}

void AllTagsTool::reset(VisualHost &host)
{
    // Visuals::SetVisual(dial, tool) (Visuals.cpp:256-283): the editor's
    // text kept for the dummy edits, ChangeTool(tool, true), SetCurVisual.
    const Context ctx = context(host);
    if (ctx.active) {
        m_start = transform::startMs(*ctx.active);
        m_end = transform::endMs(*ctx.active);
    }
    takeView(host.view());
    if (!host.view().hasVideo() || ctx.width <= 0 || ctx.height <= 0) {
        m_coeffW = m_coeffH = 1.f;
        host.toolChanged();
        return;
    }
    m_currentLineText = ctx.editorText;
    // A reset drops a held thumb (Esc, another active Line: the gesture
    // rule); legacy reset the tool only outside a drag.
    for (Slider &slider : m_slider) {
        if (slider.holding) {
            slider.holding = false;
            slider.thumbState = 0;
        }
    }
    m_find.setSeveralLines(several(host));
    const auto [selFrom, selTo] = host.editorSelection();
    m_find.setSelection(selFrom, selTo);
    changeTool(toggled(), true, host);
    setCurVisual(host);
    host.toolChanged();
}

void AllTagsTool::setToggled(int tool, VisualHost *host)
{
    m_listMode = tool >> 20;
    m_listSelection = (tool << 12) >> 12;
    if (host)
        changeTool(toggled(), false, *host);
}

void AllTagsTool::definitionsChanged(const std::vector<std::u16string> &oldNames, VisualHost &host)
{
    // The Edit button's OK (VideoToolbar.cpp:779-791): the list takes the
    // new names (HikariChoice::PutArray, ListControls.cpp:498-520: the
    // selection kept by its name where the list still has it, else the
    // first) and the tool its GetItemToggled.
    const auto names = allTagsNames(tags(host));
    const u16 ce = (m_listSelection >= 0 && m_listSelection < static_cast<int>(oldNames.size()))
                       ? oldNames[static_cast<std::size_t>(m_listSelection)]
                       : u16();
    int choice = m_listSelection;
    if (names.empty()) {
        choice = -1;
    } else if (!ce.empty()) {
        if (choice >= static_cast<int>(names.size()))
            choice = 0;
        if (ce != names[static_cast<std::size_t>(choice)]) {
            const auto at = std::find(names.begin(), names.end(), ce);
            choice = at == names.end() ? 0 : static_cast<int>(at - names.begin());
        }
    }
    m_listSelection = choice;
    changeTool(toggled(), false, host);
}

std::vector<ToolOption> AllTagsTool::options(const VisualHost &host) const
{
    // AllTagsItem::ShowContols (VideoToolbar.cpp:737-799): the tag list, the
    // change options and the Edit button.
    std::vector<ToolOption> out;
    ToolOption list;
    list.name = "tag";
    list.kind = ToolOption::Kind::Choice;
    list.iconRole = "tag-list";
    list.tooltip = u"List of tags that can edit visual tool";
    list.choices = allTagsNames(tags(host));
    list.index = m_listSelection;
    out.push_back(std::move(list));
    ToolOption change;
    change.name = "changeOption";
    change.kind = ToolOption::Kind::Choice;
    change.iconRole = "tag-change-option";
    change.tooltip = allTagsChangeOptionsHelp();
    for (const auto option : allTagsChangeOptions())
        change.choices.emplace_back(option);
    change.index = m_listMode;
    out.push_back(std::move(change));
    ToolOption edit;
    edit.name = "edit";
    edit.kind = ToolOption::Kind::Action;
    edit.iconRole = "tag-edit";
    edit.tooltip = u"Edit listed tags and create new ones";
    out.push_back(std::move(edit));
    return out;
}

bool AllTagsTool::setOption(const std::string &name, int value, VisualHost &host)
{
    // The lists' choice (sendItemToggled, VideoToolbar.cpp:770-781): a tag
    // brings its own change option; then the tool takes GetItemToggled. The
    // Edit button is the host's (the "Tag editing" dialog).
    const auto count = static_cast<int>(tags(host).size());
    if (name == "tag") {
        if (value < 0 || value >= count)
            return false;
        m_listSelection = value;
        m_listMode = tags(host)[static_cast<std::size_t>(value)].tagMode;
    } else if (name == "changeOption") {
        if (value < 0 || value >= static_cast<int>(allTagsChangeOptions().size()))
            return false;
        m_listMode = value;
    } else {
        return false;
    }
    changeTool(toggled(), false, host);
    return true;
}

void AllTagsTool::changeTool(int tool, bool blockSetCurVisual, VisualHost &host)
{
    // AllTags::ChangeTool (VisualAllTags.cpp:302-314); lastTool is never
    // set, so every change is taken.
    if (m_lastTool == tool)
        return;
    m_mode = tool >> 20;
    m_replaceTagsInCursorPosition = m_mode == PasteInsert;
    const int curtag = tool << 12;
    m_currentTag = curtag >> 12;
    if (!blockSetCurVisual)
        setCurVisual(host);
}

void AllTagsTool::checkTag()
{
    // AllTags::CheckTag (VisualAllTags.cpp:158-183).
    const u16 &t = m_actualTag.tag;
    if (t == u"1a" || t == u"2a" || t == u"3a" || t == u"4a" || t == u"alpha")
        m_tagMode = IsHexAlpha;
    else if (t == u"1c" || t == u"2c" || t == u"3c" || t == u"4c" || t == u"c")
        m_tagMode = IsHexColour;
    else if (t == u"p" || t == u"clip" || t == u"iclip")
        m_tagMode = IsVector;
    else if (t == u"t")
        m_tagMode = IsTAnimation;
    else
        m_tagMode = 0;
}

void AllTagsTool::setupSlidersPosition(int position)
{
    // AllTags::SetupSlidersPosition (VisualAllTags.cpp:185-200).
    if (m_sliderPositionY == position && m_sliderPositionY != -1)
        return;
    m_sliderPositionY = m_sliderPositionY == -1 ? 40 : position;
    const float left = 30;
    const float right = static_cast<float>(m_videoWidth - 30);
    float bottom = static_cast<float>(m_sliderPositionY);
    float top = static_cast<float>(m_sliderPositionY - 6);
    for (Slider &s : m_slider) {
        s.left = left;
        s.top = top;
        s.right = right;
        s.bottom = bottom;
        top += static_cast<float>(m_increase);
        bottom += static_cast<float>(m_increase);
    }
}

void AllTagsTool::setCurVisual(VisualHost &host)
{
    // AllTags::SetCurVisual (VisualAllTags.cpp:202-224).
    const auto &definitions = tags(host);
    if (m_currentTag < 0 || m_currentTag >= static_cast<int>(definitions.size()))
        m_currentTag = 0;
    m_actualTag = definitions[static_cast<std::size_t>(m_currentTag)];
    m_floatFormat = "5." + std::to_string(static_cast<int>(m_actualTag.digitsAfterDot)) + "f";
    setupSlidersPosition(m_sliderPositionY);
    if (m_mode >= PasteMultiply && m_mode <= PasteMultiplyPlus) {
        const AllTagsSetting &tmp = definitions[static_cast<std::size_t>(m_currentTag)];
        for (int i = 0; i < m_actualTag.numOfValues && i < 4; i++) {
            Slider &s = m_slider[static_cast<std::size_t>(i)];
            s.firstThumbValue = s.thumbValue = tmp.values[static_cast<std::size_t>(i)];
        }
    }
    checkTag();
    const Context ctx = context(host);
    const auto [selFrom, selTo] = host.editorSelection();
    if (!host.gesture())
        m_find.setSelection(selFrom, selTo);
    findTagValues(ctx);
    if (m_mode < PasteMultiply || m_mode > PasteMultiplyPlus)
        for (int i = 0; i < m_actualTag.numOfValues && i < 4; i++)
            m_slider[static_cast<std::size_t>(i)].thumbValue = m_actualTag.values[static_cast<std::size_t>(i)];
    host.toolChanged();
}

void AllTagsTool::checkRange(float val)
{
    if (val < m_actualTag.rangeMin)
        m_actualTag.rangeMin = val;
    if (val > m_actualTag.rangeMax)
        m_actualTag.rangeMax = val;
}

void AllTagsTool::findTagValues(const Context &ctx)
{
    // AllTags::FindTagValues (VisualAllTags.cpp:226-300): the Style's value,
    // then the tag's in the editor's text.
    if (!ctx.active)
        return;
    const core::StyleValues style = lineStyle(ctx, ctx.active->style);
    const u16 value = u16of(core::legacy::styleTagValue(style, u8(m_actualTag.tag)).value_or(u8""));
    double doubleValue = 0.;
    auto &values = m_actualTag.values;
    if (m_tagMode & IsHexAlpha) {
        values[0] = static_cast<float>(alphaOf(value, 0));
    } else if (m_tagMode & IsHexColour) {
        const Rgb col = assColour(value);
        values[0] = static_cast<float>(col.r);
        values[1] = static_cast<float>(col.g);
        values[2] = static_cast<float>(col.b);
    } else if (m_actualTag.tag == u"pos") {
        const PointF pos = linePosition(ctx, *ctx.active).pos;
        values[0] = pos.x;
        values[1] = pos.y;
    } else {
        if (!toDouble(value, doubleValue))
            doubleValue = atoi(value);
        values[0] = static_cast<float>(doubleValue);
    }
    if (m_find.findTag(m_actualTag.tag + kValuePattern, ctx.editorText, m_mode == PasteInsert ? m_actualTag.mode : 1)) {
        const u16 finding = m_find.result().finding;
        if (finding.starts_with(u"(")) {
            // The brackets cut.
            int i = 0;
            for (const u16 &raw : strtok(midText(finding, 1, finding.size() - 2), u',')) {
                const u16 token = trim(raw);
                double val = 0;
                if (toDouble(token, val)) {
                    if (i < 4)
                        values[static_cast<std::size_t>(i)] = static_cast<float>(val);
                    if (m_mode < PasteMultiply)
                        checkRange(static_cast<float>(val));
                }
                i++;
                if (i >= m_actualTag.numOfValues)
                    break;
            }
        } else {
            double val = 0;
            if (m_tagMode & IsHexAlpha) {
                values[0] = static_cast<float>(alphaOf(finding, 0));
            } else if (m_tagMode & IsHexColour) {
                const Rgb col = assColour(finding);
                values[0] = static_cast<float>(col.r);
                values[1] = static_cast<float>(col.g);
                values[2] = static_cast<float>(col.b);
            } else if (toDouble(finding, val)) {
                values[0] = static_cast<float>(val);
            } else {
                return;
            }
            if (m_mode < PasteMultiply)
                checkRange(static_cast<float>(val));
        }
    }
}

bool AllTagsTool::several(VisualHost &host) const
{
    // EditBox::IsCursorOnStart: more than one Line selected (hidden ones
    // counted).
    return severalLines(host.batchTargets(), host.activeLine());
}

float AllTagsTool::diffValue(const Slider &slider) const
{
    // AllTagsSlider::GetDiffValue (VisualAllTagsControls.cpp:346-349).
    return slider.holding || m_mode == 2 ? slider.thumbValue - slider.firstThumbValue : 0;
}

u16 AllTagsTool::selectedTagIn(FindData *result) const
{
    // AllTags::GetSelectedTag (VisualAllTags.cpp:124-156): a selected tag
    // ("\..." with one backslash) in the editor, taken to its end.
    const auto [start, end] = m_find.selection();
    if (start != end) {
        const u16 &txt = m_editorText;
        const u16 tag = midText(txt, static_cast<std::size_t>(start), static_cast<std::size_t>(end - start));
        if (tag.starts_with(u"\\")) {
            if (static_cast<std::size_t>(end + 1) < txt.size() && std::count(tag.begin(), tag.end(), u'\\') == 1) {
                result->inBracket = true;
                if (!(txt[static_cast<std::size_t>(end)] == u'\\' || txt[static_cast<std::size_t>(end)] == u'}')) {
                    const std::size_t slash = txt.find(u'\\', static_cast<std::size_t>(end));
                    const std::size_t endBracket = txt.find(u'}', static_cast<std::size_t>(end));
                    if (slash != u16::npos || endBracket != u16::npos) {
                        const std::size_t endPos = (slash < endBracket) ? slash : endBracket;
                        const u16 fulltag = midText(txt, static_cast<std::size_t>(start), (endPos - static_cast<std::size_t>(start) + 1));
                        result->x = start;
                        result->y = static_cast<long>(endPos);
                        return fulltag;
                    }
                } else {
                    result->x = start;
                    result->y = end - 1;
                    return tag;
                }
            }
        }
    }
    return {};
}

u16 AllTagsTool::visualValue(const u16 &curValue) const
{
    // AllTags::GetVisualValue (VisualAllTags.cpp:316-440).
    const float value = m_slider[0].thumbValue;
    const float value2 = m_slider[1].thumbValue;
    const float value3 = m_slider[2].thumbValue;
    const float value4 = m_slider[3].thumbValue;
    const float valuediff = diffValue(m_slider[0]);
    const float valuediff2 = diffValue(m_slider[1]);
    const float valuediff3 = diffValue(m_slider[2]);
    const float valuediff4 = diffValue(m_slider[3]);
    const auto &values = m_actualTag.values;
    u16 strval;
    if (curValue.empty() || m_mode == PasteMultiply || m_mode == PasteInsert) {
        // One value pasted, or (multiply) the definition's value moved by
        // the counter.
        const float val1 = (m_mode >= PasteMultiply) ? values[0] + (m_multiplyCounter * valuediff) : value;
        const float val2 = (m_mode >= PasteMultiply) ? values[1] + (m_multiplyCounter * valuediff2) : value2;
        const float val3 = (m_mode >= PasteMultiply) ? values[2] + (m_multiplyCounter * valuediff3) : value3;
        if (m_tagMode & IsHexAlpha) {
            strval = u"&H" + hexByte(mid3(0, static_cast<int>(val1 + 0.5), 255)) + u"&";
        } else if (m_tagMode & IsHexColour) {
            // BGR, the sliders RGB.
            strval = u"&H" + hexByte(mid3(0, static_cast<int>(val3 + 0.5), 255)) +
                     hexByte(mid3(0, static_cast<int>(val2 + 0.5), 255)) +
                     hexByte(mid3(0, static_cast<int>(val1 + 0.5), 255)) + u"&";
        } else if (m_actualTag.numOfValues > 1) {
            const float val4 = (m_mode >= PasteMultiply) ? values[3] + (m_multiplyCounter * valuediff4) : value4;
            strval = u"(" + getfloat(val1, m_floatFormat) + u"," + getfloat(val2, m_floatFormat);
            if (m_actualTag.numOfValues >= 3)
                strval += u"," + getfloat(val3, m_floatFormat);
            if (m_actualTag.numOfValues == 4)
                strval += u"," + getfloat(val4, m_floatFormat);
            if (m_tagMode & IsTAnimation)
                strval += u",";
            if (curValue.empty() || curValue.ends_with(u")"))
                strval += u")";
        } else {
            strval = getfloat(val1, m_floatFormat);
        }
    } else if (curValue.starts_with(u"(")) {
        const bool hasLastBracket = curValue.ends_with(u")");
        // The brackets cut.
        const u16 inner = midText(curValue, 1, hasLastBracket ? curValue.size() - 2 : curValue.size() - 1);
        strval = u"(";
        int counter = 0;
        for (const u16 &raw : strtok(inner, u',')) {
            const u16 token = trim(raw);
            double val = 0;
            if (toDouble(token, val)) {
                // counter % 2 is never 2: the third and fourth take the
                // first two's differences (legacy).
                float valdiff = (counter % 2 == 0) ? valuediff : (counter % 2 == 1) ? valuediff2 : valuediff3;
                (void)valuediff4;
                if (m_mode > PasteMultiply)
                    valdiff *= m_multiplyCounter;
                strval += getfloat(static_cast<float>(val + valdiff), m_floatFormat) + u",";
            }
            counter++;
        }
        if (strval.ends_with(u",") && !(m_tagMode & IsTAnimation))
            strval = strval.substr(0, strval.size() - 1);
        if (hasLastBracket)
            strval += u")";
    } else {
        double val = 0;
        u16 trimed = trim(curValue);
        // A tag inside \t ends with its bracket.
        bool hasEndBracked = false;
        if (trimed.ends_with(u")")) {
            trimed = trimed.substr(0, trimed.size() - 1);
            hasEndBracked = true;
        }
        if (m_tagMode & IsHexAlpha) {
            const long a = alphaOf(trimed, 0);
            const float vala = (m_mode > PasteMultiply) ? a + (valuediff * m_multiplyCounter) : a + valuediff;
            strval = u"&H" + hexByte(mid3(0, static_cast<int>(vala + 0.5), 255)) + u"&";
        } else if (m_tagMode & IsHexColour) {
            const Rgb col = assColour(trimed);
            const float valr = (m_mode > PasteMultiply) ? col.r + (valuediff * m_multiplyCounter) : col.r + valuediff;
            const float valg = (m_mode > PasteMultiply) ? col.g + (valuediff2 * m_multiplyCounter) : col.g + valuediff2;
            const float valb = (m_mode > PasteMultiply) ? col.b + (valuediff3 * m_multiplyCounter) : col.b + valuediff3;
            strval = u"&H" + hexByte(mid3(0, static_cast<int>(valb + 0.5), 255)) +
                     hexByte(mid3(0, static_cast<int>(valg + 0.5), 255)) +
                     hexByte(mid3(0, static_cast<int>(valr + 0.5), 255)) + u"&";
        } else if (toDouble(trimed, val)) {
            val += (m_mode > PasteMultiply) ? (valuediff * m_multiplyCounter) : valuediff;
            strval = getfloat(static_cast<float>(val), m_floatFormat);
        } else {
            // No bracket when no value was set.
            hasEndBracked = false;
        }
        if (hasEndBracked)
            strval += u")";
    }
    return strval;
}

long AllTagsTool::changeVisualEditor(u16 &txt)
{
    // AllTags::ChangeVisual(txt) (VisualAllTags.cpp:442-482): the one-Line
    // path; returns where the editor's caret goes.
    const u16 pattern = m_actualTag.tag + kValuePattern;
    if (m_mode == PasteGradientTextIncrease || m_mode == PasteGradientTextDecrease) {
        replaceAllByChar(m_find, pattern, m_actualTag.tag, txt, [this](const FindData &data, u16 &result, std::size_t n) {
            result = visualValue(data.finding);
            if (m_mode == PasteGradientTextIncrease)
                m_multiplyCounter += (1.f / (n - 1));
            else
                m_multiplyCounter -= (1.f / (n - 1));
        });
    } else if (m_mode) {
        m_find.findTag(pattern, txt, m_actualTag.mode);
        FindData res = m_find.result();
        u16 strValue = visualValue(res.finding);
        if (m_tagMode & IsTAnimation) {
            m_selectedTag = selectedTagIn(&res);
            if (!m_selectedTag.empty()) {
                if (strValue.ends_with(u")"))
                    strValue.insert(strValue.size() - 1, m_selectedTag);
                else
                    strValue += m_selectedTag;
                m_find.setResult(res);
            }
        }
        m_find.replace(u"\\" + m_actualTag.tag + strValue, txt);
    } else {
        m_find.replaceAll(pattern, m_actualTag.tag, txt,
                          [this](const FindData &data, u16 &result) { result = visualValue(data.finding); }, true);
        m_find.findTag(pattern, txt, 0);
    }
    return m_find.positionInText().first;
}

void AllTagsTool::changeVisualLine(u16 &txt, std::size_t numOfSelections)
{
    // AllTags::ChangeVisual(txt, dial, n) (VisualAllTags.cpp:484-529): the
    // several-Line path, each Line's text from its start.
    const u16 pattern = m_actualTag.tag + kValuePattern;
    if (m_mode == PasteGradientTextIncrease || m_mode == PasteGradientTextDecrease) {
        replaceAllByChar(m_find, pattern, m_actualTag.tag, txt, [this](const FindData &data, u16 &result, std::size_t n) {
            result = visualValue(data.finding);
            if (n > 1) {
                if (m_mode == PasteGradientTextIncrease)
                    m_multiplyCounter += (1.f / (n - 1));
                else
                    m_multiplyCounter -= (1.f / (n - 1));
            }
        });
    } else if (m_mode) {
        m_find.findTag(pattern, txt, 1);
        FindData res = m_find.result();
        u16 strValue = visualValue(res.finding);
        if (m_tagMode & IsTAnimation) {
            m_selectedTag = m_selectedTag.empty() ? selectedTagIn(&res) : m_selectedTag;
            if (!m_selectedTag.empty()) {
                if (strValue.ends_with(u")"))
                    strValue.insert(strValue.size() - 1, m_selectedTag);
                else
                    strValue += m_selectedTag;
            }
        }
        m_find.replace(u"\\" + m_actualTag.tag + strValue, txt);
        if (m_mode == PasteGradientLineIncrease)
            m_multiplyCounter += (1.f / (numOfSelections - 1));
        else if (m_mode == PasteGradientLineDecrease)
            m_multiplyCounter -= (1.f / (numOfSelections - 1));
        else if (m_mode >= PasteMultiply)
            m_multiplyCounter++;
    } else {
        m_find.replaceAll(pattern, m_actualTag.tag, txt,
                          [this](const FindData &data, u16 &result) { result = visualValue(data.finding); }, true);
    }
}

bool AllTagsTool::beginEdit(VisualHost &host)
{
    // The gesture's targets are fixed when it begins (edit-transactions.md);
    // legacy's SetVisual read the selection each time. Several Lines: the
    // selected Lines the Grid shows (SubsFile::GetSelections).
    if (host.gesture())
        return true;
    const auto targets = host.batchTargets();
    if (targets.empty())
        return false;
    const bool many = severalLines(targets, host.activeLine());
    m_targets.clear();
    if (many) {
        const auto shown = host.shownLines();
        for (const core::LineId id : targets)
            if (!shown || shown(id))
                m_targets.push_back(id);
    } else {
        m_targets = targets;
    }
    if (m_targets.empty())
        return false;
    auto g = host.beginGesture(m_targets, std::string(familyInfo(Family::Hydra).history));
    if (!g)
        return false;
    m_find.setSeveralLines(many);
    m_editing.reset();
    if (!many) {
        const Context ctx = context(host);
        m_editing = targets.front();
        m_editorText = ctx.editorText;
        m_editorIsTranslation = ctx.editorIsTranslation;
    }
    return true;
}

bool AllTagsTool::finishEdit(VisualHost &host)
{
    // A refused commit wrote nothing: the tool reads the unchanged text
    // again, as after Esc.
    // A commit that changes no Line records nothing (legacy's Send and
    // SetModified made an undo step of it).
    if (host.gesture() && !host.gesture()->changesAnyLine())
        host.cancelGesture();
    const bool committed = !host.gesture() || host.commitGesture().has_value();
    m_editing.reset();
    if (!committed)
        reset(host);
    return committed;
}

void AllTagsTool::setVisual(bool dummy, VisualHost &host)
{
    // Visuals::SetVisual(dummy) (Visuals.cpp:726-832).
    if (!beginEdit(host))
        return;
    Gesture *g = host.gesture();
    if (!m_editing) {
        const Context ctx = context(host);
        for (const auto &id : g->targets()) {
            const core::LineRecord &line = g->before(id);
            u16 txt = transform::lineText(line);
            changeVisualLine(txt, g->targets().size());
            g->stage(id, u8(txt), transform::editsTranslation(line));
        }
        if (!dummy)
            (void)finishEdit(host);
    } else if (dummy) {
        u16 txt = m_replaceTagsInCursorPosition ? m_editorText : m_currentLineText;
        const long pos = changeVisualEditor(txt);
        m_editorText = txt;
        m_find.setSelection(pos, pos);
        g->stage(*m_editing, u8(txt), m_editorIsTranslation);
    } else {
        // The caret goes to the tag only in the text that was written; it
        // and the text are taken first, as the host may reload the editor
        // while it commits.
        const u16 written = m_editorText;
        const auto [selFrom, selTo] = m_find.selection();
        if (finishEdit(host)) {
            m_currentLineText = written;
            m_find.setSelection(selFrom, selTo);
            host.setEditorSelection(selFrom, selTo);
        }
    }
    host.toolChanged();
}

void AllTagsTool::pointer(const Pointer &event, VisualHost &host)
{
    // AllTags::OnMouseEvent (VisualAllTags.cpp:41-82).
    takeView(host.view());
    if (event.kind == Pointer::Kind::DoubleClick || event.kind == Pointer::Kind::Enter)
        return; // the press before it was the double click's (Qt sends both)
    if (m_mode >= PasteMultiply)
        m_multiplyCounter = (m_mode == PasteGradientTextDecrease || m_mode == PasteGradientLineDecrease) ? 1.f : 0.f;
    const float y = static_cast<float>(event.y);
    const bool rightDown = event.kind == Pointer::Kind::Press && event.button == Pointer::Button::Right;
    const bool rightUp = event.kind == Pointer::Kind::Release && event.button == Pointer::Button::Right;
    // Right holding moves the sliders.
    if (m_rholding) {
        setupSlidersPosition(static_cast<int>(y + m_sliderPositionDiff));
        host.toolChanged();
        for (int i = 0; i < m_actualTag.numOfValues && i < 4; i++)
            m_slider[static_cast<std::size_t>(i)].onThumb = m_slider[static_cast<std::size_t>(i)].onSlider = false;
        if (!rightUp)
            return;
    }
    if (rightDown) {
        m_sliderPositionDiff = static_cast<int>(static_cast<float>(m_sliderPositionY) - y);
        m_rholding = true;
    }
    if (rightUp)
        m_rholding = false;
    // The count is read again for each slider: a tag changed by the first
    // (Shift and the wheel) changes it.
    std::array<bool, 4> wheeled{};
    for (int i = 0; i < m_actualTag.numOfValues && i < 4; i++)
        wheeled[static_cast<std::size_t>(i)] = sliderMouse(i, event, host);
    // Every slider takes the wheel's step, written as one step
    // (T6-wheel-one-step: legacy's sliders each wrote their own).
    if (std::find(wheeled.begin(), wheeled.end(), true) != wheeled.end()) {
        if (several(host)) {
            setVisual(false, host);
        } else {
            setVisual(true, host);
            setVisual(false, host);
        }
        for (std::size_t i = 0; i < wheeled.size(); i++)
            if (wheeled[i])
                m_slider[i].holding = false;
    }
}

bool AllTagsTool::sliderMouse(int index, const Pointer &event, VisualHost &host)
{
    // AllTagsSlider::OnMouseEvent (VisualAllTagsControls.cpp:87-246).
    Slider &s = m_slider[static_cast<std::size_t>(index)];
    const float range = m_actualTag.rangeMax - m_actualTag.rangeMin;
    if (range <= 0) {
        host.log(u"Bad range");
        return false;
    }
    const float thumbposdiff = -m_actualTag.rangeMin;
    const float sliderRange = s.right - s.left;
    const float coeff = sliderRange / range;
    s.x = static_cast<float>(event.x);
    s.y = static_cast<float>(event.y);
    const bool shift = event.shift;
    const bool leftDown = event.kind == Pointer::Kind::Press && event.button == Pointer::Button::Left;
    const bool leftUp = event.kind == Pointer::Kind::Release && event.button == Pointer::Button::Left;

    // The wheel.
    if (event.kind == Pointer::Kind::Wheel && event.wheelSteps != 0) {
        if (shift) {
            // The next or previous definition (the toolbar's list too).
            const int step = event.wheelSteps;
            const int count = static_cast<int>(tags(host).size());
            m_currentTag -= step;
            if (m_currentTag < 0)
                m_currentTag = count - 1;
            else if (m_currentTag >= count)
                m_currentTag = 0;
            int tool = m_mode << 20;
            tool += m_currentTag;
            // AllTagsItem::SetItemToggled (VideoToolbar.cpp:700-716).
            int selection = (tool << 12) >> 12;
            if (selection < 0)
                selection = count - 1;
            else if (selection >= count)
                selection = 0;
            m_listSelection = selection;
            changeTool(tool, false, host);
            return false;
        }
        const int rot = event.wheelSteps;
        if (m_mode != 2)
            s.firstThumbValue = s.thumbValue;
        s.thumbValue = rot < 0 ? s.thumbValue - m_actualTag.step : s.thumbValue + m_actualTag.step;
        s.thumbValue = mid3(m_actualTag.rangeMin, s.thumbValue, m_actualTag.rangeMax);
        if (s.firstThumbValue != s.thumbValue) {
            s.onThumb = true;
            s.onSlider = false;
            // Holding set first, so the value is used (pointer writes it).
            s.holding = true;
            return true;
        }
        return false;
    }

    const float thumbpos = ((s.thumbValue + thumbposdiff) * coeff) + s.left;
    const float thumbleft = thumbpos - 4;
    const float thumbright = thumbpos + 4;
    const float thumbtop = s.top - 10;
    const float thumbbottom = s.bottom + 10;
    const float x = s.x, y = s.y;
    // Leaving the window.
    if (event.kind == Pointer::Kind::Leave && s.thumbState != 0) {
        s.thumbState = 0;
        host.toolChanged();
    }
    if (!s.holding) {
        // Outside the slider, nothing to do.
        if ((x < s.left - 5 || y < thumbtop || x > s.right + 5 || y > thumbbottom)) {
            if (s.thumbState != 0 || s.onSlider || s.onThumb) {
                s.thumbState = 0;
                s.onSlider = s.onThumb = false;
                host.toolChanged();
            }
        }
        s.onThumb = false;
        s.onSlider = false;
        const bool buttonEvent = leftDown || leftUp;
        if (x >= thumbleft && x <= thumbright && y >= thumbtop && y <= thumbbottom) {
            s.onThumb = true;
            if (!buttonEvent && s.thumbState != 1) {
                s.thumbState = 1;
                host.toolChanged();
            }
        } else {
            if (y >= s.top - 5 && y <= s.bottom + 5 && x >= s.left && x <= s.right)
                s.onSlider = true;
            if (!buttonEvent) {
                s.thumbState = 0;
                host.toolChanged();
            }
        }
    }
    if (s.holding) {
        // The thumb follows the pointer.
        s.thumbValue = ((x - s.left) / coeff) - thumbposdiff;
        s.thumbValue = mid3(m_actualTag.rangeMin, s.thumbValue, m_actualTag.rangeMax);
        if (s.lastThumbValue != s.thumbValue) {
            if (!shift)
                setVisual(true, host);
        }
        s.lastThumbValue = s.thumbValue;
    }
    if (leftDown) {
        if (m_mode != PasteMultiply)
            s.lastThumbValue = s.firstThumbValue = s.thumbValue;
        else
            s.lastThumbValue = s.thumbValue;
        if (s.onThumb) {
            s.thumbState = 2;
            host.toolChanged();
            s.holding = true;
        } else if (s.onSlider) {
            s.thumbState = 1;
            s.thumbValue = ((x - s.left) / coeff) - thumbposdiff;
            s.thumbValue = mid3(m_actualTag.rangeMin, s.thumbValue, m_actualTag.rangeMax);
            // Holding set first, so the value is used.
            s.holding = true;
            if (several(host)) {
                setVisual(false, host);
            } else {
                setVisual(true, host);
                setVisual(false, host);
            }
            s.holding = false;
        }
    }
    if (leftUp && s.holding) {
        s.thumbState = 0;
        setVisual(false, host);
        s.holding = false;
    }
    return false;
}

bool AllTagsTool::key(const Key &event, VisualHost &host)
{
    // AllTags::OnKeyPress (VisualAllTags.cpp:84-122): for \fad or \t the
    // Line editor's "Insert difference from the start" / "to the end" keys
    // set the first or second slider to the video's time from the Line's
    // start (or to its end), as EditBox::OnPasteDifferents does.
    if (event.release)
        return false;
    if (!(m_actualTag.tag == u"fad" || m_tagMode & IsTAnimation))
        return false;
    const bool hkeystart = keyMatchesAccelerator(event, host.editorHotkey(3015)); // EDITBOX_START_DIFFERENCE
    const bool hkeyend = keyMatchesAccelerator(event, host.editorHotkey(3016));   // EDITBOX_END_DIFFERENCE
    if (!(hkeystart || hkeyend))
        return false;
    const Context ctx = context(host);
    if (!host.view().hasVideo() || !ctx.active) {
        host.bell();
        return true;
    }
    const int vidtime = static_cast<int>(host.videoTimeMs());
    if (vidtime < transform::startMs(*ctx.active) || vidtime > transform::endMs(*ctx.active)) {
        host.bell();
        return true;
    }
    const int diff = (hkeystart || m_tagMode & IsTAnimation) ? vidtime - transform::zeroit(transform::startMs(*ctx.active))
                                                             : std::abs(vidtime - transform::zeroit(transform::endMs(*ctx.active)));
    Slider &s = m_slider[hkeystart ? 0 : 1];
    s.thumbValue = static_cast<float>(diff);
    s.holding = true;
    if (several(host)) {
        setVisual(false, host);
    } else {
        setVisual(true, host);
        setVisual(false, host);
    }
    s.holding = false;
    return true;
}

void AllTagsTool::drawSlider(Overlay &out, const VisualHost &host, const Slider &s) const
{
    // AllTagsSlider::OnDraw (VisualAllTagsControls.cpp:248-339): the track,
    // its ticks with a value at every second, the thumb (hovered, held) and
    // the value under the pointer, in legacy's fixed colours.
    const float range = m_actualTag.rangeMax - m_actualTag.rangeMin;
    const float sliderRange = s.right - s.left;
    if (range <= 0 || sliderRange <= 0 || m_actualTag.step <= 0)
        return;
    const float coeff = sliderRange / range;
    const float step = m_actualTag.step * coeff;
    if (step <= 0)
        return;
    const float thumbposdiff = -m_actualTag.rangeMin;
    const float thumbtop = s.top - 7;
    const float thumbbottom = s.bottom + 7;
    const std::uint32_t fill = 0xAA121150, border = 0xFFBB0000;
    out.polygons.push_back({{{s.left, s.top}, {s.right, s.top}, {s.right, s.bottom}, {s.left, s.bottom}}, fill, border});
    // DRAWOUTTEXT: centred in its rectangle, at its top or (DT_BOTTOM) its
    // bottom, outlined.
    const auto text = [&](const u16 &label, long left, long top, long right, long bottom, bool atBottom) {
        const auto [w, h] = host.measureLabel(label);
        const int x = static_cast<int>(left + ((right - left) - w) / 2);
        const int y = static_cast<int>(atBottom ? bottom - h : top);
        out.texts.push_back({IntRect{x, y, x + w, y + h}, label, 0xFFFFFFFF, true, 0});
    };
    float lastPos = 0;
    int j = 0;
    const float rightend = s.right + (step / 2);
    const float distance = (std::abs(m_actualTag.rangeMax) > 999 || std::abs(m_actualTag.rangeMin) > 999) ? 15 : 10;
    for (float i = s.left; i <= rightend; i += step) {
        if (i - lastPos > distance) {
            out.lines.push_back({{i, s.top - 5}, {i, s.top + 10}, 2, 0xFFBB0000});
            lastPos = i;
            const float thumbOnSliderValue = ((i - s.left) / coeff) - thumbposdiff;
            const bool ismod0 = j % 4 == 0;
            if (j % 4 == 2 || ismod0) {
                const long left = static_cast<long>(i) - 50, right = static_cast<long>(i) + 50;
                const long top = ismod0 ? static_cast<long>(thumbbottom) + 2 : static_cast<long>(thumbtop) - 54;
                const long bottom = ismod0 ? static_cast<long>(thumbbottom) + 54 : static_cast<long>(thumbtop) - 2;
                text(getfloat(thumbOnSliderValue, m_actualTag.digitsAfterDot > 0 ? "5.1f" : "5.0f"), left, top, right,
                     bottom, !ismod0);
            }
            j++;
        }
    }
    const float thumbpos = ((s.thumbValue + thumbposdiff) * coeff) + s.left;
    const float thumbleft = thumbpos - 4;
    const float thumbright = thumbpos + 4;
    const std::uint32_t refill = (s.thumbState == 1) ? 0xAACC8748 : (s.thumbState == 2) ? 0xAAFCE6B1 : 0xAA121150;
    out.polygons.push_back({{{thumbleft, thumbtop}, {thumbright, thumbtop}, {thumbright, thumbbottom}, {thumbleft, thumbbottom}},
                            refill,
                            border,
                            {},
                            true});
    if (s.onThumb)
        text(getfloat(s.thumbValue, m_floatFormat), static_cast<long>(thumbleft) - 50, static_cast<long>(thumbbottom) + 10,
             static_cast<long>(thumbright) + 50, static_cast<long>(thumbbottom) + 50, false);
    if (s.onSlider) {
        float thumbOnSliderValue = ((s.x - s.left) / coeff) - thumbposdiff;
        thumbOnSliderValue = mid3(m_actualTag.rangeMin, thumbOnSliderValue, m_actualTag.rangeMax);
        text(getfloat(thumbOnSliderValue, m_floatFormat), static_cast<long>(s.x) - 50, static_cast<long>(s.y) + 20,
             static_cast<long>(s.x) + 50, static_cast<long>(s.y) + 70, false);
    }
}

Overlay AllTagsTool::overlay(const VisualHost &host) const
{
    // AllTags::DrawVisual (VisualAllTags.cpp:34-39): a slider per value.
    Overlay out;
    for (int i = 0; i < m_actualTag.numOfValues && i < 4; i++)
        drawSlider(out, host, m_slider[static_cast<std::size_t>(i)]);
    return out;
}

} // namespace hikari::application::visual
