#include "hikari/application/visual_transform.h"

#include "hikari/application/automation_services.h"
#include "hikari/application/grid_clipboard.h"
#include "hikari/core/legacy_regex.h"
#include "hikari/core/tag_commands.h"
#include "hikari/core/text_projection.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

// Built without floating-point contraction, as visual_view.cpp: legacy's
// float steps must round as its build did.

namespace hikari::application::visual::transform {

namespace {

using u16 = std::u16string;
using u16v = std::u16string_view;

u16 ascii(std::string_view s)
{
    return u16(s.begin(), s.end());
}

// wxString::Trim(true) and Trim(false): wxSafeIsspace characters.
bool isSpace(char16_t c)
{
    return c == u' ' || c == u'\t' || c == u'\n' || c == u'\r' || c == u'\v' || c == u'\f';
}

u16 trimmed(u16v s)
{
    std::size_t b = 0, e = s.size();
    while (e > b && isSpace(s[e - 1]))
        --e;
    while (b < e && isSpace(s[b]))
        ++b;
    return u16(s.substr(b, e - b));
}

// wxString::Mid(first, count): clamped to the text.
u16 mid(u16v s, std::size_t first, std::size_t count = u16v::npos)
{
    if (first >= s.size())
        return {};
    return u16(s.substr(first, count));
}

// wxString::BeforeFirst(ch, &rest) (wx 3 string.cpp): `rest` takes the text
// after `ch` before the result is cut from the (possibly changed) string, so
// a call whose rest is the string itself returns the tail's first characters.
u16 beforeFirst(u16 &self, char16_t ch, u16 *rest)
{
    const auto at = self.find(ch);
    std::size_t pos = at == u16::npos ? self.size() : at;
    if (rest) {
        if (at == u16::npos)
            rest->clear();
        else
            rest->assign(self, pos + 1, u16::npos);
    }
    return self.substr(0, std::min(pos, self.size()));
}

std::size_t freq(u16v s, char16_t ch)
{
    return static_cast<std::size_t>(std::count(s.begin(), s.end(), ch));
}

// wxStringTokenizer(text, delims) in its default mode: whitespace delimiters
// skip empty tokens (wxTOKEN_STRTOK), others return the empty ones between
// delimiters but not a trailing one (wxTOKEN_RET_EMPTY).
std::vector<u16> tokens(u16v text, char16_t delim, bool strtok)
{
    std::vector<u16> out;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto at = text.find(delim, start);
        const std::size_t end = at == u16v::npos ? text.size() : at;
        u16 token(text.substr(start, end - start));
        if (at == u16v::npos) {
            if (!token.empty())
                out.push_back(std::move(token));
            break;
        }
        if (!strtok || !token.empty())
            out.push_back(std::move(token));
        start = end + 1;
    }
    return out;
}

const core::LegacyRegex &posRegex()
{
    static const core::LegacyRegex re(u"\\\\(pos|move)\\(([^\\)]+)\\)", core::LegacyRegex::Advanced);
    return re;
}

const core::LegacyRegex &anRegex()
{
    static const core::LegacyRegex re(u"\\\\an([0-9]+)", core::LegacyRegex::Advanced);
    return re;
}

u16 group(const core::LegacyRegex &re, u16v text, std::size_t index)
{
    const auto m = re.match(index);
    if (!m)
        return {};
    return u16(text.substr(m->first, m->second));
}

// Styles::Styles() (styles.cpp:249-275).
core::StyleValues defaultStyle()
{
    core::StyleValues s;
    s.name = u8"Default";
    s.fontname = u8"Garamond";
    s.fontsize = u8"40";
    s.primary = core::legacy::colour(u8"&H00FFFFFF&");
    s.secondary = core::legacy::colour(u8"&H00000000&");
    s.outline = core::legacy::colour(u8"&H00FF0000&");
    s.back = core::legacy::colour(u8"&H00000000&");
    s.scaleX = u8"100";
    s.scaleY = u8"100";
    s.spacing = u8"0";
    s.angle = u8"0";
    s.outlineWidth = u8"2";
    s.shadow = u8"2";
    s.alignment = u8"2";
    s.marginLeft = s.marginRight = s.marginVertical = u8"20";
    s.encoding = u8"1";
    s.complete = true;
    return s;
}

u16 u16of(const std::u8string &s)
{
    return core::toUtf16(s);
}

int alignmentOf(const core::StyleValues &style, u16v text)
{
    int an = atoi(u16of(style.alignment));
    if (anRegex().matches(text))
        an = atoi(group(anRegex(), text, 1));
    return an;
}

// GetLineTextExtents (UtilsWindows.cpp:233-...) through the text measure: an
// empty text is 0 by 0 without measuring.
bool lineTextExtents(const Context &context, u16v text, const core::StyleValues &style, float &width, float &height,
                     float &descent, float &extlead)
{
    if (text.empty()) {
        width = height = descent = extlead = 0;
        return true;
    }
    if (!context.measure)
        return false;
    std::vector<std::string> fields;
    for (const auto &f : core::legacy::styleRawFields(style))
        fields.emplace_back(f.begin(), f.end());
    const std::u8string utf8 = core::toUtf8(text);
    const auto e = context.measure->measure(fields, std::string(utf8.begin(), utf8.end()));
    if (!e)
        return false;
    width = static_cast<float>(e->width);
    height = static_cast<float>(e->height);
    descent = static_cast<float>(e->descent);
    extlead = static_cast<float>(e->externalLeading);
    return true;
}

// Dialogue::ParseTags(tags, n, plainText = true) (SubsDialogue.cpp:1086-1161),
// as grid_split.cpp's copy, with the drawing's value trimmed in place.
struct Tag {
    u16 name, value;
};
std::vector<Tag> parseTags(u16v txt, const std::vector<u16v> &names)
{
    std::vector<Tag> out;
    const std::size_t len = txt.size();
    std::size_t pos = 0, plainStart = 0;
    bool tagsBlock = false, drawing = false;
    if (len < 1)
        return out;
    while (pos < len) {
        const char16_t ch = txt[pos];
        if (ch == u'}') {
            tagsBlock = false;
            plainStart = pos + 1;
        } else if (ch == u'{' || pos >= len - 1) {
            tagsBlock = true;
            if (pos >= len - 1)
                ++pos;
            // SubString(plainStart, pos - 1).
            if (plainStart + 1 <= pos)
                out.push_back({drawing ? u"pvector" : u"plain", u16(txt.substr(plainStart, pos - plainStart))});
        } else if (tagsBlock && ch == u'\\') {
            ++pos;
            const std::size_t slash = txt.find(u'\\', pos), bracket = txt.find(u'}', pos);
            const std::size_t tagEnd = slash == u16v::npos && bracket == u16v::npos ? len
                                       : slash == u16v::npos                     ? bracket
                                       : bracket == u16v::npos                   ? slash
                                                                                 : std::min(slash, bracket);
            u16 tag = tagEnd >= pos ? u16(txt.substr(pos, tagEnd - pos)) : u16();
            if (!tag.empty() && tag.back() == u')')
                tag.pop_back();
            for (const u16v name : names) {
                if (tag.size() > name.size() && u16v(tag).substr(0, name.size()) == name) {
                    const char16_t first = tag[name.size()];
                    if (first == u'(' || (first >= u'0' && first <= u'9') || name == u"fn" || first == u'.' ||
                        first == u'-' || first == u'+') {
                        u16 value = tag.substr(name.size());
                        if (name == u"p") {
                            value = trimmed(value);
                            drawing = value != u"0";
                        } else if (first == u'(') {
                            // After('(').BeforeFirst(')')
                            const auto open = value.find(u'(');
                            u16 after = open == u16::npos ? u16() : value.substr(open + 1);
                            const auto close = after.find(u')');
                            value = close == u16::npos ? after : after.substr(0, close);
                        } else if (name != u"fn") {
                            double ignored = 0;
                            if (!toDouble(value, ignored)) {
                                u16 number;
                                for (const char16_t c : value) {
                                    if (!((c >= u'0' && c <= u'9') || c == u'.' || c == u'-' || c == u'+'))
                                        break;
                                    number += c;
                                }
                                value = number;
                            }
                        }
                        out.push_back({u16(name), value});
                        pos = tagEnd - 1;
                        break;
                    }
                }
            }
        }
        ++pos;
    }
    return out;
}

// TagFindReplace::TagValueToStyle (TagFindReplace.cpp:510-553) for the tags
// GetTextSize hands it.
bool tagValueToStyle(core::StyleValues &style, u16v tag, const u16 &value)
{
    const std::u8string v = core::toUtf8(value);
    if (tag == u"fs")
        style.fontsize = v;
    else if (tag == u"bord")
        style.outlineWidth = v;
    else if (tag == u"shad")
        style.shadow = v;
    else if (tag == u"fsp")
        style.spacing = v;
    else if (tag == u"fscx")
        style.scaleX = v;
    else if (tag == u"fscy")
        style.scaleY = v;
    else if (tag == u"fn")
        style.fontname = v;
    else if (tag == u"b")
        style.bold = value == u"1";
    else if (tag == u"i")
        style.italic = value == u"1";
    else if (tag == u"u")
        style.underline = value == u"1";
    else if (tag == u"s")
        style.strikeOut = value == u"1";
    else if (tag == u"fr" || tag == u"frz")
        style.angle = v;
    else
        return false;
    return true;
}

// Visuals::Curve (Visuals.cpp:1245-1295) for a Bézier (bspline false).
void curve(std::size_t pos, const std::vector<ClipPoint> &points, std::vector<PointF> &table)
{
    float a[4], b[4];
    float x[4], y[4];
    for (int g = 0; g < 4; g++) {
        const ClipPoint &point = points[pos + static_cast<std::size_t>(g)];
        x[g] = point.x;
        y[g] = point.y;
    }
    a[3] = -x[0] + 3 * x[1] - 3 * x[2] + x[3];
    a[2] = 3 * x[0] - 6 * x[1] + 3 * x[2];
    a[1] = -3 * x[0] + 3 * x[1];
    a[0] = x[0];
    b[3] = -y[0] + 3 * y[1] - 3 * y[2] + y[3];
    b[2] = 3 * y[0] - 6 * y[1] + 3 * y[2];
    b[1] = -3 * y[0] + 3 * y[1];
    b[0] = y[0];
    const float maxaccel1 = std::fabs(2 * b[2]) + std::fabs(6 * b[3]);
    const float maxaccel2 = std::fabs(2 * a[2]) + std::fabs(6 * a[3]);
    const float maxaccel = maxaccel1 > maxaccel2 ? maxaccel1 : maxaccel2;
    float h = 1.0f;
    if (maxaccel > 4.0f)
        h = std::sqrt(4.0f / maxaccel);
    float p_x, p_y;
    for (float t = 0; t < 1.0; t += h) {
        p_x = a[0] + t * (a[1] + t * (a[2] + t * a[3]));
        p_y = b[0] + t * (b[1] + t * (b[2] + t * b[3]));
        table.push_back({p_x, p_y});
    }
    p_x = a[0] + a[1] + a[2] + a[3];
    p_y = b[0] + b[1] + b[2] + b[3];
    table.push_back({p_x, p_y});
}

// Visuals::GetRectFromSize (Visuals.cpp:217-237).
void rectFromSize(const Context &context, PointF size, int an, const core::LineRecord &line,
                  const core::StyleValues &style, PointF &pos, PointF &pos1)
{
    PointF result = defaultPosition(line, style, an, context.width, context.height);
    if (an % 3 == 2)
        result.x -= (size.x / 2);
    else if (an % 3 == 0)
        result.x -= size.x;
    if (an < 4)
        result.y -= size.y;
    else if (an < 7)
        result.y -= (size.y / 2);
    pos = result;
    pos1.x = result.x + size.x;
    pos1.y = result.y + size.y;
}

// Visuals::IsInRect (Visuals.cpp:239-243).
bool isInRect(PointF pos, PointF pos1, PointF second, PointF second1)
{
    return ((second1.x > pos.x) && (second.x < pos1.x) && (second1.y > pos.y) && (second.y < pos1.y));
}

// The clip rewrite both tools share once the points are moved
// (VisualScale.cpp:785-825, VisualRotationZ.cpp:501-545).
template <typename Move>
void writeClip(TagFind &find, u16 &text, u16 newclip, int vectorScale, const std::vector<ClipPoint> &points, Move move)
{
    const std::size_t psize = points.size();
    if (!psize)
        return;
    const std::string_view format = "5.0f";
    u16 lasttype;
    int countB = 0;
    bool spline = false;
    if (vectorScale > 1)
        newclip += ascii(std::to_string(vectorScale)) + u",";
    for (std::size_t i = 0; i < psize; i++) {
        const ClipPoint &pos = points[i];
        float x = 0, y = 0;
        move(pos, x, y);
        if (countB && !pos.start) {
            newclip += getfloat(x, format) + u" " + getfloat(y, format) + u" ";
            countB++;
        } else {
            if (spline) {
                newclip += u"c ";
                spline = false;
            }
            if (lasttype != pos.type || pos.type == u"m") {
                newclip += pos.type + u" ";
                lasttype = pos.type;
            }
            newclip += getfloat(x, format) + u" " + getfloat(y, format) + u" ";
            if (pos.type == u"b" || pos.type == u"s") {
                countB = 1;
                if (pos.type == u"s")
                    spline = true;
            }
        }
        // fix for m one after another
        if (pos.type == u"m" && psize > 1 &&
            ((i >= psize - 1) || (i < psize - 1 && points[i + 1].type == u"m"))) {
            newclip += u"l " + getfloat(x, format) + u" " + getfloat(y, format) + u" ";
        }
    }
    if (spline)
        newclip += u"c ";
    // newclip.Trim()
    while (!newclip.empty() && isSpace(newclip.back()))
        newclip.pop_back();
    find.replace(newclip, text);
}

// The rectangle clip's four values (VisualScale.cpp:750-774): the values
// past the fourth overwrite what follows the array in legacy; they are
// dropped here.
bool rectanglePoints(const u16 &clip, std::vector<ClipPoint> &points, const Context *context = nullptr)
{
    double value = 0;
    float xy[4] = {0, 0, 0, 0};
    int counter = 0;
    bool isBad = false;
    for (const u16 &token : tokens(clip, u',', true)) {
        if (toDouble(token, value)) {
            if (counter < 4)
                xy[counter] = static_cast<float>(value);
            counter++;
        } else {
            isBad = true;
        }
    }
    if (isBad) {
        if (context && context->log)
            context->log(u"Cannot read clip rectangle values.");
        return false;
    }
    points.push_back({xy[0], xy[1], u"m", true});
    points.push_back({xy[2], xy[1], u"l", true});
    points.push_back({xy[2], xy[3], u"l", true});
    points.push_back({xy[0], xy[3], u"l", true});
    return true;
}

} // namespace

