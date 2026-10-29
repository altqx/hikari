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
#include <map>

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

u16 trim(u16v text)
{
    std::size_t a = 0, b = text.size();
    while (a < b && isBlank(text[a]))
        ++a;
    while (b > a && isBlank(text[b - 1]))
        --b;
    return u16(text.substr(a, b - a));
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

u16 midText(u16v text, std::size_t first, std::size_t count = u16::npos)
{
    if (first >= text.size())
        return {};
    return u16(text.substr(first, count));
}

template <typename T>
T mid3(T a, T b, T c)
{
    const T m = b < c ? b : c;
    return a > m ? a : m;
}

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

constexpr std::uint32_t kOutline = 0xFF6FD3FF;

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

// The tag before every visible character (a block's own tag replaced);
// `func` gives each value and gets the visible character count.
void replaceAllByChar(TagFind &find, u16v pattern, u16v tag, u16 &text,
                      const std::function<void(const FindData &, u16 &, std::size_t)> &func)
{
    const std::size_t len = text.size();
    bool block = false;
    std::size_t numOfChars = 0;
    std::size_t i = 0;
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
            i++;
            if (i + 1 < text.size() && text[i] == u'\\' &&
                (text[i + 1] == u'h' || text[i + 1] == u'N' || text[i + 1] == u'n')) {
                i++;
                if (i + 1 < text.size() && text[i + 1] != u'{' && text[i + 1] != u'\\')
                    i++;
            }
        } else if (ch == u'\\' && !block) {
            if (i < text.size() - 1) {
                const char16_t nch = text[i + 1];
                if (nch == u'h') {
                    if (i == 0) {
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

}

bool keyMatchesAccelerator(const Key &key, std::string_view accel)
{
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
    (void)host;
    const int selection = m_listSelection, mode = m_listMode;
    const bool legacy = m_legacySliders;
    Fields fields = m_fields;
    std::string marked = m_marked;
    *this = AllTagsTool();
    m_listSelection = selection;
    m_listMode = mode;
    m_legacySliders = legacy;
    m_fields = fields;
    m_marked = std::move(marked);
}

const std::vector<AllTagsSetting> &AllTagsTool::tags(const VisualHost &host) const
{
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
    m_drag.reset();
    m_sample.reset();
    if (m_last) {
        const auto *session = host.session();
        if (!session || session != m_last->session || session->revision() != m_last->revision ||
            session->draftLine() || host.activeLine() != m_last->active)
            m_last.reset();
    }
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

void AllTagsTool::definitionsChanged(const std::vector<std::u16string> &oldNames, VisualHost &host, bool shown)
{
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
    if (choice != m_listSelection || choice < 0 || ce != names[static_cast<std::size_t>(choice)])
        forgetValues();
    m_last.reset();
    m_listSelection = choice;
    if (shown)
        changeTool(toggled(), false, host);
}

AllTagsTool::Change AllTagsTool::changeOf(int pasteMode)
{
    switch (pasteMode) {
    case PasteInsert:
        return Change::Set;
    case PasteAdd:
        return Change::Add;
    case PasteMultiply:
    case PasteMultiplyPlus:
        return Change::Multiply;
    default:
        return Change::Gradient;
    }
}

std::vector<int> AllTagsTool::sectionsOf(const VisualHost &host) const
{
    return allTagsSections(tags(host));
}

std::vector<int> AllTagsTool::listOrder(const VisualHost &host) const
{
    const auto sections = sectionsOf(host);
    std::vector<int> out;
    for (int section = 0; section < 3; ++section)
        for (std::size_t i = 0; i < sections.size(); ++i)
            if (sections[i] == section)
                out.push_back(static_cast<int>(i));
    return out;
}

std::vector<ToolOption> AllTagsTool::options(const VisualHost &host) const
{
    std::vector<ToolOption> out;
    ToolOption list;
    list.name = "tag";
    list.kind = ToolOption::Kind::Choice;
    list.iconRole = "tag-list";
    list.tooltip = u"List of tags that can edit visual tool";
    const auto &definitions = tags(host);
    list.choices = allTagsNames(definitions);
    for (const AllTagsSetting &tag : definitions)
        list.notes.push_back(u"\\" + tag.tag);
    list.sections = sectionsOf(host);
    list.sectionNames = {u"Common", u"More", u"Your tags"};
    list.searchable = true;
    list.index = m_listSelection;
    list.group = u"Tag";
    out.push_back(std::move(list));
    ToolOption change;
    change.name = "change";
    change.kind = ToolOption::Kind::Switch;
    change.iconRole = "tag-change-option";
    change.tooltip = allTagsChangeOptionsHelp();
    change.choices = {u"Set", u"Add", u"Multiply", u"Gradient"};
    change.index = static_cast<int>(changeOf(m_listMode));
    change.group = u"Change";
    change.afterValues = true;
    out.push_back(std::move(change));
    ToolOption variant;
    variant.name = "variant";
    variant.kind = ToolOption::Kind::Choice;
    variant.iconRole = "tag-change-option";
    variant.group = u"Change";
    variant.afterValues = true;
    switch (changeOf(m_listMode)) {
    case Change::Set:
        variant.tooltip = u"Where the tag goes in the text";
        variant.choices = {u"At the cursor", u"At the start of the text", u"Clip rectangle with \\t animation"};
        variant.index = m_actualTag.mode;
        variant.enabled = !several(host);
        break;
    case Change::Add:
        break;
    case Change::Multiply:
        variant.tooltip = u"How each Line's number changes the value";
        variant.choices = {u"Set by line number", u"Add by line number"};
        variant.index = m_listMode - PasteMultiply;
        break;
    case Change::Gradient:
        variant.tooltip = u"What the gradient runs across";
        variant.choices = {u"Across characters, increasing", u"Across characters, decreasing",
                           u"Across lines, increasing", u"Across lines, decreasing"};
        variant.index = m_listMode - PasteGradientTextIncrease;
        break;
    }
    if (!variant.choices.empty())
        out.push_back(std::move(variant));
    return out;
}

bool AllTagsTool::setOption(const std::string &name, int value, VisualHost &host)
{
    const auto count = static_cast<int>(tags(host).size());
    const int before = m_listMode, beforeTag = m_listSelection;
    if (name == "tag") {
        if (value < 0 || value >= count)
            return false;
        m_listSelection = value;
        m_listMode = tags(host)[static_cast<std::size_t>(value)].tagMode;
    } else if (name == "changeOption") {
        if (value < 0 || value >= static_cast<int>(allTagsChangeOptions().size()))
            return false;
        m_listMode = value;
    } else if (name == "change") {
        if (value < 0 || value > static_cast<int>(Change::Gradient))
            return false;
        const auto to = static_cast<Change>(value);
        if (to != changeOf(m_listMode))
            m_listMode = to == Change::Set ? PasteInsert
                         : to == Change::Add ? PasteAdd
                         : to == Change::Multiply ? PasteMultiply
                                                  : PasteGradientTextIncrease;
    } else if (name == "variant") {
        switch (changeOf(m_listMode)) {
        case Change::Set:
            if (value < 0 || value > 2)
                return false;
            if (const auto *own = host.allTagsSettings(); (!own || own->empty()) && beforeTag < count)
                m_ownTags[static_cast<std::size_t>(beforeTag)].mode = static_cast<unsigned char>(value);
            m_last.reset();
            break;
        case Change::Multiply:
            if (value < 0 || value > 1)
                return false;
            m_listMode = PasteMultiply + value;
            break;
        case Change::Gradient:
            if (value < 0 || value > 3)
                return false;
            m_listMode = PasteGradientTextIncrease + value;
            break;
        case Change::Add:
            return false;
        }
    } else {
        return false;
    }
    if (m_listSelection != beforeTag || m_listMode != before)
        forgetValues();
    if (m_listMode != before || m_listSelection != beforeTag)
        m_last.reset();
    changeTool(toggled(), false, host);
    return true;
}

void AllTagsTool::changeTool(int tool, bool blockSetCurVisual, VisualHost &host)
{
    // MUST KILL: m_lastTool is never assigned
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
    m_base = m_actualTag.values;
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

bool AllTagsTool::several(const VisualHost &host) const
{
    return severalLines(targetsFor(host), host.activeLine());
}

std::vector<core::LineId> AllTagsTool::targetsFor(const VisualHost &host) const
{
    return m_forcedTargets ? *m_forcedTargets : host.batchTargets();
}

std::vector<core::LineId> AllTagsTool::editedTargets(const VisualHost &host) const
{
    auto targets = targetsFor(host);
    if (m_forcedTargets || !severalLines(targets, host.activeLine()))
        return targets;
    if (const auto shown = host.shownLines())
        std::erase_if(targets, [&](core::LineId id) { return !shown(id); });
    return targets;
}

bool AllTagsTool::blocked(const VisualHost &host) const
{
    const auto *session = host.session();
    const auto active = host.activeLine();
    if (!session || !active)
        return false;
    const auto draft = session->draftRecord();
    const core::LineRecord *line = draft && draft->id == *active ? &*draft : findLine(session->document(), *active);
    return line && lineWarning(Family::Hydra, *line, host.videoTimeMs()) != LineWarning::None;
}

float AllTagsTool::diffValue(const Slider &slider) const
{
    return slider.holding || m_mode == 2 ? slider.thumbValue - slider.firstThumbValue : 0;
}

u16 AllTagsTool::selectedTagIn(FindData *result) const
{
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
        const float val1 = (m_mode >= PasteMultiply) ? values[0] + (m_multiplyCounter * valuediff) : value;
        const float val2 = (m_mode >= PasteMultiply) ? values[1] + (m_multiplyCounter * valuediff2) : value2;
        const float val3 = (m_mode >= PasteMultiply) ? values[2] + (m_multiplyCounter * valuediff3) : value3;
        if (m_tagMode & IsHexAlpha) {
            strval = u"&H" + hexByte(mid3(0, static_cast<int>(val1 + 0.5), 255)) + u"&";
        } else if (m_tagMode & IsHexColour) {
            // ASS colours are BGR.
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
        const u16 inner = midText(curValue, 1, hasLastBracket ? curValue.size() - 2 : curValue.size() - 1);
        strval = u"(";
        int counter = 0;
        for (const u16 &raw : strtok(inner, u',')) {
            const u16 token = trim(raw);
            double val = 0;
            if (toDouble(token, val)) {
                // MUST KILL: counter % 2 is never 2, so valuediff3 is dead
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
            hasEndBracked = false;
        }
        if (hasEndBracked)
            strval += u")";
    }
    return strval;
}

long AllTagsTool::changeVisualEditor(u16 &txt)
{
    // One-Line path; returns the editor's new caret position.
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
    // Several-Line path.
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
    if (host.gesture())
        return true;
    const auto targets = targetsFor(host);
    if (targets.empty())
        return false;
    const bool many = severalLines(targets, host.activeLine());
    m_targets = editedTargets(host);
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
    if (host.gesture() && !host.gesture()->changesAnyLine())
        host.cancelGesture();
    const bool committed = !host.gesture() || host.commitGesture().has_value();
    m_editing.reset();
    if (!committed)
        reset(host);
    return committed;
}

bool AllTagsTool::setVisual(bool dummy, VisualHost &host)
{
    if (!beginEdit(host))
        return false;
    Gesture *g = host.gesture();
    bool done = true;
    if (!m_editing) {
        const Context ctx = context(host);
        for (const auto &id : g->targets()) {
            const core::LineRecord &line = g->before(id);
            u16 txt = transform::lineText(line);
            changeVisualLine(txt, g->targets().size());
            g->stage(id, u8(txt), transform::editsTranslation(line));
        }
        if (!dummy)
            done = finishEdit(host);
    } else if (dummy) {
        u16 txt = m_replaceTagsInCursorPosition ? m_editorText : m_currentLineText;
        const long pos = changeVisualEditor(txt);
        m_editorText = txt;
        m_find.setSelection(pos, pos);
        g->stage(*m_editing, u8(txt), m_editorIsTranslation);
    } else {
        // MUST KILL: text and caret copied before the commit, which may reload the editor
        const u16 written = m_editorText;
        const auto [selFrom, selTo] = m_find.selection();
        done = finishEdit(host);
        if (done) {
            m_currentLineText = written;
            m_find.setSelection(selFrom, selTo);
            host.setEditorSelection(selFrom, selTo);
        }
    }
    host.toolChanged();
    return done;
}

void AllTagsTool::legacyPointer(const Pointer &event, VisualHost &host)
{
    takeView(host.view());
    if (event.kind == Pointer::Kind::DoubleClick || event.kind == Pointer::Kind::Enter)
        return; // Qt sends a press before the double click
    if (m_mode >= PasteMultiply)
        m_multiplyCounter = (m_mode == PasteGradientTextDecrease || m_mode == PasteGradientLineDecrease) ? 1.f : 0.f;
    const float y = static_cast<float>(event.y);
    const bool rightDown = event.kind == Pointer::Kind::Press && event.button == Pointer::Button::Right;
    const bool rightUp = event.kind == Pointer::Kind::Release && event.button == Pointer::Button::Right;
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
    // MUST KILL: numOfValues re-read per slider; the first one's Shift+wheel can change the tag
    std::array<bool, 4> wheeled{};
    for (int i = 0; i < m_actualTag.numOfValues && i < 4; i++)
        wheeled[static_cast<std::size_t>(i)] = sliderMouse(i, event, host);
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

    if (event.kind == Pointer::Kind::Wheel && event.wheelSteps != 0) {
        if (shift) {
            const int step = event.wheelSteps;
            const int count = static_cast<int>(tags(host).size());
            m_currentTag -= step;
            if (m_currentTag < 0)
                m_currentTag = count - 1;
            else if (m_currentTag >= count)
                m_currentTag = 0;
            int tool = m_mode << 20;
            tool += m_currentTag;
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
            // MUST KILL: holding doubles as "use this slider's value" in diffValue
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
    if (event.kind == Pointer::Kind::Leave && s.thumbState != 0) {
        s.thumbState = 0;
        host.toolChanged();
    }
    if (!s.holding) {
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

bool AllTagsTool::legacyKey(const Key &event, VisualHost &host)
{
    if (event.release)
        return false;
    if (!(m_actualTag.tag == u"fad" || m_tagMode & IsTAnimation))
        return false;
    // MUST KILL: raw Line editor hotkey ids 3015/3016, no named constants
    const bool hkeystart = keyMatchesAccelerator(event, host.editorHotkey(3015));
    const bool hkeyend = keyMatchesAccelerator(event, host.editorHotkey(3016));
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

int AllTagsTool::valueCount() const
{
    return std::clamp(static_cast<int>(m_actualTag.numOfValues), 1, 4);
}

std::u16string AllTagsTool::valueText(float value) const
{
    if (value == 0.f)
        value = 0.f; // no "-0"
    return getfloat(value, m_floatFormat);
}

void AllTagsTool::forgetValues()
{
    m_fields = Fields();
    m_sample.reset();
    m_drag.reset();
    m_marked.clear();
}

void AllTagsTool::remember(const Fields &f, const std::array<float, 4> &base, std::pair<long, long> caret,
                           VisualHost &host)
{
    const EditSession *session = host.session();
    m_last = Last{m_currentTag, m_mode, f, base, m_targets, caret, session->revision(), host.activeLine(), session};
}

void AllTagsTool::readBase(VisualHost &host)
{
    const auto &definitions = tags(host);
    if (m_currentTag < 0 || m_currentTag >= static_cast<int>(definitions.size()))
        return;
    m_actualTag = definitions[static_cast<std::size_t>(m_currentTag)];
    checkTag();
    const Context ctx = context(host);
    m_currentLineText = ctx.editorText;
    findTagValues(ctx);
    m_base = m_actualTag.values;
}

namespace {

// A value name is its role ("value", "by", "start", "perLine", "from", "to") plus the index.
bool splitName(const std::string &name, std::string &role, int &index)
{
    if (name.empty() || name.back() < '0' || name.back() > '3')
        return false;
    role = name.substr(0, name.size() - 1);
    index = name.back() - '0';
    return true;
}

std::vector<std::string> rolesOf(int mode)
{
    switch (AllTagsTool::changeOf(mode)) {
    case AllTagsTool::Change::Set:
        return {"value"};
    case AllTagsTool::Change::Add:
        return {"by"};
    case AllTagsTool::Change::Multiply:
        return mode == PasteMultiply ? std::vector<std::string>{"start", "perLine"} : std::vector<std::string>{"perLine"};
    case AllTagsTool::Change::Gradient:
        return {"from", "to"};
    }
    return {};
}

}

float AllTagsTool::field(const Fields &f, const std::string &name, int mode) const
{
    (void)mode;
    std::string role;
    int i = 0;
    if (!splitName(name, role, i))
        return 0;
    const auto at = static_cast<std::size_t>(i);
    const float base = m_base[at];
    if (role == "value")
        return f.set[at].value_or(base);
    if (role == "by")
        return f.by[at];
    if (role == "perLine")
        return f.perLine[at];
    if (role == "start" || role == "from")
        return f.start[at].value_or(base);
    if (role == "to")
        return f.to[at].value_or(f.start[at].value_or(base));
    return 0;
}

bool AllTagsTool::putField(Fields &f, const std::string &name, float value) const
{
    std::string role;
    int i = 0;
    if (!splitName(name, role, i) || i >= valueCount())
        return false;
    const auto roles = rolesOf(m_mode);
    if (std::find(roles.begin(), roles.end(), role) == roles.end())
        return false;
    const auto at = static_cast<std::size_t>(i);
    const float inRange = std::clamp(value, std::min(m_actualTag.rangeMin, m_actualTag.rangeMax),
                                     std::max(m_actualTag.rangeMin, m_actualTag.rangeMax));
    if (role == "value")
        f.set[at] = inRange;
    else if (role == "by")
        f.by[at] = value;
    else if (role == "perLine")
        f.perLine[at] = value;
    else if (role == "start" || role == "from")
        f.start[at] = inRange;
    else if (role == "to")
        f.to[at] = inRange;
    return true;
}

bool AllTagsTool::isMove(const std::string &name) const
{
    return name.starts_with("by") || (name.starts_with("perLine") && m_mode == PasteMultiplyPlus);
}

std::string AllTagsTool::markedName() const
{
    std::string role;
    int i = 0;
    const auto roles = rolesOf(m_mode);
    if (splitName(m_marked, role, i) && i < valueCount() && std::find(roles.begin(), roles.end(), role) != roles.end())
        return m_marked;
    return roles.back() + "0";
}

std::vector<ToolValue> AllTagsTool::valuesOf(const Fields &f, int mode, const std::array<float, 4> &base,
                                              const std::string &marked, bool editable) const
{
    const int n = valueCount();
    std::vector<u16> labels;
    u16 unit;
    if (m_tagMode & IsHexColour)
        labels = {u"R", u"G", u"B"};
    else if (m_actualTag.tag == u"pos")
        labels = {u"X", u"Y"};
    else if (m_actualTag.tag == u"fad")
        labels = {u"In", u"Out"}, unit = u"ms";
    else if (m_tagMode & IsTAnimation)
        labels = {u"Start", u"End"}, unit = u"ms";
    else
        labels = {u"1", u"2", u"3", u"4"};
    static const std::map<std::string, u16> roleLabels{{"value", u"Value"}, {"by", u"By"},     {"start", u"Start"},
                                                       {"perLine", u"Per line"}, {"from", u"From"}, {"to", u"To"}};
    Fields shown = f;
    std::vector<ToolValue> out;
    for (const std::string &role : rolesOf(mode)) {
        std::array<float, 4> group{};
        for (int i = 0; i < n; ++i) {
            const auto at = static_cast<std::size_t>(i);
            group[at] = role == "value"     ? shown.set[at].value_or(base[at])
                        : role == "by"      ? shown.by[at]
                        : role == "perLine" ? shown.perLine[at]
                        : role == "to"      ? shown.to[at].value_or(shown.start[at].value_or(base[at]))
                                            : shown.start[at].value_or(base[at]);
        }
        for (int i = 0; i < n; ++i) {
            const auto at = static_cast<std::size_t>(i);
            ToolValue v;
            v.name = role + std::to_string(i);
            v.label = n == 1 ? roleLabels.at(role) : labels[std::min<std::size_t>(at, labels.size() - 1)];
            v.group = n == 1 || role == "value" ? u16() : roleLabels.at(role);
            v.text = valueText(group[at]);
            v.editable = editable;
            v.unit = unit;
            v.step = m_actualTag.step > 0 ? m_actualTag.step : 1;
            v.marked = v.name == marked;
            if ((m_tagMode & IsHexColour) && i == 0 && role != "by" && role != "perLine") {
                const auto byte = [&](int c) {
                    return static_cast<std::uint32_t>(mid3(0, static_cast<int>(group[static_cast<std::size_t>(c)] + 0.5f), 255));
                };
                v.swatch = 0xFF000000u | (byte(0) << 16) | (byte(1) << 8) | byte(2);
            }
            out.push_back(std::move(v));
        }
    }
    return out;
}

std::vector<ToolValue> AllTagsTool::values(const VisualHost &host) const
{
    (void)host;
    if (m_legacySliders)
        return {};
    return valuesOf(m_sample ? *m_sample : m_fields, m_mode, m_base, markedName(), true);
}

void AllTagsTool::load(const Fields &f)
{
    for (int i = 0; i < valueCount(); ++i) {
        const auto at = static_cast<std::size_t>(i);
        Slider &s = m_slider[at];
        const float base = m_base[at];
        s.holding = true;
        switch (m_mode) {
        case PasteInsert:
            m_actualTag.values[at] = s.firstThumbValue = s.thumbValue = f.set[at].value_or(base);
            break;
        case PasteAdd:
            m_actualTag.values[at] = s.firstThumbValue = base;
            s.thumbValue = base + f.by[at];
            break;
        case PasteMultiply:
            m_actualTag.values[at] = f.start[at].value_or(base);
            s.firstThumbValue = 0;
            s.thumbValue = f.perLine[at];
            break;
        case PasteMultiplyPlus:
            m_actualTag.values[at] = base;
            s.firstThumbValue = 0;
            s.thumbValue = f.perLine[at];
            break;
        default: {
            const float from = f.start[at].value_or(base);
            m_actualTag.values[at] = from;
            s.firstThumbValue = 0;
            s.thumbValue = f.to[at].value_or(from) - from;
            break;
        }
        }
    }
}

bool AllTagsTool::run(const Fields &f, bool commit, VisualHost &host)
{
    const auto *session = host.session();
    const std::uint64_t before = session ? session->revision() : 0;
    load(f);
    const auto counter = [&] {
        if (m_mode >= PasteMultiply)
            m_multiplyCounter = (m_mode == PasteGradientTextDecrease || m_mode == PasteGradientLineDecrease) ? 1.f : 0.f;
    };
    bool done = false;
    counter();
    if (!commit) {
        done = setVisual(true, host);
    } else if (several(host)) {
        done = setVisual(false, host);
    } else {
        done = setVisual(true, host);
        counter();
        done = setVisual(false, host) && done;
    }
    for (Slider &s : m_slider)
        s.holding = false;
    m_actualTag.values = m_base;
    if (!commit)
        return done;
    session = host.session();
    return done && session && session->revision() != before;
}

bool AllTagsTool::setValue(const std::string &name, const std::u16string &text, VisualHost &host, ValueEdit edit)
{
    if (m_legacySliders || host.gesture() && !m_sample || !host.view().hasVideo() || blocked(host))
        return false;
    double parsed = 0;
    if (!parseValue(text, parsed))
        return false;
    Fields f = m_sample ? *m_sample : m_fields;
    if (!putField(f, name, static_cast<float>(parsed)))
        return false;
    if (edit == ValueEdit::Sample) {
        m_sample = f;
        if (!run(f, false, host)) {
            m_sample.reset();
            return false;
        }
        return true;
    }
    m_sample.reset();
    const auto base = m_base;
    const auto caret = host.editorSelection();
    const bool done = run(f, true, host);
    if (host.gesture())
        host.cancelGesture();
    if (done) {
        m_fields = f;
        for (auto &v : m_fields.set)
            v.reset();
        readBase(host);
        remember(f, base, caret, host);
    }
    host.toolChanged();
    return done;
}

bool AllTagsTool::markValue(const std::string &name, VisualHost &host)
{
    std::string role;
    int i = 0;
    const auto roles = rolesOf(m_mode);
    if (!splitName(name, role, i) || i >= valueCount() || std::find(roles.begin(), roles.end(), role) == roles.end())
        return false;
    m_marked = name;
    host.toolChanged();
    return true;
}

void AllTagsTool::targetsChanged(VisualHost &host)
{
    m_outlines.reset();
    host.toolChanged();
}

void AllTagsTool::dropDrag(VisualHost &host)
{
    m_drag.reset();
    m_sample.reset();
    if (host.gesture())
        host.cancelGesture();
}

std::optional<LastChange> AllTagsTool::lastChange(const VisualHost &host) const
{
    if (!m_last || m_legacySliders)
        return std::nullopt;
    const auto &definitions = tags(host);
    LastChange out;
    out.tag = m_last->tag < static_cast<int>(definitions.size()) ? definitions[static_cast<std::size_t>(m_last->tag)].name
                                                                   : u16();
    out.change = static_cast<int>(changeOf(m_last->mode));
    out.lines = static_cast<int>(m_last->targets.size());
    out.values = valuesOf(m_last->fields, m_last->mode, m_last->base, std::string(), true);
    return out;
}

bool AllTagsTool::editLastChange(const std::string &name, const std::u16string &text, VisualHost &host)
{
    if (!m_last || m_last->tag != m_currentTag || m_last->mode != m_mode || !host.view().hasVideo() || blocked(host))
        return false;
    if (host.session() != m_last->session) {
        m_last.reset();
        host.toolChanged();
        return false;
    }
    double parsed = 0;
    if (!parseValue(text, parsed))
        return false;
    Last last = *m_last;
    if (!putField(last.fields, name, static_cast<float>(parsed)))
        return false;
    if (!host.undoStep(last.revision)) {
        m_last.reset();
        host.toolChanged();
        return false;
    }
    m_forcedTargets = last.targets;
    m_find.setSeveralLines(several(host));
    m_find.setSelection(last.caret.first, last.caret.second);
    readBase(host);
    last.base = m_base;
    const bool done = run(last.fields, true, host);
    if (host.gesture())
        host.cancelGesture();
    m_forcedTargets.reset();
    readBase(host);
    if (done) {
        last.revision = host.session()->revision();
        m_last = last;
        m_fields = last.fields;
        for (auto &v : m_fields.set)
            v.reset();
    } else {
        m_last.reset();
    }
    host.toolChanged();
    return done;
}

void AllTagsTool::pointer(const Pointer &event, VisualHost &host)
{
    if (m_legacySliders) {
        legacyPointer(event, host);
        return;
    }
    takeView(host.view());
    if (event.kind == Pointer::Kind::Leave || event.kind == Pointer::Kind::DoubleClick)
        return;
    host.setPointerShape(host.view().videoRect().contains(event.x, event.y) || m_drag ? PointerShape::SizeWE
                                                                                     : PointerShape::Arrow);
    if (event.kind == Pointer::Kind::Wheel && event.wheelSteps != 0) {
        if (host.gesture())
            return;
        if (event.shift) {
            const auto order = listOrder(host);
            if (order.empty())
                return;
            auto at = std::find(order.begin(), order.end(), m_listSelection);
            int pos = at == order.end() ? 0 : static_cast<int>(at - order.begin());
            const int count = static_cast<int>(order.size());
            pos = ((pos - event.wheelSteps) % count + count) % count;
            (void)setOption("tag", order[static_cast<std::size_t>(pos)], host);
            return;
        }
        const std::string name = markedName();
        const bool move = isMove(name);
        Fields f = move ? Fields() : m_fields;
        const float step = m_actualTag.step > 0 ? m_actualTag.step : 1;
        if (!putField(f, name, (move ? 0.f : field(f, name, m_mode)) + (event.wheelSteps > 0 ? step : -step)))
            return;
        const auto base = m_base;
        const auto caret = host.editorSelection();
        if (run(f, true, host)) {
            if (!move)
                m_fields = f;
            for (auto &v : m_fields.set)
                v.reset();
            readBase(host);
            remember(f, base, caret, host);
        }
        host.toolChanged();
        return;
    }
    const bool leftDown = event.kind == Pointer::Kind::Press && event.button == Pointer::Button::Left;
    const bool leftUp = event.kind == Pointer::Kind::Release && event.button == Pointer::Button::Left;
    if (leftDown && !host.gesture() && host.view().videoRect().contains(event.x, event.y)) {
        Drag d;
        d.x = d.atX = event.x;
        d.y = d.atY = event.y;
        d.name = markedName();
        d.from = isMove(d.name) ? 0.f : field(m_fields, d.name, m_mode);
        m_drag = d;
        return;
    }
    if (!m_drag)
        return;
    if (event.kind == Pointer::Kind::Move && event.leftDown) {
        const int dx = event.x - m_drag->x;
        if (!m_drag->active && std::abs(dx) < 3)
            return;
        m_drag->active = true;
        m_drag->atX = event.x;
        m_drag->atY = event.y;
        const float step = (m_actualTag.step > 0 ? m_actualTag.step : 1) * (event.shift ? 0.1f : 1.f);
        Fields f = isMove(m_drag->name) ? Fields() : m_fields;
        if (!putField(f, m_drag->name, m_drag->from + static_cast<float>(dx) * step))
            return;
        m_sample = f;
        if (!run(f, false, host)) {
            dropDrag(host);
            reset(host);
        }
        host.toolChanged();
        return;
    }
    if (leftUp) {
        const bool active = m_drag->active && m_sample;
        if (!active) {
            m_drag.reset();
            return;
        }
        const Fields f = *m_sample;
        const auto base = m_base;
        const auto caret = host.editorSelection();
        const bool done = run(f, true, host);
        if (host.gesture())
            host.cancelGesture();
        m_drag.reset();
        m_sample.reset();
        if (done) {
            m_fields = f;
            for (auto &v : m_fields.set)
                v.reset();
            readBase(host);
            remember(f, base, caret, host);
        }
        host.toolChanged();
    }
}

bool AllTagsTool::key(const Key &event, VisualHost &host)
{
    if (m_legacySliders)
        return legacyKey(event, host);
    if (event.release)
        return false;
    if (!(m_actualTag.tag == u"fad" || m_tagMode & IsTAnimation))
        return false;
    const bool hkeystart = keyMatchesAccelerator(event, host.editorHotkey(3015));
    const bool hkeyend = keyMatchesAccelerator(event, host.editorHotkey(3016));
    if (!(hkeystart || hkeyend))
        return false;
    const Context ctx = context(host);
    if (!host.view().hasVideo() || !ctx.active || host.gesture()) {
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
    const int mode = m_mode;
    const bool inCursor = m_replaceTagsInCursorPosition;
    m_mode = PasteInsert;
    m_replaceTagsInCursorPosition = true;
    Fields f;
    f.set[hkeystart ? 0 : 1] = static_cast<float>(diff);
    const bool done = run(f, true, host);
    if (host.gesture())
        host.cancelGesture();
    m_mode = mode;
    m_replaceTagsInCursorPosition = inCursor;
    if (done) {
        readBase(host);
        m_last.reset();
    }
    host.toolChanged();
    return true;
}

void AllTagsTool::drawSlider(Overlay &out, const VisualHost &host, const Slider &s) const
{
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
    Overlay out;
    out.unit = unitOf(host);
    if (m_legacySliders) {
        for (int i = 0; i < m_actualTag.numOfValues && i < 4; i++)
            drawSlider(out, host, m_slider[static_cast<std::size_t>(i)]);
        return out;
    }
    const auto targets = editedTargets(host);
    const auto *session = host.session();
    const VideoView &view = host.view();
    const std::int64_t time = host.videoTimeMs();
    const auto draft = session ? session->draftRecord() : std::nullopt;
    std::u8string draftKey;
    if (draft)
        for (const std::u8string *field : {&draft->text, &draft->translation, &draft->style, &draft->start.lexeme,
                                           &draft->end.lexeme,
                                           &draft->layer.lexeme, &draft->marginLeft.lexeme, &draft->marginRight.lexeme,
                                           &draft->marginVertical.lexeme})
            draftKey += *field + u8'\x01';
    // MUST KILL: const_cast because VisualHost has no const gesture()
    const Gesture *gesture = const_cast<VisualHost &>(host).gesture();
    const bool fresh = !gesture && m_outlines && session && m_outlines->revision == session->revision() &&
                       m_outlines->time == time && m_outlines->targets == targets && m_outlines->draft == draftKey &&
                       m_outlines->coeffW == view.coeffW() && m_outlines->coeffH == view.coeffH() &&
                       m_outlines->zoomMove == view.zoomMove() && m_outlines->zoomScale == view.zoomScale();
    if (!fresh && session) {
        Outlines o;
        o.revision = session->revision();
        o.time = time;
        o.targets = targets;
        o.draft = draftKey;
        o.coeffW = view.coeffW();
        o.coeffH = view.coeffH();
        o.zoomMove = view.zoomMove();
        o.zoomScale = view.zoomScale();
        const Context ctx = context(host);
        const auto toView = [&](PointF p) {
            return PointF{((p.x / o.coeffW) - o.zoomMove.x) * o.zoomScale.x,
                          ((p.y / o.coeffH) - o.zoomMove.y) * o.zoomScale.y};
        };
        std::map<core::LineId, std::size_t> at;
        for (std::size_t i = 0; i < targets.size(); ++i)
            at.emplace(targets[i], i);
        std::vector<const core::LineRecord *> records(targets.size(), nullptr);
        for (const core::LineRecord *record : session->document().lines())
            if (const auto it = at.find(record->id); it != at.end())
                records[it->second] = record;
        constexpr std::size_t kMostOutlined = 64;
        std::size_t outlined = 0;
        for (std::size_t i = 0; i < targets.size() && outlined < kMostOutlined; ++i) {
            const core::LineRecord *found = draft && draft->id == targets[i] ? &*draft : records[i];
            if (!found)
                continue;
            core::LineRecord staged;
            const core::LineRecord *line = found;
            if (gesture) {
                const auto text = gesture->staged(targets[i]), translation = gesture->staged(targets[i], true);
                if (text || translation) {
                    staged = *found;
                    staged.text = text.value_or(staged.text);
                    staged.translation = translation.value_or(staged.translation);
                    line = &staged;
                }
            }
            if (lineWarning(Family::Hydra, *line, time) != LineWarning::None)
                continue;
            const auto box = lineBox(ctx, *line);
            if (!box)
                continue;
            ++outlined;
            const PointF a = toView(box->first), b = toView(box->second);
            o.overlay.polygons.push_back({{{a.x, a.y}, {b.x, a.y}, {b.x, b.y}, {a.x, b.y}}, 0, kOutline});
            if (targets.size() > 1) {
                const u16 number = ascii(std::to_string(i + 1));
                const auto [w, h] = host.measureLabel(number);
                const int x = static_cast<int>(a.x);
                const int y = static_cast<int>(a.y) - h;
                o.overlay.texts.push_back({IntRect{x, y, x + w, y + h}, number, kOutline, true, 0});
            }
        }
        if (gesture)
            out = std::move(o.overlay);
        else
            m_outlines = std::move(o);
    }
    if (m_outlines && !gesture)
        out = m_outlines->overlay;
    if (m_drag && m_drag->active && m_sample) {
        const u16 text = m_actualTag.name + u"  " + valueText(m_drag->from) + u" \u2192 " +
                         valueText(field(*m_sample, m_drag->name, m_mode));
        const auto [w, h] = host.measureLabel(text);
        const float u = unitOf(host);
        const int x = m_drag->atX + static_cast<int>(14 * u), y = m_drag->atY - h - static_cast<int>(10 * u);
        out.texts.push_back({IntRect{x, y, x + w, y + h}, text, 0xFFFFFFFF, true, 0});
    }
    return out;
}

AllTagsSetPreview allTagsSetPreview(const AllTagsSetting &tag, std::u16string_view line)
{
    AllTagsSetPreview out{{}, allTagsSetText(tag), {}};
    transform::TagFind find;
    find.findTag(tag.tag + kValuePattern, line, tag.mode);
    const FindData &found = find.result();
    const auto x = static_cast<std::size_t>(std::clamp<long>(found.x, 0, static_cast<long>(line.size())));
    if (line.empty() || !found.inBracket) {
        out.before = u16(line.substr(0, x)) + u"{";
        out.after = u"}" + u16(line.substr(x));
        return out;
    }
    const auto end = found.x < found.y ? std::min(static_cast<std::size_t>(found.y) + 1, line.size()) : x;
    out.before = u16(line.substr(0, x));
    out.after = u16(line.substr(end));
    return out;
}

}
