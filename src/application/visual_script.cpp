#include "hikari/application/visual_script.h"

#include "hikari/application/automation_services.h"
#include "hikari/application/resample.h"
#include "hikari/core/legacy_regex.h"
#include "hikari/core/text_projection.h"

#include <cfloat>
#include <charconv>
#include <cmath>
#include <cstdlib>

// Built without floating-point contraction, as visual_view.cpp: legacy's
// float steps must round as its build did.

namespace hikari::application::visual {

namespace {

using u16 = std::u16string;
using u16v = std::u16string_view;

std::string narrow(u16v text)
{
    const std::u8string u = core::toUtf8(text);
    return std::string(u.begin(), u.end());
}

std::string narrow(std::u8string_view text)
{
    return std::string(text.begin(), text.end());
}

// wxString::ToCDouble: the whole text a C-locale number (leading white space
// and a '+' allowed, as strtod), else false.
bool toCDouble(u16v text, double &out)
{
    std::string s = narrow(text);
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || (s[i] >= '\t' && s[i] <= '\r')))
        ++i;
    if (i < s.size() && s[i] == '+' && (i + 1 >= s.size() || s[i + 1] != '-'))
        ++i;
    if (i >= s.size())
        return false;
    double value = 0;
    const auto [end, ec] = std::from_chars(s.data() + i, s.data() + s.size(), value);
    if (ec != std::errc())
        return false;
    out = value;
    return end == s.data() + s.size();
}

// wxAtoi / wxAtof (atoi, atof).
int atoi16(u16v text)
{
    return std::atoi(narrow(text).c_str());
}
int atoi8(std::u8string_view text)
{
    return std::atoi(narrow(text).c_str());
}
double atof16(u16v text)
{
    // atof reads the leading number; the C locale's form, as wxAtof on the
    // legacy build's default locale.
    const std::string s = narrow(text);
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || (s[i] >= '\t' && s[i] <= '\r')))
        ++i;
    if (i < s.size() && s[i] == '+')
        ++i;
    double value = 0;
    const auto [end, ec] = std::from_chars(s.data() + i, s.data() + s.size(), value);
    (void)end;
    return ec == std::errc() ? value : 0.0;
}

// wxStringTokenizer(text, ",") (wxTOKEN_RET_EMPTY: empty tokens in the
// middle, none after a trailing delimiter) and wxTOKEN_STRTOK (no empties).
std::vector<u16> tokenize(u16v text, char16_t delimiter, bool strtok)
{
    std::vector<u16> out;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t next = text.find(delimiter, pos);
        const u16v token = text.substr(pos, next == u16v::npos ? u16v::npos : next - pos);
        if (!strtok || !token.empty())
            out.emplace_back(token);
        if (next == u16v::npos)
            break;
        pos = next + 1;
    }
    return out;
}

bool isSpace16(char16_t c)
{
    return c == u' ' || (c >= u'\t' && c <= u'\r');
}

// wxString::Trim().Trim(false).
u16 trimmed(u16v text)
{
    std::size_t a = 0, b = text.size();
    while (a < b && isSpace16(text[a]))
        ++a;
    while (b > a && isSpace16(text[b - 1]))
        --b;
    return u16(text.substr(a, b - a));
}

const core::LegacyRegex &posMoveRegex()
{
    // Visuals.cpp:116, 644, 845.
    static const core::LegacyRegex re(u"\\\\(pos|move)\\(([^\\)]+)\\)", core::LegacyRegex::Advanced);
    return re;
}

const core::LegacyRegex &anRegex()
{
    // Visuals.cpp:153, 711, 895.
    static const core::LegacyRegex re(u"\\\\an([0-9]+)", core::LegacyRegex::Advanced);
    return re;
}

u16 group(const core::LegacyRegex &re, u16v text, std::size_t index)
{
    const auto m = re.match(index);
    if (!m || m->first == u16v::npos)
        return {};
    return u16(text.substr(m->first, m->second));
}

int alignmentOf(const core::StyleValues &style, u16v text)
{
    int an = atoi8(style.alignment);
    if (anRegex().matches(text))
        an = atoi16(group(anRegex(), text, 1));
    return an;
}