Mouse mouse(const Pointer &event)
{
    Mouse m;
    m.x = event.x;
    m.y = event.y;
    m.shift = event.shift;
    const bool press = event.kind == Pointer::Kind::Press;
    m.leftc = press && event.button == Pointer::Button::Left;
    m.rightc = press && event.button == Pointer::Button::Right;
    m.middlec = press && event.button == Pointer::Button::Middle;
    m.click = m.leftc || m.rightc || m.middlec;
    m.leftIsDown = event.leftDown || m.leftc;
    m.holding = m.click || event.leftDown || event.rightDown || event.middleDown;
    m.buttonUp = event.kind == Pointer::Kind::Release;
    if (m.buttonUp)
        m.holding = event.leftDown || event.rightDown || event.middleDown;
    m.moving = event.kind == Pointer::Kind::Move && !m.holding;
    return m;
}

bool severalLines(const std::vector<core::LineId> &targets, std::optional<core::LineId> active)
{
    return !(targets.size() == 1 && active && targets.front() == *active);
}

u16 getfloat(float num, std::string_view format)
{
    char buf[128];
    const std::string fmt = "%" + std::string(format);
    std::snprintf(buf, sizeof buf, fmt.c_str(), static_cast<double>(num));
    std::string s(buf);
    if (!format.ends_with(".0f")) {
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
    }
    const auto first = s.find_first_not_of(" \t\r\n\v\f");
    s = first == std::string::npos ? std::string() : s.substr(first);
    return ascii(s);
}

bool toDouble(u16v text, double &out)
{
    std::string s;
    for (const char16_t c : text) {
        if (c > 0x7F)
            return false;
        s += static_cast<char>(c);
    }
    if (s.empty())
        return false;
    char *end = nullptr;
    const double value = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || *end != '\0')
        return false;
    out = value;
    return true;
}

double atof(u16v text)
{
    std::string s;
    for (const char16_t c : text) {
        if (c > 0x7F)
            break;
        s += static_cast<char>(c);
    }
    return std::strtod(s.c_str(), nullptr);
}

int atoi(u16v text)
{
    std::string s;
    for (const char16_t c : text) {
        if (c > 0x7F)
            break;
        s += static_cast<char>(c);
    }
    return static_cast<int>(std::strtol(s.c_str(), nullptr, 10));
}

bool TagFind::findTag(u16v pattern, u16v text, int mode)
{
    // With several Lines selected FindTag searches from 0 whatever the mode.
    const long from = m_several ? 0 : m_selFrom;
    const long to = m_several ? 0 : m_selTo;
    core::legacy::TagEditor editor({u16(text), from, to});
    const bool found = editor.findTag(pattern, m_several && mode == 0 ? 1 : mode, false);
    m_result.finding = editor.finding();
    m_result.x = editor.position().first;
    m_result.y = editor.position().second;
    m_result.inBracket = editor.inBracket();
    return found;
}