int marginOr(const core::IntField &field, std::u8string_view styleValue)
{
    return field.value != 0 ? static_cast<int>(field.value) : atoi8(styleValue);
}

// Dialogue::ParseTags (SubsDialogue.cpp:1086-1156) with plain text.
struct TagData {
    u16 name;
    u16 value;
};
std::vector<TagData> parseTags(u16v txt, const std::vector<u16v> &names)
{
    std::vector<TagData> out;
    const std::size_t len = txt.size();
    std::size_t pos = 0, plainStart = 0;
    bool hasDrawing = false, tagsBlock = false;
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
            if (plainStart + 1 <= pos)
                out.push_back({hasDrawing ? u"pvector" : u"plain", u16(txt.substr(plainStart, pos - plainStart))});
        } else if (tagsBlock && ch == u'\\') {
            ++pos;
            const std::size_t slashPos = txt.find(u'\\', pos);
            const std::size_t bracketPos = txt.find(u'}', pos);
            const std::size_t tagEnd = (slashPos == u16v::npos && bracketPos == u16v::npos) ? len
                                       : slashPos == u16v::npos                            ? bracketPos
                                       : bracketPos == u16v::npos                          ? slashPos
                                       : bracketPos < slashPos                             ? bracketPos
                                                                                           : slashPos;
            u16 tag(txt.substr(pos, tagEnd - pos));
            if (!tag.empty() && tag.back() == u')')
                tag.pop_back();
            for (const u16v name : names) {
                if (tag.size() > name.size() && u16v(tag).substr(0, name.size()) == name) {
                    const char16_t first = tag[name.size()];
                    if (first == u'(' || (first >= u'0' && first <= u'9') || name == u"fn" || first == u'.' ||
                        first == u'-' || first == u'+') {
                        u16 value = tag.substr(name.size());
                        if (name == u"p") {
                            hasDrawing = trimmed(value) != u"0";
                        } else if (first == u'(') {
                            // After('(').BeforeFirst(')').
                            u16 after = value.substr(1);
                            const auto close = after.find(u')');
                            value = close == u16::npos ? after : after.substr(0, close);
                        } else if (name != u"fn") {
                            double number = 0;
                            if (!toCDouble(value, number)) {
                                u16 kept;
                                for (const char16_t c : value) {
                                    if (!(c >= u'0' && c <= u'9') && c != u'.' && c != u'-' && c != u'+')
                                        break;
                                    kept += c;
                                }
                                value = kept;
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

// TagFindReplace::TagValueToStyle (TagFindReplace.cpp:511-556) for the tags
// GetTextSize hands it.
bool tagValueToStyle(core::StyleValues &style, u16v tag, u16v value)
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

// Styles::GetOtlineDouble / GetShadowDouble (styles.cpp:134-149): ToDouble,
// else wxAtoi.
double styleDouble(std::u8string_view value)
{
    double out = 0;
    const u16 v = core::toUtf16(value);
    if (!toCDouble(v, out))
        out = atoi16(v);
    return out;
}

// GetLineTextExtents (UtilsWindows.cpp:233-341) through the text measure.
bool lineTextExtents(const ScriptState &state, u16v text, const core::StyleValues &style, float &width,
                     float &height, float &descent, float &extlead)
{
    if (text.empty()) {
        width = height = descent = extlead = 0;
        return true;
    }
    if (!state.measure)
        return false;
    std::vector<std::string> fields;
    for (const auto &f : core::legacy::styleRawFields(style))
        fields.push_back(narrow(f));
    const auto e = state.measure->measure(fields, narrow(text));
    if (!e)
        return false;
    width = static_cast<float>(e->width);
    height = static_cast<float>(e->height);
    descent = static_cast<float>(e->descent);
    extlead = static_cast<float>(e->externalLeading);
    return true;
}

void log(const ScriptState &state, u16v text)
{
    if (state.log)
        state.log(text);
}

// ClipPoint and Visuals::GetVectorPoints (Visuals.cpp:566-605).
struct ClipPoint {
    float x = 0, y = 0;
    u16 type;
    bool start = false;
};
std::vector<ClipPoint> vectorPoints(u16v vector)
{
    std::vector<ClipPoint> points;
    double tmpx = 0;
    bool gotx = false;
    bool start = false;
    int pointsAfterStart = 1;
    u16 type = u"m";
    // wxStringTokenizer(vector, " "): white space delimiters, no empty tokens.
    std::size_t pos = 0;
    while (pos < vector.size()) {
        while (pos < vector.size() && vector[pos] == u' ')
            ++pos;
        if (pos >= vector.size())
            break;
        std::size_t end = vector.find(u' ', pos);
        if (end == u16v::npos)
            end = vector.size();
        u16 token(vector.substr(pos, end - pos));
        pos = end;
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
            if (!toCDouble(token, tmpy)) {
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
            if (toCDouble(token, tmpx))
                gotx = true;
        }
    }
    return points;
}

// Visuals::Curve (Visuals.cpp:1245-1294) for a Bézier segment.
void curve(int pos, const std::vector<ClipPoint> &points, std::vector<PointF> &table)
{
    float a[4], b[4];
    float x[4], y[4];
    int currentPoint = 0;
    const int nBsplinePoints = 4;
    for (int g = 0; g < 4; g++) {
        if (currentPoint > (nBsplinePoints - 1))
            currentPoint = 0;
        const ClipPoint &point = points[static_cast<std::size_t>(pos + currentPoint)];
        x[g] = point.x;
        y[g] = point.y;
        currentPoint++;
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

// Visuals::GetDialoguesWithoutPosition (Visuals.cpp:108-140).
std::vector<const core::LineRecord *> linesWithoutPosition(const ScriptState &state, const core::LineRecord &line)
{
    std::vector<const core::LineRecord *> out;
    const int time = state.timeMs;
    if (!state.document || !(time >= startMs(line) && time < endMs(line)))
        return out;
    for (const auto *dial : state.document->lines()) {
        if ((!state.ignoreFiltered && dial->visibility == core::LineVisibility::Hidden) || dial->unparsed)
            continue;
        if (dial->id == line.id)
            break;
        if (time >= startMs(*dial) && time < endMs(*dial)) {
            if (!posMoveRegex().matches(lineText(*dial)))
                out.push_back(dial);
        }
    }
    return out;
}

// Visuals::GetRectFromSize (Visuals.cpp:217-237).
void rectFromSize(const ScriptState &state, PointF size, int an, const core::LineRecord &dial,
                  const core::StyleValues &style, PointF &pos, PointF &pos1)
{
    PointF result = defaultPosition(dial, style, an, state.width, state.height);
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

bool isInRect(PointF pos, PointF pos1, PointF second, PointF second1)
{
    return ((second1.x > pos.x) && (second.x < pos1.x) && (second1.y > pos.y) && (second.y < pos1.y));
}

PointF add(PointF a, PointF b)
{
    return {a.x + b.x, a.y + b.y};
}

} // namespace

ScriptState scriptState(const VisualHost &host)
{
    ScriptState state;
    const EditSession *session = host.session();
    state.width = host.view().scriptWidth();
    state.height = host.view().scriptHeight();
    state.timeMs = static_cast<int>(host.videoTimeMs());
    state.timebase = host.timebase();
    state.measure = host.textMeasure();
    state.ignoreFiltered = host.ignoreFiltered();
    VisualHost *mutableHost = const_cast<VisualHost *>(&host);
    state.log = [mutableHost](std::u16string_view text) { mutableHost->log(text); };
    if (!session)
        return state;
    state.document = &session->document();
    state.styles = core::decodeStyles(session->document());
    if (const auto active = host.activeLine()) {
        const auto draft = session->draftRecord();
        if (draft && draft->id == *active)
            state.active = *draft;
        else
            for (const auto *line : session->document().lines())
                if (line->id == *active)
                    state.active = *line;
    }
    return state;
}

const core::LineRecord *scriptLine(const ScriptState &state, core::LineId id)
{
    if (state.active && state.active->id == id)
        return &*state.active;
    if (!state.document)
        return nullptr;
    for (const auto *line : state.document->lines())
        if (line->id == id)
            return line;
    return nullptr;
}

bool editsTranslation(const core::LineRecord &line)
{
    return !line.translation.empty();
}

std::u16string lineText(const core::LineRecord &line)
{
    return core::toUtf16(editsTranslation(line) ? line.translation : line.text);
}

core::StyleValues lineStyle(const ScriptState &state, std::u8string_view name)
{
    if (!name.empty())
        for (const auto &s : state.styles)
            if (s.name == name)
                return s;
    if (!state.styles.empty())
        return state.styles.front();
    // Styles::Styles() (styles.cpp:249-275), which GetStyle adds.
    core::StyleValues d;
    d.name = u8"Default";
    d.fontname = u8"Garamond";
    d.fontsize = u8"40";
    d.scaleX = d.scaleY = u8"100";
    d.spacing = d.angle = u8"0";
    d.outlineWidth = d.shadow = u8"2";
    d.alignment = u8"2";
    d.marginLeft = d.marginRight = d.marginVertical = u8"20";
    d.encoding = u8"1";
    d.complete = true;
    return d;
}

PointF defaultPosition(const core::LineRecord &line, const core::StyleValues &style, int an, int width, int height)
{
    // Dialogue::GetDefaultPosition (SubsDialogue.cpp:609-634).
    PointF pos;
    if (an % 3 == 2) {
        const int marginL = marginOr(line.marginLeft, style.marginLeft);
        const int marginR = marginOr(line.marginRight, style.marginRight);
        pos.x = ((width + marginL - marginR) / 2.f);
    } else if (an % 3 == 0) {
        pos.x = static_cast<float>(marginOr(line.marginRight, style.marginRight));
        pos.x = width - pos.x;
    } else {
        pos.x = static_cast<float>(marginOr(line.marginLeft, style.marginLeft));
    }
    if (an < 4) {
        pos.y = static_cast<float>(marginOr(line.marginVertical, style.marginVertical));
        pos.y = height - pos.y;
    } else if (an < 7) {
        pos.y = (height / 2.f);
    } else {
        pos.y = static_cast<float>(marginOr(line.marginVertical, style.marginVertical));
    }
    return pos;
}

LinePosition linePosition(const ScriptState &state, const core::LineRecord &line, bool withMove)
{
    // Visuals::GetPosition (Visuals.cpp:834-903).
    LinePosition out;
    const core::StyleValues style = lineStyle(state, line.style);
    const u16 txt = lineText(line);
    const auto &pos = posMoveRegex();
    if (pos.matches(txt)) {
        const u16 txtpos = group(pos, txt, 2);
        double posx = 0, posy = 0;
        // BeforeFirst(',', &rest) twice.
        const auto c1 = txtpos.find(u',');
        const u16 rest = c1 == u16::npos ? u16() : txtpos.substr(c1 + 1);
        const bool res1 = toCDouble(u16v(txtpos).substr(0, c1), posx);
        const auto c2 = rest.find(u',');
        const u16 rest1 = c2 == u16::npos ? u16() : rest.substr(c2 + 1);
        const bool res2 = toCDouble(u16v(rest).substr(0, c2), posy);
        if (withMove && group(pos, txt, 1) == u"move" && !rest1.empty()) {
            // A \move with too few values leaves the end point's other
            // coordinate as legacy's uninitialised moveTable had it; it is
            // 0 here (a proposed departure in the T2 report).
            MoveTable move;
            int ipos = 0;
            for (const u16 &token : tokenize(rest1, u',', false)) {
                if (ipos >= 4)
                    break;
                double value = 0;
                move.values[static_cast<std::size_t>(ipos)] = toCDouble(token, value) ? value : 0;
                ipos++;
            }
            if (ipos < 3) {
                move.values[2] = startMs(line);
                move.values[3] = endMs(line);
            } else {
                move.values[2] += startMs(line);
                move.values[3] += startMs(line);
            }
            move.count = ipos;
            out.move = move;
        }
        if (const auto m = pos.match(0)) {
            out.textStart = m->first;
            out.textLength = m->second;
        }
        out.pos = {static_cast<float>(posx), static_cast<float>(posy)};
        if (res1 && res2)
            return out;
    }
    if (!txt.empty() && txt[0] == u'{') {
        out.textStart = 1;
        out.textLength = 0;
    } else {
        out.textStart = 0;
        out.textLength = 0;
        out.putInBracket = true;
    }
    const int an = alignmentOf(style, txt);
    out.pos = defaultPosition(line, style, an, state.width, state.height);
    const PointF additional = additionalPosition(state, line);
    out.pos.y += additional.y;
    return out;
}

PointF additionalPosition(const ScriptState &state, const core::LineRecord &line)
{
    // Visuals::GetDialogueAdditionalPosition (Visuals.cpp:143-215).
    const auto without = linesWithoutPosition(state, line);
    PointF result{0, 0};
    if (without.empty())
        return result;
    const core::StyleValues currentStyle = lineStyle(state, line.style);
    const int curlineAn = alignmentOf(currentStyle, lineText(line));
    const int clan = static_cast<int>(curlineAn / 3.1f);
    PointF dialPos{0, 0}, dialPos1{0, 0};
    TextSize measured = textSize(state, line, &currentStyle, true);
    PointF size = add(measured.size, measured.border);
    rectFromSize(state, size, curlineAn, line, currentStyle, dialPos, dialPos1);
    int placesForText = 0;
    int placesTaken = 0;
    for (const auto *dial : without) {
        const core::StyleValues style = lineStyle(state, dial->style);
        const int newan = alignmentOf(style, lineText(*dial));
        const int nan = static_cast<int>(newan / 3.1f);
        if (nan == clan) {
            const TextSize other = textSize(state, *dial, &style, true);
            const PointF otherSize = add(other.size, other.border);
            PointF newpos, newpos1;
            rectFromSize(state, otherSize, newan, *dial, style, newpos, newpos1);
            if (isInRect(dialPos, dialPos1, newpos, newpos1)) {
                if (clan == 2)
                    result.y += otherSize.y;
                else if (clan == 1)
                    result.y += otherSize.y / 2;
                else if (clan == 0)
                    result.y -= otherSize.y;
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

PointF posnScale(const ScriptState &state, Family visual, const core::LineRecord &editLine, std::u16string_view txt,
                 int *an, double *tbl)
{
    // Visuals::GetPosnScale (Visuals.cpp:624-722). The scale (FindTag
    // fscx/fscy) is asked only by the families after Move.
    PointF ppos{0.0f, 0.0f};
    const core::StyleValues currentStyle = lineStyle(state, editLine.style);
    bool foundpos = false;
    const auto &pos = posMoveRegex();
    if (pos.matches(txt) && tbl) {
        const u16 txtpos = group(pos, txt, 2);
        int ipos = 0;
        for (const u16 &token : tokenize(txtpos, u',', false)) {
            if (ipos >= 6)
                break;
            double value = 0;
            tbl[ipos] = toCDouble(token, value) ? value : 0;
            ipos++;
        }
        tbl[4] += startMs(editLine);
        tbl[5] += startMs(editLine);
        tbl[6] = ipos;
        if (ipos > 1) {
            ppos.x = static_cast<float>(tbl[0]);
            ppos.y = static_cast<float>(tbl[1]);
            foundpos = true;
        }
    } else {
        if (tbl)
            tbl[6] = 0;
        ppos.x = static_cast<float>(marginOr(editLine.marginLeft, currentStyle.marginLeft));
        ppos.y = static_cast<float>(marginOr(editLine.marginVertical, currentStyle.marginVertical));
        const PointF additional = additionalPosition(state, editLine);
        ppos.y += additional.y;
    }
    if (tbl && tbl[6] < 4) {
        const int startTime = zeroit(startMs(editLine));
        const auto [start, end] = moveTimes(state, editLine);
        tbl[4] = startTime + start;
        tbl[5] = startTime + end;
    }
    if (visual != Family::VectorClip) {
        int tmpan = atoi8(currentStyle.alignment);
        if (anRegex().matches(txt))
            tmpan = atoi16(group(anRegex(), txt, 1));
        if (an)
            *an = tmpan;
        if (foundpos)
            return ppos;
        ppos = defaultPosition(editLine, currentStyle, tmpan, state.width, state.height);
    }
    return ppos;
}

std::pair<int, int> moveTimes(const ScriptState &state, const core::LineRecord &editLine)
{
    // Visuals::GetMoveTimes (Visuals.cpp:607-622).
    const LegacyTimebase &timebase = state.timebase;
    const int startTime = zeroit(startMs(editLine));
    const int endTime = zeroit(endMs(editLine));
    const int msstart = timebase.msAt(timebase.frameAt(startTime));
    const int msend = timebase.msAt(timebase.frameAt(endTime) - 1);
    const int diff = endTime - startTime;
    return {std::abs(msstart - startTime), (diff - std::abs(endTime - msend))};
}

PointF calcMovePosition(PointF point, const double *moveTable, int time)
{
    // CalcMovePosition (UtilsWindows.cpp:344-358), its float and double steps.
    const float tmpt = static_cast<float>(time - moveTable[2]);
    const float tmpt1 = static_cast<float>(moveTable[3] - moveTable[2]);
    const float actime = tmpt / tmpt1;
    float distx, disty;
    if (time < moveTable[2]) {
        distx = point.x, disty = point.y;
    } else if (time > moveTable[3]) {
        distx = static_cast<float>(moveTable[0]), disty = static_cast<float>(moveTable[1]);
    } else {
        distx = static_cast<float>(point.x - ((point.x - moveTable[0]) * actime));
        disty = static_cast<float>(point.y - ((point.y - moveTable[1]) * actime));
    }
    return {distx, disty};
}

TextSize textSize(const ScriptState &state, const core::LineRecord &line, const core::StyleValues *style,
                  bool keepExtraLead)
{
    // Visuals::GetTextSize (Visuals.cpp:953-1133). GetLineTextExtents is
    // called with descent and external leading swapped (its fifth parameter
    // is the descent), so `extlead` holds the descent and `descent` the
    // external leading, as legacy computes.
    static const std::vector<u16v> tags{u"p",   u"fscx", u"fscy",  u"fsp", u"fs",   u"fn",    u"bord",
                                        u"xbord", u"ybord", u"b", u"i", u"shad", u"xshad", u"yshad"};
    TextSize out;
    PointF result{0.f, 0.f};
    const u16 text = lineText(line);
    if (text.empty())
        return out;
    core::StyleValues measuring = style ? *style : lineStyle(state, state.active ? state.active->style : line.style);
    const auto presult = parseTags(text, tags);
    float bord = static_cast<float>(styleDouble(measuring.outlineWidth));
    float xbord = bord;
    float xbord1 = bord;
    float ybord1 = bord;
    const float shad = static_cast<float>(styleDouble(measuring.shadow));
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
    PointF extralead{0, 0};
    PointF drawingPosition{0, 0};
    for (const auto &tag : presult) {
        if (tag.name == u"p" || tag.name == u"pvector") {
            if (tag.name == u"pvector") {
                drawingText += tag.value;
            } else if (tag.value == u"0") {
                const PointF drawing = drawingSize(drawingText, &drawingPosition);
                result = add(result, drawing);
                drawingText.clear();
            }
        } else if (tag.name.size() >= 3 && tag.name.ends_with(u"ord")) {
            const bool isx = tag.name[0] != u'y';
            const bool isy = tag.name[0] != u'x';
            const float b = static_cast<float>(atof16(tag.value));
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
            const float s = static_cast<float>(atof16(tag.value));
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
                    const u16 pltext = trimmed(u16v(tag.value).substr(g, i == u16::npos ? u16v::npos : i - g));
                    if (lineTextExtents(state, pltext, measuring, fwidth, fheight, extlead, descent)) {
                        maxwidth += fwidth;
                        if (!result.y && !keepExtraLead)
                            fheight -= (extlead - descent);
                        if (extralead.x < extlead)
                            extralead.x = extlead;
                        if (extralead.y < descent)
                            extralead.y = descent;
                        if (maxheight < fheight)
                            maxheight = fheight;
                        if (i != u16::npos) {
                            if (maxwidth > result.x)
                                result.x = maxwidth;
                            result.y += maxheight;
                            maxwidth = 0.f;
                            maxheight = 0.f;
                        }
                    } else {
                        log(state, u"Cannot measure text: " +
                                       u16(u16v(tag.value).substr(g, i == u16::npos ? u16v::npos : i - g)));
                    }
                    g = i + 2;
                }
            }
        } else {
            if (!tagValueToStyle(measuring, tag.name, tag.value))
                log(state, u"Cannot assign style to tag: " + tag.name + u" with value: " + tag.value);
        }
    }
    if (!drawingText.empty()) {
        const PointF drawing = drawingSize(drawingText, &drawingPosition);
        result = add(result, drawing);
    }
    if (maxwidth || maxheight) {
        if (maxwidth > result.x)
            result.x = maxwidth;
        result.y += maxheight;
    }
    if (extralead.x < extlead)
        extralead.x = extlead;
    if (extralead.y < descent)
        extralead.y = descent;
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
    out.bordShad[0] = {xbord - xshad, ybord1 - yshad};
    out.bordShad[1] = {xbord1 + xshad1, ybord1 + yshad1};
    out.size = result;
    out.extraLead = extralead;
    out.drawingPosition = drawingPosition;
    return out;
}

PointF drawingSize(std::u16string_view drawing, PointF *position)
{
    // Visuals::GetDrawingSize (Visuals.cpp:1135-1187).
    double minx = DBL_MAX;
    double miny = DBL_MAX;
    double maxx = -DBL_MAX;
    double maxy = -DBL_MAX;
    std::vector<ClipPoint> points = vectorPoints(drawing);
    std::size_t i = 0;
    while (i < points.size()) {
        const ClipPoint p = points[i];
        if (p.type == u"b" && i > 0) {
            std::vector<PointF> bezier;
            const ClipPoint tmp = points[i - 1];
            if (tmp.type == u"s") {
                int j = static_cast<int>(i) - 2;
                while (j >= 0) {
                    if (points[static_cast<std::size_t>(j)].type != u"s")
                        break;
                    j--;
                }
                const int diff = (static_cast<int>(i) - j) - 2;
                points[i - 1] = points[i - static_cast<std::size_t>(diff)];
            }
            // Curve reads four points from i - 1; a short last segment reads
            // past legacy's vector, which is not reproduced: it ends here.
            if (i + 2 >= points.size()) {
                points[i - 1] = tmp;
                break;
            }
            curve(static_cast<int>(i) - 1, points, bezier);
            points[i - 1] = tmp;
            for (const PointF &point : bezier) {
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

int changeText(std::u16string &txt, std::u16string_view what, bool inbracket, long x, long y)
{
    // TagFindReplace::ChangeText (TagFindReplace.cpp:612-631).
    const auto at = static_cast<std::size_t>(std::max(0L, std::min<long>(x, static_cast<long>(txt.size()))));
    if (!inbracket) {
        txt.insert(at, u"{" + u16(what) + u"}");
        return 1;
    }
    if (x < y) {
        if (static_cast<std::size_t>(y) + 1u >= txt.size())
            txt.erase(at);
        else
            txt.erase(at, static_cast<std::size_t>(y) + 1 - at);
    }
    txt.insert(std::min(at, txt.size()), what);
    return 0;
}

int replaceTag(std::u16string &text, std::u16string_view replaceTxt, bool inBracket, long x, long y)
{
    // TagFindReplace::Replace (TagFindReplace.cpp:423-444).
    if (text.empty()) {
        text += u"{" + u16(replaceTxt) + u"}";
        return 1;
    }
    const auto at = static_cast<std::size_t>(std::max(0L, std::min<long>(x, static_cast<long>(text.size()))));
    if (!inBracket) {
        text.insert(at, u"{" + u16(replaceTxt) + u"}");
        return 1;
    }
    if (x < y) {
        if (static_cast<std::size_t>(y) + 1u >= text.size())
            text.erase(at);
        else
            text.erase(at, static_cast<std::size_t>(y) + 1 - at);
    }
    text.insert(std::min(at, text.size()), replaceTxt);
    return 0;
}

int startMs(const core::LineRecord &line)
{
    return static_cast<int>(line.start.value.microseconds() / 1000);
}

int endMs(const core::LineRecord &line)
{
    return static_cast<int>(line.end.value.microseconds() / 1000);
}

void drawRect(Overlay &out, PointF pos, bool sel, float rcsize)
{
    // Visuals::DrawRect (Visuals.cpp:357-374).
    out.polygons.push_back({{{pos.x - rcsize, pos.y - rcsize},
                             {pos.x + rcsize, pos.y - rcsize},
                             {pos.x + rcsize, pos.y + rcsize},
                             {pos.x - rcsize, pos.y + rcsize}},
                            sel ? kHandleSelectedFill : kHandleFill,
                            kHandleBorder});
}

void drawCircle(Overlay &out, PointF pos, bool sel, float crsize)
{
    // Visuals::DrawCircle (Visuals.cpp:377-400): filled, then its border.
    out.circles.push_back({pos, crsize, sel ? kHandleSelectedFill : kHandleFill, true});
    out.circles.push_back({pos, crsize, kHandleBorder, false});
}

void drawCross(Overlay &out, PointF position, std::uint32_t colour)
{
    // Visuals::DrawCross (Visuals.cpp:339-355): two 30-pixel lines in the
    // two-pixel antialiased D3DX line Draw sets.
    out.lines.push_back({{position.x - 15.0f, position.y}, {position.x + 15.0f, position.y}, 2, colour});
    out.lines.push_back({{position.x, position.y - 15.0f}, {position.x, position.y + 15.0f}, 2, colour});
}

PointF drawArrow(Overlay &out, PointF from, PointF to, int diff)
{
    // Visuals::DrawArrow (Visuals.cpp:308-337).
    const PointF pdiff{from.x - to.x, from.y - to.y};
    const float len = std::sqrt((pdiff.x * pdiff.x) + (pdiff.y * pdiff.y));
    const PointF units = (len == 0) ? PointF{0, 0} : PointF{pdiff.x / len, pdiff.y / len};
    const float back = static_cast<float>(12 + diff);
    const PointF pend{to.x + (units.x * back), to.y + (units.y * back)};
    const PointF halfbase{-units.y * 5.f, units.x * 5.f};
    out.polygons.push_back({{{pend.x - units.x * 12, pend.y - units.y * 12},
                             {pend.x + halfbase.x, pend.y + halfbase.y},
                             {pend.x - halfbase.x, pend.y - halfbase.y}},
                            kHandleFill,
                            kHandleBorder});
    return pend;
}

void drawDashedLine(Overlay &out, const PointF *vector, std::size_t vectorSize, int dashLen, std::uint32_t colour)
{
    // Visuals::DrawDashedLine (Visuals.cpp:402-423).
    PointF actual[2];
    for (std::size_t i = 0; i + 1 < vectorSize; i++) {
        const std::size_t iPlus1 = i + 1;
        const PointF pdiff{vector[i].x - vector[iPlus1].x, vector[i].y - vector[iPlus1].y};
        const float len = std::sqrt((pdiff.x * pdiff.x) + (pdiff.y * pdiff.y));
        if (len == 0)
            return;
        const PointF units{pdiff.x / len, pdiff.y / len};
        const float singleMovement = 1 / (len / (dashLen * 2));
        actual[0] = vector[i];
        actual[1] = actual[0];
        for (float j = 0; j <= 1; j += singleMovement) {
            actual[1] = {actual[1].x - units.x * dashLen, actual[1].y - units.y * dashLen};
            if (j + singleMovement >= 1)
                actual[1] = vector[iPlus1];
            out.lines.push_back({actual[0], actual[1], 2, colour});
            actual[1] = {actual[1].x - units.x * dashLen, actual[1].y - units.y * dashLen};
            actual[0] = {actual[0].x - (units.x * dashLen) * 2, actual[0].y - (units.y * dashLen) * 2};
        }
    }
}

void drawHelperLine(Overlay &out, int x, int y, const VideoView &view)
{
    // Position::Draw / Move::DrawVisual's helper (VisualPosition.cpp:123-131,
    // VisualMove.cpp:56-64): VideoSize's GetWidth/GetHeight are the video
    // rectangle's right and bottom edges (SizeChanged's wxRect).
    const IntRect video = view.videoRect();
    const PointF vertical[2] = {{static_cast<float>(x), 0}, {static_cast<float>(x), static_cast<float>(video.bottom)}};
    const PointF horizontal[2] = {{0, static_cast<float>(y)}, {static_cast<float>(video.right), static_cast<float>(y)}};
    drawDashedLine(out, vertical, 2, 4, kHelperColour);
    drawDashedLine(out, horizontal, 2, 4, kHelperColour);
    drawRect(out, {static_cast<float>(x), static_cast<float>(y)}, false, 4.f);
}

} // namespace hikari::application::visual