int TagFind::replaceAll(u16v pattern, u16v tag, u16 &text, const std::function<void(const FindData &, u16 &)> &func,
                        bool returnPosWhenNoTags)
{
    // TagFindReplace::ReplaceAll (TagFindReplace.cpp:257-304).
    int replaces = 0;
    const core::LegacyRegex regex(u"\\\\" + u16(pattern), core::LegacyRegex::Advanced);
    std::size_t textPosition = 0;
    bool needFirstReplace = true;
    const auto tpos = core::legacy::findBrackets(text, 0);
    while (regex.isValid() && regex.matches(mid(text, textPosition))) {
        u16 changedValue;
        const auto m = regex.match(1);
        const std::size_t startMatch = m ? m->first : 0;
        std::size_t lenMatch = m ? m->second : 0;
        const std::size_t position = textPosition + startMatch;
        FindData res{mid(text, position, lenMatch), static_cast<long>(position), static_cast<long>(lenMatch), true};
        func(res, changedValue);
        if (tpos.first <= static_cast<long>(position) && static_cast<long>(position) <= tpos.second)
            needFirstReplace = false;
        if (lenMatch)
            text.erase(position, lenMatch);
        if (!changedValue.empty()) {
            text.insert(position, changedValue);
            lenMatch = changedValue.size();
        } else {
            lenMatch = 0;
        }
        textPosition += startMatch + lenMatch;
        replaces++;
    }
    if (returnPosWhenNoTags && needFirstReplace) {
        const long pos = text.starts_with(u"{") ? 1 : 0;
        FindData res{{}, pos, pos, pos == 1};
        u16 changedValue;
        func(res, changedValue);
        changedValue = u"\\" + u16(tag) + changedValue;
        if (!res.inBracket)
            changedValue = u"{" + changedValue + u"}";
        if (!changedValue.empty())
            text.insert(static_cast<std::size_t>(pos), changedValue);
    }
    return replaces;
}

int TagFind::replace(u16v replacement, u16 &text) const
{
    // TagFindReplace::Replace (TagFindReplace.cpp:423-443). A place past the
    // text (a result from another text) goes to its end.
    const long x = std::clamp<long>(m_result.x, 0, static_cast<long>(text.size()));
    const long y = m_result.y;
    if (text.empty()) {
        text.append(u"{" + u16(replacement) + u"}");
        return 1;
    }
    if (!m_result.inBracket) {
        text.insert(static_cast<std::size_t>(x), u"{" + u16(replacement) + u"}");
        return 1;
    }
    if (x < y) {
        if (static_cast<unsigned long>(y) + 1u >= text.size())
            text.erase(static_cast<std::size_t>(x));
        else
            text.erase(static_cast<std::size_t>(x), static_cast<std::size_t>(y + 1 - x));
    }
    text.insert(static_cast<std::size_t>(x), replacement);
    return 0;
}

bool TagFind::getDouble(double &out) const
{
    return toDouble(m_result.finding, out);
}

bool TagFind::getTwoValueDouble(double &a, double &b) const
{
    // The brackets are cut from a copy that is never used (TagFindReplace.cpp:587-592).
    u16 finding = m_result.finding;
    u16 sval;
    const u16 fval = beforeFirst(finding, u',', &sval);
    return toDouble(fval, a) && toDouble(sval, b);
}

bool TagFind::getTextResult(u16 &out) const
{
    if (m_result.finding.empty())
        return false;
    out = m_result.finding;
    return true;
}

int changeText(u16 &text, u16v what, bool inBracket, long x, long y)
{
    const long px = std::clamp<long>(x, 0, static_cast<long>(text.size()));
    if (!inBracket) {
        text.insert(static_cast<std::size_t>(px), u"{" + u16(what) + u"}");
        return 1;
    }
    if (px < y) {
        if (static_cast<unsigned long>(y) + 1u >= text.size())
            text.erase(static_cast<std::size_t>(px));
        else
            text.erase(static_cast<std::size_t>(px), static_cast<std::size_t>(y + 1 - px));
    }
    text.insert(static_cast<std::size_t>(px), what);
    return 0;
}

Context context(const VisualHost &host)
{
    Context c;
    const EditSession *session = host.session();
    c.width = host.view().scriptWidth();
    c.height = host.view().scriptHeight();
    c.timeMs = static_cast<int>(host.videoTimeMs());
    c.timebase = host.timebase();
    c.measure = host.textMeasure();
    c.ignoreFiltered = host.ignoreFiltered();
    c.log = [&host](u16v text) { const_cast<VisualHost &>(host).log(text); };
    if (!session)
        return c;
    c.document = &session->document();
    c.styles = core::decodeStyles(session->document());
    if (const auto active = host.activeLine()) {
        const auto draft = session->draftRecord();
        if (draft && draft->id == *active)
            c.active = *draft;
        else if (const auto *line = findLine(session->document(), *active))
            c.active = *line;
    }
    if (c.active) {
        // TextEdit's value; in TLMode it holds the translation and an empty
        // one makes Visuals use TextEditOrig (Visuals.cpp:634-639).
        c.editorIsTranslation = translationMode(*session) && !c.active->translation.empty();
        c.editorText = u16of(c.editorIsTranslation ? c.active->translation : c.active->text);
    }
    return c;
}

u16 lineText(const core::LineRecord &line)
{
    return u16of(line.translation.empty() ? line.text : line.translation);
}

bool editsTranslation(const core::LineRecord &line)
{
    return !line.translation.empty();
}

const core::LineRecord *findLine(const core::Document &document, core::LineId id)
{
    for (const auto *line : document.lines())
        if (line->id == id)
            return line;
    return nullptr;
}

core::StyleValues lineStyle(const Context &context, std::u8string_view name)
{
    if (!name.empty())
        for (const auto &s : context.styles)
            if (s.name == name)
                return s;
    if (!context.styles.empty())
        return context.styles.front();
    return defaultStyle();
}

double styleNumber(const std::u8string &value, double initial)
{
    double v = initial;
    if (!toDouble(u16of(value), v))
        v = atoi(u16of(value));
    return v;
}

PointF defaultPosition(const core::LineRecord &line, const core::StyleValues &style, int an, int width, int height)
{
    // Dialogue::GetDefaultPosition (SubsDialogue.cpp:609-634).
    const int lineL = static_cast<int>(line.marginLeft.value);
    const int lineR = static_cast<int>(line.marginRight.value);
    const int lineV = static_cast<int>(line.marginVertical.value);
    float posx = 0, posy = 0;
    if (an % 3 == 2) {
        const int marginL = (lineL != 0) ? lineL : atoi(u16of(style.marginLeft));
        const int marginR = (lineR != 0) ? lineR : atoi(u16of(style.marginRight));
        posx = ((width + marginL - marginR) / 2.f);
    } else if (an % 3 == 0) {
        posx = static_cast<float>((lineR != 0) ? lineR : atoi(u16of(style.marginRight)));
        posx = width - posx;
    } else {
        posx = static_cast<float>((lineL != 0) ? lineL : atoi(u16of(style.marginLeft)));
    }
    if (an < 4) {
        posy = static_cast<float>((lineV != 0) ? lineV : atoi(u16of(style.marginVertical)));
        posy = height - posy;
    } else if (an < 7) {
        posy = (height / 2.f);
    } else {
        posy = static_cast<float>((lineV != 0) ? lineV : atoi(u16of(style.marginVertical)));
    }
    return {posx, posy};
}

int startMs(const core::LineRecord &line)
{
    return static_cast<int>(line.start.value.microseconds() / 1000);
}

int endMs(const core::LineRecord &line)
{
    return static_cast<int>(line.end.value.microseconds() / 1000);
}

PointF posnScale(const Context &context, TagFind &find, bool fromStart, PointF *scale, int *an, double *tbl)
{
    // Visuals::GetPosnScale (Visuals.cpp:624-722) for Scale, RotationZ and
    // RotationXY: beforeCursor is replaceTagsInCursorPosition.
    PointF ppos{0.0f, 0.0f};
    if (!context.active)
        return ppos;
    const core::LineRecord &line = *context.active;
    const u16 &txt = context.editorText;
    const core::StyleValues currentStyle = lineStyle(context, line.style);
    bool foundpos = false;
    if (posRegex().matches(txt) && tbl) {
        const u16 txtpos = group(posRegex(), txt, 2);
        int ipos = 0;
        for (const u16 &token : tokens(txtpos, u',', false)) {
            if (ipos >= 6)
                break;
            if (!toDouble(token, tbl[ipos]))
                tbl[ipos] = 0;
            ipos++;
        }
        tbl[4] += startMs(line);
        tbl[5] += startMs(line);
        tbl[6] = ipos;
        if (ipos > 1) {
            ppos.x = static_cast<float>(tbl[0]);
            ppos.y = static_cast<float>(tbl[1]);
            foundpos = true;
        }
    } else {
        if (tbl)
            tbl[6] = 0;
        // The margins and GetDialogueAdditionalPosition's offset are
        // replaced by the default position below for these families
        // (Visual != VECTORCLIP), so they are not computed here.
    }
    if (tbl && tbl[6] < 4) {
        const int startTime = zeroit(startMs(line));
        const auto [start, end] = moveTimes(context);
        tbl[4] = startTime + start;
        tbl[5] = startTime + end;
    }
    double fscx = 100.0, fscy = 100.0;
    const int mode = fromStart ? 1 : 0;
    if (!(find.findTag(u"fscx([.0-9-]+)", txt, mode) && find.getDouble(fscx)))
        fscx = styleNumber(currentStyle.scaleX, 100.);
    if (!(find.findTag(u"fscy([.0-9-]+)", txt, mode) && find.getDouble(fscy)))
        fscy = styleNumber(currentStyle.scaleY, 100.);
    if (scale) {
        scale->x = static_cast<float>(fscx / 100.f);
        scale->y = static_cast<float>(fscy / 100.f);
    }
    const int tmpan = alignmentOf(currentStyle, txt);
    if (an)
        *an = tmpan;
    if (foundpos)
        return ppos;
    return defaultPosition(line, currentStyle, tmpan, context.width, context.height);
}

std::pair<int, int> moveTimes(const Context &context)
{
    // Visuals::GetMoveTimes (Visuals.cpp:607-622).
    if (!context.active)
        return {0, 0};
    const LegacyTimebase &timebase = context.timebase;
    const int startTime = zeroit(startMs(*context.active));
    const int endTime = zeroit(endMs(*context.active));
    const int msstart = timebase.msAt(timebase.frameAt(startTime));
    const int msend = timebase.msAt(timebase.frameAt(endTime) - 1);
    const int diff = endTime - startTime;
    return {std::abs(msstart - startTime), (diff - std::abs(endTime - msend))};
}

PointF calcMovePos(const Context &context, double *tbl, int start, int end)
{
    // Visuals::CalcMovePos (Visuals.cpp:547-564).
    const int time = context.timeMs;
    if (tbl[6] < 6) {
        tbl[4] = start;
        tbl[5] = end;
    }
    const float tmpt = static_cast<float>(time - tbl[4]);
    const float tmpt1 = static_cast<float>(tbl[5] - tbl[4]);
    const float actime = tmpt / tmpt1;
    float distx, disty;
    if (time < tbl[4]) {
        distx = static_cast<float>(tbl[0]);
        disty = static_cast<float>(tbl[1]);
    } else if (time > tbl[5]) {
        distx = static_cast<float>(tbl[2]);
        disty = static_cast<float>(tbl[3]);
    } else {
        distx = static_cast<float>(tbl[0] - ((tbl[0] - tbl[2]) * actime));
        disty = static_cast<float>(tbl[1] - ((tbl[1] - tbl[3]) * actime));
    }
    return {distx, disty};
}

LinePosition linePosition(const Context &context, const core::LineRecord &line)
{
    // Visuals::GetPosition (Visuals.cpp:834-903), without the move table.
    LinePosition out;
    const core::StyleValues currentStyle = lineStyle(context, line.style);
    const u16 txt = lineText(line);
    if (posRegex().matches(txt)) {
        u16 txtpos = group(posRegex(), txt, 2);
        double posx = 0, posy = 0;
        u16 rest, rest1;
        u16 first = beforeFirst(txtpos, u',', &rest);
        const bool res1 = toDouble(first, posx);
        u16 second = beforeFirst(rest, u',', &rest1);
        const bool res2 = toDouble(second, posy);
        if (const auto m = posRegex().match(0)) {
            out.textX = static_cast<long>(m->first);
            out.textY = static_cast<long>(m->second);
        }
        out.pos = {static_cast<float>(posx), static_cast<float>(posy)};
        if (res1 && res2)
            return out;
    }
    if (!txt.empty() && txt[0] == u'{') {
        out.textX = 1;
        out.textY = 0;
    } else {
        out.textX = 0;
        out.textY = 0;
        out.putInBracket = true;
    }
    const int tmpan = alignmentOf(currentStyle, txt);
    out.pos = defaultPosition(line, currentStyle, tmpan, context.width, context.height);
    const PointF additional = additionalPosition(context, line);
    out.pos.y += additional.y;
    return out;
}

PointF additionalPosition(const Context &context, const core::LineRecord &dialogue)
{
    // Visuals::GetDialoguesWithoutPosition (Visuals.cpp:108-140): the Lines
    // before this one shown at the video's time without \pos or \move.
    std::vector<const core::LineRecord *> withoutPosition;
    const int time = context.timeMs;
    if (context.document && time >= startMs(dialogue) && time < endMs(dialogue)) {
        for (const auto *dial : context.document->lines()) {
            if (!context.ignoreFiltered && dial->visibility == core::LineVisibility::Hidden)
                continue;
            if (dial->id == dialogue.id)
                break;
            if (time >= startMs(*dial) && time < endMs(*dial)) {
                if (!posRegex().matches(lineText(*dial)))
                    withoutPosition.push_back(dial);
            }
        }
    }
    // GetDialogueAdditionalPosition (143-215).
    PointF result{0, 0};
    if (withoutPosition.empty())
        return result;
    const core::StyleValues currentDialogueStyle = lineStyle(context, dialogue.style);
    const int curlineAn = alignmentOf(currentDialogueStyle, lineText(dialogue));
    const int clan = static_cast<int>(curlineAn / 3.1f);
    PointF dialPos{0, 0}, dialPos1{0, 0};
    {
        const TextSize ts = textSize(context, dialogue, &currentDialogueStyle, true);
        PointF size = ts.size;
        size.x += ts.border.x;
        size.y += ts.border.y;
        rectFromSize(context, size, curlineAn, dialogue, currentDialogueStyle, dialPos, dialPos1);
    }
    int placesForText = 0;
    int placesTaken = 0;
    for (const auto *dial : withoutPosition) {
        const core::StyleValues currentStyle = lineStyle(context, dial->style);
        const int newan = alignmentOf(currentStyle, lineText(*dial));
        const int nan = static_cast<int>(newan / 3.1f);
        if (nan == clan) {
            const TextSize ts = textSize(context, *dial, &currentStyle, true);
            PointF size = ts.size;
            size.x += ts.border.x;
            size.y += ts.border.y;
            PointF newpos, newpos1;
            rectFromSize(context, size, newan, *dial, currentStyle, newpos, newpos1);
            if (isInRect(dialPos, dialPos1, newpos, newpos1)) {
                if (clan == 2)
                    result.y += size.y;
                else if (clan == 1)
                    result.y += size.y / 2;
                else if (clan == 0)
                    result.y -= size.y;
                placesTaken++;
            } else if (placesTaken < placesForText && placesTaken > 0 && placesForText > 0) {
                break;
            } else {
                placesForText++;
            }
        }
    }
    return result;
}

TextSize textSize(const Context &context, const core::LineRecord &line, const core::StyleValues *style,
                  bool keepExtraLead)
{
    // Visuals::GetTextSize (Visuals.cpp:953-1133). With no text legacy left
    // `border` as the caller had it; it is 0 here.
    TextSize out;
    const u16 text = lineText(line);
    if (text.empty())
        return out;
    static const std::vector<u16v> names{u"p",    u"fscx",  u"fscy",  u"fsp", u"fs", u"fn",    u"bord",
                                         u"xbord", u"ybord", u"b",    u"i",   u"shad", u"xshad", u"yshad"};
    core::StyleValues measuringStyle =
        style ? *style : lineStyle(context, context.active ? context.active->style : std::u8string());
    PointF result{0.f, 0.f};
    const auto tags = parseTags(text, names);
    float bord = static_cast<float>(styleNumber(measuringStyle.outlineWidth, 0.));
    float xbord = bord;
    float xbord1 = bord;
    float ybord1 = bord;
    const float shad = static_cast<float>(styleNumber(measuringStyle.shadow, 0.));
    float xshad = 0.f;
    float xshad1 = shad;
    float yshad = 0.f;
    float yshad1 = shad;
    u16 drawingText;
    bool wasPlain = false;
    float maxwidth = 0.f;
    float maxheight = 0.f;
    float extlead = 0.f;
    float descent = 0.f;
    for (const Tag &tag : tags) {
        if (tag.name == u"p" || tag.name == u"pvector") {
            if (tag.name == u"pvector") {
                drawingText += tag.value;
            } else if (tag.value == u"0") {
                const PointF drawingsize = drawingSize(drawingText, nullptr);
                result.x += drawingsize.x;
                result.y += drawingsize.y;
                drawingText.clear();
            }
        } else if (tag.name.ends_with(u"ord")) {
            const bool isx = tag.name[0] != u'y';
            const bool isy = tag.name[0] != u'x';
            const float b = static_cast<float>(atof(tag.value));
            if (isx) {
                xbord1 = b;
                if (!wasPlain)
                    xbord = b;
            }
            if (isy)
                ybord1 = b;
        } else if (tag.name.ends_with(u"shad")) {
            const bool isx = tag.name[0] != u'y';
            const bool isy = tag.name[0] != u'x';
            const float s = static_cast<float>(atof(tag.value));
            if (isx) {
                if (s < xshad)
                    xshad = s;
                else
                    xshad1 = s;
            }
            if (isy) {
                if (s < yshad)
                    yshad = s;
                else
                    yshad1 = s;
            }
        } else if (tag.name == u"plain") {
            if (!tag.value.empty()) {
                wasPlain = true;
                std::size_t i = 0;
                std::size_t g = 0;
                float fwidth = 0;
                float fheight = 0;
                while (i != u16::npos) {
                    i = tag.value.find(u"\\N", g);
                    const u16 piece = mid(tag.value, g, i == u16::npos ? u16::npos : i - g);
                    const u16 pltext = trimmed(piece);
                    // GetLineTextExtents(pltext, style, &width, &height, &extlead, &descent):
                    // legacy passes the descent into extlead and the leading into descent.
                    if (lineTextExtents(context, pltext, measuringStyle, fwidth, fheight, extlead, descent)) {
                        maxwidth += fwidth;
                        if (!result.y && !keepExtraLead)
                            fheight -= (extlead - descent);
                        if (maxheight < fheight)
                            maxheight = fheight;
                        if (i != u16::npos) {
                            if (maxwidth > result.x)
                                result.x = maxwidth;
                            result.y += maxheight;
                            maxwidth = 0.f;
                            maxheight = 0.f;
                        }
                    } else if (context.log) {
                        context.log(u"Cannot measure text: " + piece);
                    }
                    g = i + 2;
                }
            }
        } else {
            if (!tagValueToStyle(measuringStyle, tag.name, tag.value) && context.log)
                context.log(u"Cannot assign style to tag: " + tag.name + u" with value: " + tag.value);
        }
    }
    if (!drawingText.empty()) {
        const PointF drawingsize = drawingSize(drawingText, nullptr);
        result.x += drawingsize.x;
        result.y += drawingsize.y;
    }
    if (maxwidth || maxheight) {
        if (maxwidth > result.x)
            result.x = maxwidth;
        result.y += maxheight;
    }
    out.border.x = xbord + xbord1;
    out.border.y = ybord1 * 2;
    if (xshad)
        out.border.x -= xshad;
    if (yshad)
        out.border.y -= yshad;
    if (xshad1)
        out.border.x += xshad1;
    if (yshad1)
        out.border.y += yshad1;
    out.size = result;
    return out;
}

PointF drawingSize(u16v drawing, PointF *position)
{
    // Visuals::GetDrawingSize (Visuals.cpp:1135-1187). A curve that starts
    // the drawing or runs past its end read outside legacy's vector; it is
    // measured by its points here.
    double minx = DBL_MAX;
    double miny = DBL_MAX;
    double maxx = -DBL_MAX;
    double maxy = -DBL_MAX;
    std::vector<ClipPoint> vectorDrawing = vectorPoints(drawing);
    std::size_t i = 0;
    while (i < vectorDrawing.size()) {
        const ClipPoint p = vectorDrawing[i];
        if (p.type == u"b" && i >= 1 && i + 2 < vectorDrawing.size()) {
            std::vector<PointF> bezierPoints;
            const ClipPoint tmp = vectorDrawing[i - 1];
            if (tmp.type == u"s") {
                long j = static_cast<long>(i) - 2;
                while (j >= 0) {
                    if (vectorDrawing[static_cast<std::size_t>(j)].type != u"s")
                        break;
                    j--;
                }
                const long diff = (static_cast<long>(i) - j) - 2;
                vectorDrawing[i - 1] = vectorDrawing[static_cast<std::size_t>(static_cast<long>(i) - diff)];
            }
            curve(i - 1, vectorDrawing, bezierPoints);
            vectorDrawing[i - 1] = tmp;
            for (const PointF point : bezierPoints) {
                if (point.x < minx)
                    minx = point.x;
                if (point.y < miny)
                    miny = point.y;
                if (point.x > maxx)
                    maxx = point.x;
                if (point.y > maxy)
                    maxy = point.y;
            }
            i += 3;
            continue;
        }
        if (p.x < minx)
            minx = p.x;
        if (p.y < miny)
            miny = p.y;
        if (p.x > maxx)
            maxx = p.x;
        if (p.y > maxy)
            maxy = p.y;
        i++;
    }
    if (position) {
        position->x = static_cast<float>(-minx);
        position->y = static_cast<float>(-miny);
    }
    return {static_cast<float>(maxx - minx), static_cast<float>(maxy - miny)};
}

std::vector<ClipPoint> vectorPoints(u16v vector)
{
    // Visuals::GetVectorPoints (Visuals.cpp:566-605).
    std::vector<ClipPoint> points;
    double tmpx = 0;
    bool gotx = false;
    bool start = false;
    int pointsAfterStart = 1;
    u16 type = u"m";
    for (u16 token : tokens(vector, u' ', true)) {
        if (token == u"p")
            token = u"s";
        if (token == u"m" || token == u"l" || token == u"b" || token == u"s") {
            type = token;
            start = true;
            pointsAfterStart = 1;
        } else if (token == u"c") {
            start = true;
            continue;
        } else if (gotx) {
            double tmpy = 0;
            if (!toDouble(token, tmpy)) {
                gotx = false;
                continue;
            }
            points.push_back({static_cast<float>(tmpx), static_cast<float>(tmpy), type, start});
            gotx = false;
            if ((type == u"l" || (type == u"m" && pointsAfterStart == 1)) || (type == u"b" && pointsAfterStart == 3)) {
                if (type == u"m")
                    type = u"l";
                start = true;
                pointsAfterStart = 0;
            } else {
                start = false;
            }
            pointsAfterStart++;
        } else {
            if (toDouble(token, tmpx))
                gotx = true;
        }
    }
    return points;
}

PointF rotateZ(PointF point, float sinOfAngle, float cosOfAngle, PointF pivot)
{
    const float x = point.x - pivot.x;
    const float y = point.y - pivot.y;
    return {(x * cosOfAngle) - (y * sinOfAngle) + pivot.x, (x * sinOfAngle) + (y * cosOfAngle) + pivot.y};
}

void changeOrg(const Context &context, TagFind &find, u16 &text, const core::LineRecord &line, float coordx,
               float coordy)
{
    // Visuals::ChangeOrg (Visuals.cpp:905-934). FindTag gives the found
    // \org's start and its last character; legacy then moved that end by the
    // start less one as it did for an insertion, so an \org that was not the
    // block's first tag took the characters after it with it. Only the
    // insertion's end is set here; a found \org is replaced exactly
    // (T3-change-org-end).
    double orgx = 0, orgy = 0;
    bool putInBrackets = false;
    long sx = 0, sy = 0;
    if (find.findTag(u"org\\((.+)\\)", text.empty() ? u16v(context.editorText) : u16v(text), 1)) {
        (void)find.getTwoValueDouble(orgx, orgy);
        putInBrackets = false;
        sx = find.result().x;
        sy = find.result().y;
    } else {
        const LinePosition pos = linePosition(context, line);
        putInBrackets = pos.putInBracket;
        sx = pos.textX;
        sy = pos.textY;
        orgx = pos.pos.x;
        orgy = pos.pos.y;
        if (sy == 0) {
            const u16 posTag = u"\\pos(" + getfloat(pos.pos.x) + u"," + getfloat(pos.pos.y) + u")";
            const int append = changeText(text, posTag, !putInBrackets, sx, sy);
            sx += static_cast<long>(posTag.size()) + append;
            putInBrackets = false;
        }
        // An insertion at sx (ChangeText erases nothing when its end is
        // before its start).
        sy = sx - 1;
    }
    changeText(text,
               u"\\org(" + getfloat(static_cast<float>(orgx + coordx)) + u"," +
                   getfloat(static_cast<float>(orgy + coordy)) + u")",
               !putInBrackets, sx, sy);
}

void changeClipScale(TagFind &find, u16 &text, PointF pivot, float scalex, float scaley)
{
    // Scale::ChangeClipScale (VisualScale.cpp:741-827).
    if (!find.findTag(u"(i?clip[^)]+\\))", text, 1))
        return;
    u16 clip1, clip;
    (void)find.getTextResult(clip1);
    u16 newclip = u"\\" + beforeFirst(clip1, u'(', &clip) + u"(";
    int vectorScale = 1;
    const std::size_t clipFreq = freq(clip, u',');
    std::vector<ClipPoint> points;
    if (clipFreq >= 3) {
        (void)rectanglePoints(clip, points);
    } else {
        if (clipFreq >= 1) {
            // Legacy wrote clip.BeforeFirst(L',', &clip): the rest replaced
            // the clip before the head was returned, so the scale read was
            // the tail's first characters and the vector's scale was dropped
            // (`\clip(2,m ...)` lost `2,`). The head is the scale here, as
            // RotationZ's rewrite reads it (T3-scale-vector-clip).
            u16 rest;
            const u16 vscale = beforeFirst(clip, u',', &rest);
            const int vscaleint = atoi(vscale);
            if (vscaleint > 0)
                vectorScale = vscaleint;
            clip = rest;
        }
        points = vectorPoints(clip);
    }
    // A scaled vector's points are in units of 1 / 2^(scale - 1) pixels, so
    // the pivot is taken into them.
    const float units = std::ldexp(1.f, vectorScale - 1);
    const PointF p{pivot.x * units, pivot.y * units};
    writeClip(find, text, newclip, vectorScale, points, [&](const ClipPoint &pos, float &x, float &y) {
        x = p.x + ((pos.x - p.x) * scalex) + 0.5f;
        y = p.y + ((pos.y - p.y) * scaley) + 0.5f;
    });
}

void changeClipRotationZ(TagFind &find, u16 &text, PointF pivot, float sinus, float cosinus)
{
    // RotationZ::ChangeClipRotationZ (VisualRotationZ.cpp:455-547).
    if (!find.findTag(u"(i?clip[^)]+\\))", text, 1))
        return;
    u16 clip1, clip;
    (void)find.getTextResult(clip1);
    u16 newclip = u"\\" + beforeFirst(clip1, u'(', &clip) + u"(";
    int vectorScale = 1;
    const std::size_t clipFreq = freq(clip, u',');
    std::vector<ClipPoint> points;
    if (clipFreq >= 3) {
        // Legacy added 2 to the bottom (VisualRotationZ.cpp:480), so every
        // edit made the rectangle 2 taller; not here (T3-rotz-rect-clip).
        (void)rectanglePoints(clip, points);
    } else {
        if (clipFreq >= 1) {
            const u16 vscale = beforeFirst(clip, u',', &clip1);
            const int vscaleint = atoi(vscale);
            if (vscaleint > 0)
                vectorScale = vscaleint;
            clip = clip1;
        }
        points = vectorPoints(clip);
    }
    writeClip(find, text, newclip, vectorScale, points, [&](const ClipPoint &pos, float &x, float &y) {
        const PointF posxy = rotateZ({pos.x, pos.y}, sinus, cosinus, pivot);
        x = static_cast<float>(posxy.x + 0.5);
        y = static_cast<float>(posxy.y + 0.5);
    });
}

u16 moveText(PointF pos, const double *tbl, int lineStartMs)
{
    const PointF pos1{static_cast<float>(tbl[2] - tbl[0]), static_cast<float>(tbl[3] - tbl[1])};
    const int startTime = zeroit(lineStartMs);
    return u"\\move(" + getfloat(pos.x) + u"," + getfloat(pos.y) + u"," + getfloat(pos.x + pos1.x) + u"," +
           getfloat(pos.y + pos1.y) + u"," + getfloat(static_cast<float>(tbl[4] - startTime), "6.0f") + u"," +
           getfloat(static_cast<float>(tbl[5] - startTime), "6.0f") + u")";
}

PointF drawArrow(Overlay &out, PointF from, PointF to, int diff)
{
    // Visuals::DrawArrow (Visuals.cpp:305-337): the head's triangle filled
    // 0xAA121150 with a 0xFFBB0000 border; the line ends at its base.
    const PointF pdiff{from.x - to.x, from.y - to.y};
    const float len = std::sqrt((pdiff.x * pdiff.x) + (pdiff.y * pdiff.y));
    const PointF diffUnits = (len == 0) ? PointF{0, 0} : PointF{pdiff.x / len, pdiff.y / len};
    const float k = static_cast<float>(12 + diff);
    const PointF pend{to.x + diffUnits.x * k, to.y + diffUnits.y * k};
    const PointF halfbase{-diffUnits.y * 5.f, diffUnits.x * 5.f};
    out.polygons.push_back({{{pend.x - diffUnits.x * 12, pend.y - diffUnits.y * 12},
                             {pend.x + halfbase.x, pend.y + halfbase.y},
                             {pend.x - halfbase.x, pend.y - halfbase.y}},
                            kHandleFill,
                            kHandleBorder});
    return pend;
}

void drawRect(Overlay &out, PointF pos, bool selected, float size)
{
    // Visuals::DrawRect (Visuals.cpp:357-374).
    out.polygons.push_back({{{pos.x - size, pos.y - size},
                             {pos.x + size, pos.y - size},
                             {pos.x + size, pos.y + size},
                             {pos.x - size, pos.y + size}},
                            selected ? kHandleSelectedFill : kHandleFill,
                            kHandleBorder});
}

} // namespace hikari::application::visual::transform
