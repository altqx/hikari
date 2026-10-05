#include "hikari/application/visual_drawing.h"

#include "hikari/application/grid_clipboard.h"
#include "hikari/core/legacy_regex.h"
#include "hikari/core/style.h"
#include "hikari/core/tag_commands.h"
#include "hikari/core/text_projection.h"

#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>

// Built without floating-point contraction, as visual_view.cpp: every step
// keeps legacy's float arithmetic and int truncations.

namespace hikari::application::visual {

namespace {

using u16 = std::u16string;
using u16v = std::u16string_view;

// The Shapes rectangle's hit-test results (VisualDrawingShapes.cpp:34-41).
enum { LEFT = 1, RIGHT, TOP = 4, BOTTOM = 8, INSIDE = 16, OUTSIDE = 32 };
// Shapes::DrawVisual's rectangle (VisualDrawingShapes.cpp:494).
constexpr std::uint32_t kShapeRectangle = 0xFFBB0000;

bool isSpace(char16_t c)
{
    return c == u' ' || (c >= u'\t' && c <= u'\r');
}

bool isDigit(char16_t c)
{
    return c >= u'0' && c <= u'9';
}

// wxString::Mid(first): empty past the end.
u16 mid(u16v text, std::size_t first, std::size_t count = u16v::npos)
{
    if (first > text.size())
        return {};
    return u16(text.substr(first, count));
}

// The legacy "find" a size_t result put into an int (npos is -1).
int asInt(std::size_t value)
{
    return static_cast<int>(value);
}

core::legacy::TagEditor find(u16v text, u16v pattern, bool *found = nullptr)
{
    core::legacy::TagEditor editor({u16(text), 0, 0});
    const bool f = editor.findTag(pattern, 1, false);
    if (found)
        *found = f;
    return editor;
}

// TagFindReplace::Replace (TagFindReplace.cpp:423-442).
int replaceFound(const core::legacy::TagEditor &found, u16v replaceTxt, u16 &text)
{
    const auto [x, y] = found.position();
    if (text.empty()) {
        text += u"{" + u16(replaceTxt) + u"}";
        return 1;
    }
    if (!found.inBracket()) {
        text.insert(static_cast<std::size_t>(x), u"{" + u16(replaceTxt) + u"}");
        return 1;
    }
    if (x < y) {
        if (static_cast<std::size_t>(y) + 1u >= text.length())
            text.erase(static_cast<std::size_t>(x));
        else
            text.erase(static_cast<std::size_t>(x), static_cast<std::size_t>(y - x + 1));
    }
    text.insert(static_cast<std::size_t>(x), replaceTxt);
    return 0;
}

// wxString::ToDouble in the C locale (the reading of \pos values and of a
// Style's scale, as the other ports read them).
bool toDouble(u16v text, double &out)
{
    return legacy::cDouble(text, out);
}

// Styles::GetScaleXDouble / GetScaleYDouble (styles.cpp:176-192).
double styleScale(const std::u8string &value)
{
    const u16 text = core::toUtf16(value);
    double scale = 100.;
    if (!toDouble(text, scale))
        scale = legacy::atoi(text);
    return scale;
}

// SubsFile::GetStyle(0, name): the first Style of that name, else the first
// Style, else Styles() (styles.cpp:249-275).
core::StyleValues lineStyle(const core::Document &document, const std::u8string &name)
{
    const auto styles = core::decodeStyles(document);
    for (const auto &s : styles)
        if (s.name == name)
            return s;
    if (!styles.empty())
        return styles.front();
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

int marginOr(const core::IntField &field, const std::u8string &styleValue)
{
    return field.value != 0 ? static_cast<int>(field.value) : legacy::atoi(core::toUtf16(styleValue));
}

// Dialogue::GetDefaultPosition (SubsDialogue.cpp:609-634).
PointF defaultPosition(const core::LineRecord &line, const core::StyleValues &style, int an, int width, int height)
{
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

// wxStringTokenizer(text, ",") in its default mode for a non-blank
// delimiter: every token, empty ones too.
std::vector<u16> tokens(u16v text, char16_t delimiter)
{
    std::vector<u16> out;
    if (text.empty())
        return out;
    std::size_t start = 0;
    while (true) {
        const auto at = text.find(delimiter, start);
        if (at == u16v::npos) {
            out.emplace_back(text.substr(start));
            break;
        }
        out.emplace_back(text.substr(start, at - start));
        start = at + 1;
        if (start == text.size())
            break; // wx: a trailing delimiter ends the tokens
    }
    return out;
}

int startMs(const core::LineRecord &line)
{
    return static_cast<int>(line.start.value.microseconds() / 1000);
}

int endMs(const core::LineRecord &line)
{
    return static_cast<int>(line.end.value.microseconds() / 1000);
}

// The active Line as the Line editor shows it (the pending draft applied).
std::optional<core::LineRecord> activeRecord(const VisualHost &host)
{
    const EditSession *session = host.session();
    const auto active = host.activeLine();
    if (!session || !active)
        return std::nullopt;
    if (const auto draft = session->draftRecord(); draft && draft->id == *active)
        return draft;
    for (const auto *line : session->document().lines())
        if (line->id == *active)
            return *line;
    return std::nullopt;
}

// The text a visual tool edits: the translation in TLMode unless it is
// empty (legacy's TextEdit / TextEditOrig and Dialogue::GetTextNoCopy).
bool editsTranslation(const VisualHost &host, const core::LineRecord &line)
{
    return host.session() && translationMode(*host.session()) && !line.translation.empty();
}

u16 editedText(const VisualHost &host, const core::LineRecord &line)
{
    return core::toUtf16(editsTranslation(host, line) ? line.translation : line.text);
}

Gesture *ensureGesture(VisualHost &host, bool &began)
{
    if (Gesture *g = host.gesture())
        return g;
    if (began)
        return nullptr;
    auto begun = host.beginGesture(host.batchTargets(), std::string(familyInfo(Family::Drawing).history));
    if (!begun)
        return nullptr;
    began = true;
    return *begun;
}

void stage(Gesture &g, core::LineId id, bool tl, const u16 &before, const u16 &after)
{
    if (after != before || g.staged(id, tl))
        g.stage(id, core::toUtf8(after), tl);
}

u16 currentText(const VisualHost &host, const Gesture &g, core::LineId id, bool *tl)
{
    const core::LineRecord &before = g.before(id);
    *tl = editsTranslation(host, before);
    if (const auto staged = g.staged(id, *tl))
        return core::toUtf16(*staged);
    return editedText(host, before);
}

} // namespace

namespace drawing {

std::vector<Tag> parseTags(u16v txt, const std::vector<u16v> &names, bool plainText)
{
    std::vector<Tag> out;
    std::size_t pos = 0, plainStart = 0;
    bool hasDrawing = false;
    const std::size_t len = txt.size();
    bool tagsBlock = false;
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
                pos++;
            if ((plainText || hasDrawing) && plainStart + 1 <= pos)
                out.push_back({hasDrawing ? u"pvector" : u"plain", mid(txt, plainStart, pos - plainStart), plainStart});
        } else if (tagsBlock && ch == u'\\') {
            pos++;
            const std::size_t slashPos = txt.find(u'\\', pos);
            const std::size_t bracketPos = txt.find(u'}', pos);
            const std::size_t tagEnd = (slashPos == u16v::npos && bracketPos == u16v::npos) ? len
                                       : (slashPos == u16v::npos)                           ? bracketPos
                                       : (bracketPos == u16v::npos)                         ? slashPos
                                       : (bracketPos < slashPos)                            ? bracketPos
                                                                                            : slashPos;
            u16 tag = mid(txt, pos, tagEnd - pos);
            if (!tag.empty() && tag.back() == u')')
                tag.pop_back();
            for (const u16v tagName : names) {
                const std::size_t tagLen = tagName.size();
                if (tag.size() > tagLen && u16v(tag).substr(0, tagLen) == tagName) {
                    const char16_t first = tag[tagLen];
                    if (first == u'(' || isDigit(first) || tagName == u"fn" || first == u'.' || first == u'-' ||
                        first == u'+') {
                        Tag newTag{u16(tagName), {}, pos + tagLen};
                        u16 tagValue = tag.substr(tagLen);
                        if (tagName == u"p") {
                            // Trim().Trim(false), in place.
                            std::size_t b = 0, e = tagValue.size();
                            while (e > 0 && isSpace(tagValue[e - 1]))
                                --e;
                            while (b < e && isSpace(tagValue[b]))
                                ++b;
                            tagValue = tagValue.substr(b, e - b);
                            hasDrawing = tagValue != u"0";
                            newTag.value = tagValue;
                        } else if (tag[tagLen] == u'(') {
                            newTag.start++;
                            const auto open = tagValue.find(u'(');
                            const u16 after = open == u16::npos ? u16() : tagValue.substr(open + 1);
                            newTag.value = after.substr(0, after.find(u')'));
                        } else {
                            double tmpValue = 0;
                            if (tagName != u"fn" && !legacy::cDouble(tagValue, tmpValue)) {
                                u16 newTagValue;
                                for (const char16_t c : tagValue) {
                                    if (!isDigit(c) && c != u'.' && c != u'-' && c != u'+')
                                        break;
                                    newTagValue += c;
                                }
                                tagValue = newTagValue;
                            }
                            newTag.value = tagValue;
                        }
                        out.push_back(std::move(newTag));
                        pos = tagEnd - 1;
                        break;
                    }
                }
            }
        }
        pos++;
    }
    return out;
}

PointF drawingSize(int alignment, const std::vector<VectorPoint> &points, bool withoutAlignment)
{
    if ((alignment == 7 && !withoutAlignment) || points.size() < 1)
        return {0, 0};
    float minx = FLT_MAX, miny = FLT_MAX, maxx = -FLT_MAX, maxy = -FLT_MAX;
    for (const VectorPoint &p : points) {
        if (p.x < minx)
            minx = p.x;
        if (p.y < miny)
            miny = p.y;
        if (p.x > maxx)
            maxx = p.x;
        if (p.y > maxy)
            maxy = p.y;
    }
    const PointF sizes{(maxx - minx), (maxy - miny)};
    if (withoutAlignment)
        return sizes;
    PointF result{0, 0};
    if (alignment % 3 == 2)
        result.x = static_cast<float>(sizes.x / 2.0);
    else if (alignment % 3 == 0)
        result.x = sizes.x;
    if (alignment < 4)
        result.y = sizes.y;
    else if (alignment < 7)
        result.y = static_cast<float>(sizes.y / 2.0);
    return result;
}

void rotate(VectorPoint &point, float sinOfAngle, float cosOfAngle, PointF orgpivot)
{
    const float x = point.x + orgpivot.x;
    const float y = point.y + orgpivot.y;
    point.x = (x * cosOfAngle) - (y * sinOfAngle) - orgpivot.x;
    point.y = (x * sinOfAngle) + (y * cosOfAngle) - orgpivot.y;
}

LineDrawing readLine(const core::Document &document, const core::LineRecord &line, u16v txt, u16v lineText,
                     int scriptWidth, int scriptHeight, const std::array<double, 7> &moveTable)
{
    LineDrawing out;
    out.move = moveTable;
    out.start = startMs(line);
    out.end = endMs(line);
    double *tbl = out.move.data();
    const core::StyleValues currentStyle = lineStyle(document, line.style);

    // Visuals::GetPosnScale (Visuals.cpp:624-722) for VECTORDRAW.
    PointF ppos{0.0f, 0.0f};
    bool foundpos = false;
    static const core::LegacyRegex pos(u"\\\\(pos|move)\\(([^\\)]+)\\)", core::LegacyRegex::Advanced);
    if (pos.matches(txt)) {
        const auto g = pos.match(2);
        const u16 txtpos = g ? mid(txt, g->first, g->second) : u16();
        int ipos = 0;
        for (const u16 &token : tokens(txtpos, u',')) {
            if (ipos >= 6)
                break;
            double value = 0;
            tbl[ipos] = toDouble(token, value) ? value : 0;
            ipos++;
        }
        tbl[4] += out.start;
        tbl[5] += out.start;
        tbl[6] = ipos;
        if (ipos > 1) {
            ppos.x = static_cast<float>(tbl[0]);
            ppos.y = static_cast<float>(tbl[1]);
            foundpos = true;
        }
    } else {
        tbl[6] = 0;
        // The margins and GetDialogueAdditionalPosition's stacking: the
        // default position below replaces them for the drawing.
    }
    if (tbl[6] < 4) {
        // GetMoveTimes: the \move times from the Line's frames. Only a
        // \move with six values keeps its own (CalcMovePos replaces the
        // others' with the Line's times), so these are never read.
        tbl[4] = (out.start / 10) * 10;
        tbl[5] = (out.start / 10) * 10;
    }

    double fscx = 100.0, fscy = 100.0;
    {
        bool found = false;
        const auto f = find(txt, u"fscx([.0-9-]+)", &found);
        if (!(found && toDouble(f.finding(), fscx)))
            fscx = styleScale(currentStyle.scaleX);
    }
    {
        bool found = false;
        const auto f = find(txt, u"fscy([.0-9-]+)", &found);
        if (!(found && toDouble(f.finding(), fscy)))
            fscy = styleScale(currentStyle.scaleY);
    }
    out.scale.x = static_cast<float>(fscx / 100.f);
    out.scale.y = static_cast<float>(fscy / 100.f);
    static const core::LegacyRegex drawscale(u"\\\\p([0-9]+)", core::LegacyRegex::Advanced);
    int dscale = 1;
    if (drawscale.matches(txt)) {
        const auto m = drawscale.match(1);
        dscale = m ? legacy::atoi(txt.substr(m->first, m->second)) : 0;
        out.vectorScale = dscale;
    }
    if (dscale > 1)
        dscale = legacy::toInt(std::pow(2.f, (dscale - 1.f)));
    else
        dscale = 1;
    out.scale.x /= dscale;
    out.scale.y /= dscale;
    int tmpan = legacy::atoi(core::toUtf16(currentStyle.alignment));
    static const core::LegacyRegex an(u"\\\\an([0-9]+)", core::LegacyRegex::Advanced);
    if (an.matches(txt)) {
        const auto m = an.match(1);
        tmpan = m ? legacy::atoi(txt.substr(m->first, m->second)) : 0;
    }
    out.alignment = static_cast<unsigned char>(tmpan); // legacy's byte
    out.position = foundpos ? ppos : defaultPosition(line, currentStyle, tmpan, scriptWidth, scriptHeight);

    // DrawingAndClip::SetCurVisual (VisualClips.cpp:221-268): the drawing
    // from the Line's \p tags, the first one skipped.
    const auto tags = parseTags(lineText, {u"p"});
    if (tags.size() >= 2) {
        for (std::size_t i = 1; i < tags.size(); i++) {
            if (tags[i].name == u"p") {
                const int vscale = legacy::atoi(tags[i].value);
                if (vscale > 0)
                    out.vectorScale = vscale;
            } else if (tags[i].name == u"pvector") {
                out.body = tags[i].value;
                break;
            }
        }
    }
    {
        bool found = false;
        const auto f = find(txt, u"frz?([0-9.-]+)", &found);
        double frzd = 0.;
        if (found) {
            (void)toDouble(f.finding(), frzd); // GetDouble
            out.frz = static_cast<float>(frzd);
        } else {
            double result = 0.;
            (void)toDouble(core::toUtf16(currentStyle.angle), result);
            out.frz = static_cast<float>(result);
        }
    }
    {
        bool found = false;
        const auto f = find(txt, u"org(\\([^\\)]+)", &found);
        if (found) {
            // GetTwoValueDouble (TagFindReplace.cpp): it splits the finding
            // itself, "(" included, so the first value never reads.
            const u16 &finding = f.finding();
            const auto comma = finding.find(u',');
            const u16 fval = finding.substr(0, comma);
            const u16 sval = comma == u16::npos ? u16() : finding.substr(comma + 1);
            double orx = 0, ory = 0;
            if (toDouble(fval, orx) && toDouble(sval, ory))
                out.org = PointF{static_cast<float>(orx), static_cast<float>(ory)};
        }
    }
    return out;
}

PointF movePosition(std::array<double, 7> &moveValues, int start, int end, int time)
{
    PointF ppos;
    if (moveValues[6] < 6) {
        moveValues[4] = start;
        moveValues[5] = end;
    }
    const float tmpt = static_cast<float>(time - moveValues[4]);
    const float tmpt1 = static_cast<float>(moveValues[5] - moveValues[4]);
    const float actime = tmpt / tmpt1;
    float distx, disty;
    if (time < moveValues[4]) {
        distx = static_cast<float>(moveValues[0]), disty = static_cast<float>(moveValues[1]);
    } else if (time > moveValues[5]) {
        distx = static_cast<float>(moveValues[2]), disty = static_cast<float>(moveValues[3]);
    } else {
        distx = static_cast<float>(moveValues[0] - ((moveValues[0] - moveValues[2]) * actime));
        disty = static_cast<float>(moveValues[1] - ((moveValues[1] - moveValues[3]) * actime));
    }
    ppos.x = distx, ppos.y = disty;
    return ppos;
}

PutResult putDrawing(u16 text, u16v clip, PointF position, int alignment, const SetScale &setScale)
{
    u16 &txt = text;
    bool isf = false;
    bool hasP1 = true;
    const std::size_t cliplen = clip.size();
    (void)cliplen;
    auto found = find(txt, u"p([0-9]+)", &isf);
    if (!isf) {
        replaceFound(found, u"\\p1", txt);
        hasP1 = false;
    }
    found = find(txt, u"pos|move\\(([,. 0-9-]+)\\)", &isf);
    if (!isf)
        replaceFound(found, u"\\pos(" + legacy::getfloat(position.x) + u"," + legacy::getfloat(position.y) + u")", txt);
    found = find(txt, u"an([0-9])", &isf);
    if (!isf)
        replaceFound(found, u"\\an" + legacy::getfloat(static_cast<float>(alignment), "1.0f"), txt);

    int bracketPos = 0;
    while (true) {
        bracketPos = asInt(txt.find(u"}", static_cast<std::size_t>(bracketPos)));
        if (bracketPos < 0 || static_cast<std::size_t>(bracketPos) == txt.length() - 1)
            break;
        bracketPos++;
        const u16 mcheck = mid(txt, static_cast<std::size_t>(bracketPos), 2);
        if (!mcheck.starts_with(u"m ") && !mcheck.starts_with(u"{")) {
            txt.insert(static_cast<std::size_t>(bracketPos), u"{");
            bracketPos++;
            bracketPos = asInt(txt.find(u"{", static_cast<std::size_t>(bracketPos)));
            if (bracketPos < 0) {
                txt += u"}";
                break;
            }
            txt.insert(static_cast<std::size_t>(bracketPos), u"}");
        }
    }

    // GetPositionInText(): the last FindTag's (\an's) place.
    const std::size_t positionY = static_cast<std::size_t>(found.position().second);
    const u16 afterP1 = mid(txt, positionY);
    const int afterBracket = asInt(afterP1.find(u"}") + 1);
    int Mpos = afterBracket;
    if (hasP1) {
        const u16 afterP1Bracket = mid(afterP1, static_cast<std::size_t>(afterBracket));
        Mpos = asInt(afterP1Bracket.find(u"m "));
        if (Mpos == -1)
            Mpos = afterBracket;
        else
            Mpos += afterBracket;
    }
    const u16 startM = mid(afterP1, static_cast<std::size_t>(Mpos));
    int endDrawing = asInt(startM.find(u"{"));
    u16 p0;
    if (endDrawing == -1) {
        if (hasP1)
            endDrawing = asInt(startM.length());
        else
            endDrawing = 0;
        p0 += u"{\\p0}";
    } else if (!hasP1) {
        p0 += u"{\\p0}";
    }
    std::size_t startOfDrawing = static_cast<std::size_t>(Mpos) + positionY;
    txt.replace(startOfDrawing, static_cast<std::size_t>(endDrawing), u16(clip) + p0);
    if (setScale) {
        int diff = 0;
        setScale(txt, startOfDrawing, &diff);
        startOfDrawing += diff;
        endDrawing += diff;
    }
    return {std::move(text), startOfDrawing, endDrawing};
}

} // namespace drawing

// ---------------------------------------------------------------------------
// The drawing tool.

DrawingTool::DrawingTool()
{
    m_editor.drawing = true;
    m_editor.modeCount = 6; // VectorItem(false): no Invert clip button
}

VectorFrame DrawingTool::frame(const VisualHost &host) const
{
    const VideoView &v = host.view();
    VectorFrame f;
    f.coeffW = v.coeffW();
    f.coeffH = v.coeffH();
    f.coeffW /= m_scale.x; // SetCurVisual: coeffW /= scale.x
    f.coeffH /= m_scale.y;
    f.offsetX = m_x;
    f.offsetY = m_y;
    f.zoomMove = v.zoomMove();
    f.zoomScale = v.zoomScale();
    f.videoRect = v.videoRect();
    return f;
}

void DrawingTool::syncMove(const VisualHost &host) const
{
    // DrawingAndClip::DrawVisual (VisualClips.cpp:117-121): a drawing that
    // moves (\move with three values or more) follows the video's time.
    if (m_shape < 0 && m_move[6] > 2) {
        const PointF movePos = drawing::movePosition(m_move, m_start, m_end, static_cast<int>(host.videoTimeMs()));
        m_x = movePos.x / m_scale.x;
        m_y = movePos.y / m_scale.y;
    }
}

void DrawingTool::readActive(VisualHost &host)
{
    // DrawingAndClip::SetCurVisual for VECTORDRAW (VisualClips.cpp:182-296).
    const auto active = activeRecord(host);
    const EditSession *session = host.session();
    m_editor.points.clear();
    if (!active || !session) {
        return;
    }
    const u16 text = editedText(host, *active);
    const drawing::LineDrawing line =
        drawing::readLine(session->document(), *active, text, text, host.view().scriptWidth(),
                          host.view().scriptHeight(), m_move);
    m_scale = line.scale;
    m_alignment = line.alignment;
    // GetPosnScale set vectorScale to any \p it matched, \p0 included
    // (Visuals.cpp:699); SetCurVisual's loop only to a later positive one.
    if (line.vectorScale)
        m_vectorScale = *line.vectorScale;
    m_move = line.move;
    m_start = line.start;
    m_end = line.end;
    m_x = line.position.x / m_scale.x;
    m_y = (line.position.y / m_scale.y);
    m_frz = line.frz;
    m_org = line.org ? PointF{line.org->x / m_scale.x, line.org->y / m_scale.y} : PointF{m_x, m_y};
    m_editor.points = parseVectorPoints(line.body);
    if (!m_editor.points.empty()) {
        const PointF offsetxy = drawing::drawingSize(m_alignment, m_editor.points);
        const float rad = 0.01745329251994329576923690768489f;
        const PointF orgpivot{std::fabs(m_org.x - m_x), std::fabs(m_org.y - m_y)};
        const float s = std::sin(-m_frz * rad);
        const float c = std::cos(-m_frz * rad);
        for (VectorPoint &p : m_editor.points) {
            p.x -= offsetxy.x;
            p.y -= offsetxy.y;
            if (m_frz) {
                p.x *= m_scale.x;
                p.y *= m_scale.y;
                drawing::rotate(p, s, c, orgpivot);
                p.x /= m_scale.x;
                p.y /= m_scale.y;
            }
        }
    }
}

void DrawingTool::reset(VisualHost &host)
{
    // Visuals::SetVisual: ChangeTool(the toolbar's, blockSetCurVisual) then
    // SetCurVisual; the renderer drops its cached Line (dummytext).
    m_editor.endDrag();
    m_inKey = m_keyCommit = false;
    m_placed = false;
    changeShape(m_shapeSelection, false, host);
    readActive(host);
    if (m_shape < 0)
        m_editor.normaliseFirst(); // DrawVisual's, as legacy rendered after SetVisual
    syncMove(host);
    host.toolChanged();
}

void DrawingTool::changeShape(int listSelection, bool fromToolbar, VisualHost &host)
{
    // DrawingAndClip::ChangeTool for VECTORDRAW: Shapes::SetShape, then a
    // return to free drawing from the toolbar reads the Line again.
    const int shapeSelectionNew = listSelection;
    {
        // Shapes::SetShape (VisualDrawingShapes.cpp:499-520).
        const std::vector<ShapePreset> *shapes = host.shapePresets();
        const int curshape = shapeSelectionNew;
        if (shapes && !shapes->empty() && curshape - 1 != m_shape) {
            if (curshape <= 0) {
                m_shape = -1;
            } else {
                if (curshape > static_cast<int>(shapes->size()))
                    m_shape = static_cast<int>(shapes->size()) - 1;
                else
                    m_shape = curshape - 1; // the list's first entry is "Choose"
                m_current = (*shapes)[static_cast<std::size_t>(m_shape)];
                m_shapePoints = parseVectorPoints(m_current.shape);
                m_shapeSize = drawing::drawingSize(m_alignment, m_shapePoints, true);
            }
        }
    }
    if (shapeSelectionNew == 0 && m_shapeSelection != 0 && fromToolbar) {
        m_shapeSelection = shapeSelectionNew;
        readActive(host);
        m_editor.normaliseFirst();
    }
    m_shapeSelection = shapeSelectionNew;
}

std::u16string DrawingTool::drawingBody() const
{
    // DrawingAndClip::GetVisual for VECTORDRAW (VisualClips.cpp:298-363).
    std::vector<VectorPoint> points = m_editor.points;
    if (m_frz) {
        const float rad = 0.01745329251994329576923690768489f;
        const PointF orgpivot{std::fabs(m_org.x - m_x), std::fabs(m_org.y - m_y)};
        const float s = std::sin(m_frz * rad);
        const float c = std::cos(m_frz * rad);
        for (VectorPoint &p : points) {
            p.x *= m_scale.x;
            p.y *= m_scale.y;
            drawing::rotate(p, s, c, orgpivot);
            p.x /= m_scale.x;
            p.y /= m_scale.y;
        }
    }
    const PointF offsetxy = drawing::drawingSize(m_alignment, points);
    return serializeVectorPoints(points, "6.2f", offsetxy);
}

PointF DrawingTool::drawingAnchor() const
{
    // Shapes::CalcDrawingAnchor (VisualDrawingShapes.cpp:760-776).
    if (m_shapePoints.size() < 1)
        return {0, 0};
    float minx = FLT_MAX, miny = FLT_MAX;
    for (VectorPoint p : m_shapePoints) {
        p.x *= m_shapeScale.x;
        p.y *= m_shapeScale.y;
        if (p.x < minx)
            minx = p.x;
        if (p.y < miny)
            miny = p.y;
    }
    return {-(minx), -(miny)};
}

std::u16string DrawingTool::shapeBody() const
{
    // Shapes::GetVisual (VisualDrawingShapes.cpp:522-602).
    if (!(m_shapeScale.x > 0.f && m_rectVisible))
        return {};
    PointF offsetxy = drawingAnchor();
    const float rectx = m_rect[0].x < m_rect[1].x ? m_rect[0].x : m_rect[1].x;
    const float recty = m_rect[0].y < m_rect[1].y ? m_rect[0].y : m_rect[1].y;
    const float rectx1 = m_rect[0].x > m_rect[1].x ? m_rect[0].x : m_rect[1].x;
    const float recty1 = m_rect[0].y > m_rect[1].y ? m_rect[0].y : m_rect[1].y;
    offsetxy.x -= ((m_x - rectx) - 1);
    offsetxy.y -= ((m_y - recty) - 1);
    if (m_alignment % 3 == 2)
        offsetxy.x += (rectx1 - rectx) / 2;
    else if (m_alignment % 3 == 0)
        offsetxy.x += (rectx1 - rectx);
    if (m_alignment < 4)
        offsetxy.y += (recty1 - recty);
    else if (m_alignment < 7)
        offsetxy.y += (recty1 - recty) / 2;
    std::vector<VectorPoint> points = m_shapePoints;
    for (VectorPoint &p : points) {
        p.x = (p.x * m_shapeScale.x);
        p.y = (p.y * m_shapeScale.y);
    }
    return serializeVectorPoints(points, "6.2f", offsetxy);
}

std::u16string DrawingTool::body() const
{
    // GetVisual is virtual: Shapes' while a shape is chosen.
    return m_shape >= 0 ? shapeBody() : drawingBody();
}

void DrawingTool::setScaleTags(u16 &txt, std::size_t position, int *diff) const
{
    // Shapes::SetScale (VisualDrawingShapes.cpp:604-625): FindTag from the
    // start (SetFromTo(0, position), mode 3). `diff` is legacy's: the old
    // tag's length less the new one's, which ChangeVectorVisual adds to the
    // drawing's place.
    (void)position;
    if (m_current.scalingMode == ShapePreset::ChangeScale && m_shapeScale.x > 0.f) {
        auto f = find(txt, u"fscx([0-9.-]+)");
        auto textPos = f.position();
        u16 newTag = u"\\fscx" + legacy::getfloat(m_shapeScale.x * 100);
        replaceFound(f, newTag, txt);
        if (diff)
            *diff = static_cast<int>((textPos.second - textPos.first + 1) - static_cast<long>(newTag.length()));
        if (m_current.mode != ShapePreset::OnlyScaleX) {
            f = find(txt, u"fscy([0-9.-]+)");
            textPos = f.position();
            newTag = u"\\fscy" + legacy::getfloat(m_shapeScale.y * 100);
            replaceFound(f, newTag, txt);
            if (diff)
                *diff += static_cast<int>((textPos.second - textPos.first + 1) - static_cast<long>(newTag.length()));
        }
    }
}

VectorEditor::Callbacks DrawingTool::callbacks(VisualHost &host)
{
    VectorEditor::Callbacks cb;
    cb.apply = [this, &host](bool commit) { apply(host, commit); };
    cb.bell = [&host] { host.bell(); };
    cb.notice = [&host](std::u16string_view text) { host.notice(text); };
    return cb;
}

void DrawingTool::apply(VisualHost &host, bool commit)
{
    // DrawingAndClip::SetClip(!commit) for VECTORDRAW (VisualClips.cpp:365-543).
    if (commit && m_inKey) {
        m_keyCommit = true; // a nudge or Delete commits on its key's release
        return;
    }
    Gesture *g = ensureGesture(host, m_began);
    if (!g) {
        if (commit)
            m_began = false;
        return;
    }
    const u16 clip = body();
    const PointF position{m_x * m_scale.x, m_y * m_scale.y}; // ChangeVectorVisual's \pos
    const drawing::SetScale setScale =
        m_shapeSelection ? drawing::SetScale([this](u16 &t, std::size_t p, int *d) { setScaleTags(t, p, d); })
                         : drawing::SetScale();
    if (g->targets().size() > 1) {
        // Several Lines (legacy several selected): each one's drawing is
        // replaced from its own text, on every sample and again on release.
        for (const auto &id : g->targets()) {
            const core::LineRecord &before = g->before(id);
            const bool tl = editsTranslation(host, before);
            const u16 text = editedText(host, before);
            stage(*g, id, tl, text, drawing::putDrawing(text, clip, position, m_alignment, setScale).text);
        }
    } else if (!commit) {
        // One Line: the first sample after a reset rewrites the editor's
        // text; the next ones replace only the drawing where it went
        // (legacy's dummytext and dumplaced).
        const core::LineId id = g->targets().front();
        bool tl = false;
        u16 current = currentText(host, *g, id, &tl);
        const u16 original = editedText(host, g->before(id));
        if (!m_placed || m_placedStart > current.size()) {
            const drawing::PutResult put = drawing::putDrawing(current, clip, position, m_alignment, setScale);
            current = put.text;
            m_placed = true;
            m_placedStart = put.start;
            m_placedEnd = put.start + clip.size();
        } else {
            current.replace(m_placedStart, m_placedEnd - m_placedStart, clip);
            m_placedEnd = m_placedStart + clip.size();
        }
        stage(*g, id, tl, original, current);
    }
    if (commit) {
        m_began = false;
        (void)host.commitGesture();
    }
}

void DrawingTool::pointer(const Pointer &event, VisualHost &host)
{
    syncMove(host);
    if (m_shape >= 0) {
        shapePointer(event, host);
        host.toolChanged();
        return;
    }
    if (event.kind == Pointer::Kind::Wheel) {
        // Ctrl+wheel resizes the video window and never reaches the tool.
        if (event.control || event.wheelSteps == 0)
            return;
        m_editor.wheel(event.wheelSteps);
        host.toolChanged();
        return;
    }
    if (event.kind == Pointer::Kind::Press && !host.gesture())
        m_began = false;
    m_inKey = false;
    m_editor.pointer(event, frame(host), callbacks(host));
    m_editor.normaliseFirst();
    host.toolChanged();
}

bool DrawingTool::key(const Key &event, VisualHost &host)
{
    // DrawingAndClip::OnKeyPress (Shapes does not override it: the keys move
    // the Line's points, and the edit writes GetVisual, the shape's while
    // one is chosen).
    syncMove(host);
    const bool nudge = event.key == keys::W || event.key == keys::S || event.key == keys::A ||
                       event.key == keys::D || event.key == keys::Delete;
    if (event.release) {
        if (!nudge || !m_keyCommit)
            return false;
        if (event.autoRepeat)
            return true;
        m_keyCommit = false;
        m_inKey = false;
        if (host.gesture() && m_began) {
            m_began = false;
            (void)host.commitGesture();
        }
        host.toolChanged();
        return true;
    }
    if (!host.gesture())
        m_began = false;
    m_inKey = true;
    const bool used = m_editor.key(event, frame(host), callbacks(host));
    m_inKey = false;
    m_editor.normaliseFirst();
    if (used)
        host.toolChanged();
    return used;
}

// --- Shapes ------------------------------------------------------------------

PointF DrawingTool::pointToVideo(PointF point, const VisualHost &host) const
{
    const VectorFrame f = frame(host);
    const float pointx = ((point.x / f.coeffW) - f.zoomMove.x) * f.zoomScale.x,
                pointy = ((point.y / f.coeffH) - f.zoomMove.y) * f.zoomScale.y;
    return {pointx, pointy};
}

PointF DrawingTool::pointToSubtitles(float x, float y, const VisualHost &host) const
{
    const VectorFrame f = frame(host);
    const float pointx = ((x / f.zoomScale.x) + f.zoomMove.x) * f.coeffW,
                pointy = ((y / f.zoomScale.y) + f.zoomMove.y) * f.coeffH;
    return {pointx, pointy};
}

int DrawingTool::hitTest(PointF pos, bool diff, const VisualHost &host)
{
    // Shapes::HitTest (VisualDrawingShapes.cpp:627-665), its x/y mix-up and
    // int truncations kept.
    int resultX = 0, resultY = 0, resultInside = 0, resultFinal = 0, oldpointx = 0, oldpointy = 0;
    for (int i = 0; i < 2; i++) {
        const PointF point = pointToVideo(m_rect[i], host);
        if (std::fabs(pos.x - point.x) < 5) {
            if (diff)
                m_diffX = legacy::toInt(point.x - pos.x);
            resultX |= (i + 1);
        }
        if (std::fabs(pos.y - point.y) < 5) {
            if (diff)
                m_diffY = legacy::toInt(point.y - pos.y);
            resultY |= ((i + 1) * 4);
        }
        if (i) {
            resultInside |= (resultX || (oldpointx <= point.x && oldpointx <= pos.x && point.x >= pos.x) ||
                             (oldpointx >= point.x && oldpointx >= pos.x && point.x <= pos.x))
                                ? INSIDE
                                : OUTSIDE;
            resultInside |= (resultY || (oldpointx <= point.x && oldpointy <= pos.y && point.y >= pos.y) ||
                             (oldpointx >= point.x && oldpointy >= pos.y && point.y <= pos.y))
                                ? INSIDE
                                : OUTSIDE;
        } else {
            oldpointx = legacy::toInt(point.x);
            oldpointy = legacy::toInt(point.y);
        }
    }
    resultFinal = (resultInside & OUTSIDE) ? OUTSIDE : INSIDE;
    if (resultFinal == INSIDE) {
        resultFinal |= resultX;
        resultFinal |= resultY;
        if (resultFinal > INSIDE)
            resultFinal ^= INSIDE;
    }
    return resultFinal;
}

void DrawingTool::sortPoints()
{
    if (m_rect[1].y < m_rect[0].y) {
        const float tmpy = m_rect[0].y;
        m_rect[0].y = m_rect[1].y;
        m_rect[1].y = tmpy;
    }
    if (m_rect[1].x < m_rect[0].x) {
        const float tmpx = m_rect[0].x;
        m_rect[0].x = m_rect[1].x;
        m_rect[1].x = tmpx;
    }
}

void DrawingTool::setDrawingScale()
{
    // Shapes::SetDrawingScale (VisualDrawingShapes.cpp:681-713): under ten
    // units wide or high the shape is not written.
    if (m_rectVisible) {
        const float rectx = m_rect[0].x < m_rect[1].x ? m_rect[0].x : m_rect[1].x;
        const float recty = m_rect[0].y < m_rect[1].y ? m_rect[0].y : m_rect[1].y;
        const float rectx1 = m_rect[0].x > m_rect[1].x ? m_rect[0].x : m_rect[1].x;
        const float recty1 = m_rect[0].y > m_rect[1].y ? m_rect[0].y : m_rect[1].y;
        if (rectx >= rectx1 - 10) {
            m_shapeScale.x = 0.f;
        } else {
            const float rectSizeX = rectx1 - rectx;
            m_shapeScale.x = rectSizeX / m_shapeSize.x;
        }
        if (recty >= recty1 - 10) {
            m_shapeScale.y = 0.f;
        } else {
            const float rectSizeY = recty1 - recty;
            m_shapeScale.y = rectSizeY / m_shapeSize.y;
        }
    }
}

void DrawingTool::setSquareShape(bool axisX)
{
    // Shapes::SetSquareShape (VisualDrawingShapes.cpp:715-744): the shape's
    // proportions along the axis dragged.
    const float rectx = m_rect[0].x < m_rect[1].x ? m_rect[0].x : m_rect[1].x;
    const float rectx1 = m_rect[0].x > m_rect[1].x ? m_rect[0].x : m_rect[1].x;
    const float recty = m_rect[0].y < m_rect[1].y ? m_rect[0].y : m_rect[1].y;
    const float recty1 = m_rect[0].y > m_rect[1].y ? m_rect[0].y : m_rect[1].y;
    const float aspectRatio = m_shapeSize.y == 0 ? 1 : m_shapeSize.x / m_shapeSize.y;
    if (axisX) {
        const float height = recty + ((rectx1 - rectx) / aspectRatio);
        if (m_rect[0].y > m_rect[1].y)
            m_rect[0].y = height;
        else
            m_rect[1].y = height;
    } else {
        const float width = rectx + ((recty1 - recty) * aspectRatio);
        if (m_rect[0].x > m_rect[1].x)
            m_rect[0].x = width;
        else
            m_rect[1].x = width;
    }
}

void DrawingTool::shapePointer(const Pointer &event, VisualHost &host)
{
    // Shapes::OnMouseEvent (VisualDrawingShapes.cpp:359-474). Legacy's
    // cursor shapes over the edges are not ported (as T4's rectangle clip).
    const bool click = event.kind == Pointer::Kind::Press && event.button == Pointer::Button::Left;
    const bool holding = event.leftDown && event.kind != Pointer::Kind::Release;
    int x = event.x, y = event.y;
    const VectorFrame f = frame(host);
    if (event.kind == Pointer::Kind::Release) {
        if (m_rectVisible) {
            if (m_rect[1].y == m_rect[0].y || m_rect[1].x == m_rect[0].x)
                m_rectVisible = false;
            sortPoints();
        }
        if (m_rectVisible) {
            setDrawingScale();
            apply(host, true);
        } else if (host.gesture() && m_began) {
            // A rectangle without width or height is not written: its
            // preview goes (legacy left it in the Line editor, unsent; as
            // the approved T4-zero-rect-preview for the rectangle clip).
            m_began = false;
            host.cancelGesture();
        }
    }
    if (event.kind == Pointer::Kind::Press && event.button == Pointer::Button::Left && !host.gesture())
        m_began = false;
    if (click) {
        m_shapeGrabbed = OUTSIDE;
        const PointF point = pointToSubtitles(static_cast<float>(x), static_cast<float>(y), host);
        if (m_rectVisible) {
            m_shapeGrabbed = hitTest({static_cast<float>(x), static_cast<float>(y)}, true, host);
            if (m_shapeGrabbed == INSIDE) {
                if (m_rect[0].x <= point.x && m_rect[1].x >= point.x && m_rect[0].y <= point.y &&
                    m_rect[1].y >= point.y) {
                    m_diffX = x;
                    m_diffY = y;
                }
            }
        }
        if (!m_rectVisible || m_shapeGrabbed == OUTSIDE) {
            m_rect[0] = m_rect[1] = point;
            m_shapeGrabbed = OUTSIDE;
            m_rectVisible = true;
        }
    } else if (holding && m_shapeGrabbed != -1) {
        bool axisX = true;
        if (m_shapeGrabbed < INSIDE) {
            if (m_shapeGrabbed & LEFT || m_shapeGrabbed & RIGHT) {
                x = std::max(f.videoRect.left, std::min(x, f.videoRect.right));
                const int posInTable = (m_shapeGrabbed & RIGHT) ? 1 : 0;
                m_rect[posInTable].x = ((((x + m_diffX) / f.zoomScale.x) + f.zoomMove.x) * f.coeffW);
                if (m_shapeGrabbed & LEFT && m_rect[0].x > m_rect[1].x)
                    m_rect[0].x = m_rect[1].x;
                if (m_shapeGrabbed & RIGHT && m_rect[1].x < m_rect[0].x)
                    m_rect[1].x = m_rect[0].x;
            }
            if (m_shapeGrabbed & TOP || m_shapeGrabbed & BOTTOM) {
                axisX = false;
                y = std::max(f.videoRect.top, std::min(y, f.videoRect.bottom));
                const int posInTable = (m_shapeGrabbed & BOTTOM) ? 1 : 0;
                m_rect[posInTable].y = ((((y + m_diffY) / f.zoomScale.y) + f.zoomMove.y) * f.coeffH);
                if (m_shapeGrabbed & TOP && m_rect[0].y > m_rect[1].y)
                    m_rect[0].y = m_rect[1].y;
                if (m_shapeGrabbed & BOTTOM && m_rect[1].y < m_rect[0].y)
                    m_rect[1].y = m_rect[0].y;
            }
        } else if (m_shapeGrabbed == INSIDE) {
            const float movex = (((x - m_diffX) / f.zoomScale.x) * f.coeffW),
                        movey = (((y - m_diffY) / f.zoomScale.y) * f.coeffH);
            m_rect[0].x += movex;
            m_rect[0].y += movey;
            m_rect[1].x += movex;
            m_rect[1].y += movey;
            m_diffX = x;
            m_diffY = y;
        } else if (m_shapeGrabbed == OUTSIDE) {
            m_rect[1] = pointToSubtitles(static_cast<float>(x), static_cast<float>(y), host);
        }
        const bool bothScaleX = m_current.mode == ShapePreset::BothScaleX;
        if (((bothScaleX && !event.shift) || (!bothScaleX && event.shift)) && m_shapeGrabbed != INSIDE)
            setSquareShape(axisX);
        setDrawingScale();
        if (m_rectVisible)
            apply(host, false);
    }
}

Overlay DrawingTool::overlay(const VisualHost &host) const
{
    Overlay out;
    if (m_shape < 0) {
        // DrawingAndClip::DrawVisual: the \move position first.
        syncMove(host);
        m_editor.draw(out, frame(host));
        return out;
    }
    // Shapes::DrawVisual (VisualDrawingShapes.cpp:476-497): the rectangle.
    if (m_rectVisible) {
        const PointF point1 = pointToVideo(m_rect[0], host);
        const PointF point2 = pointToVideo(m_rect[1], host);
        const PointF v4[5] = {point1, {point2.x, point1.y}, point2, {point1.x, point2.y}, point1};
        for (int i = 0; i < 4; ++i)
            out.lines.push_back({v4[i], v4[i + 1], 1.f, kShapeRectangle});
    }
    return out;
}

std::vector<ToolOption> DrawingTool::options(const VisualHost &host) const
{
    // VectorItem for the drawing (VideoToolbar.cpp:56-66, 414-466, 503-559):
    // the six point modes, none of them usable or shown pushed while a shape
    // is chosen, and the shape list.
    static const struct {
        const char *icon;
        const char16_t *label;
    } modes[] = {{"vector-drag", u"Move points"},        {"vector-line", u"Add line"},
                 {"vector-bezier", u"Add Bézier curve"}, {"vector-bspline", u"Add B-spline"},
                 {"vector-point", u"Add separate point"}, {"vector-delete", u"Delete point"}};
    std::vector<ToolOption> out;
    for (int i = 0; i < 6; ++i) {
        ToolOption mode;
        mode.name = "mode" + std::to_string(i);
        mode.iconRole = modes[i].icon;
        mode.tooltip = modes[i].label;
        mode.checked = m_shapeSelection == 0 && m_editor.mode == i;
        mode.enabled = m_shapeSelection == 0;
        out.push_back(std::move(mode));
    }
    ToolOption shapes;
    shapes.name = "shape";
    shapes.kind = ToolOption::Kind::Choice;
    shapes.tooltip = u"List of ASS drawings with edit option.\nAfter choose drawing from list just set cursor i n "
                     u"place\nof start of drawing and click left mouse button and drag.";
    static const std::vector<ShapePreset> none;
    const std::vector<ShapePreset> *presets = host.shapePresets();
    shapes.choices = shapeListChoices(presets ? *presets : none);
    shapes.index = m_shapeSelection;
    out.push_back(std::move(shapes));
    return out;
}

bool DrawingTool::setOption(const std::string &name, int value, VisualHost &host)
{
    if (name == "shape") {
        // A preset (or "Choose"); the list's "Edit" is the host's (it opens
        // the editor).
        changeShape(value, true, host);
        host.toolChanged();
        return true;
    }
    if (m_shapeSelection != 0)
        return false; // VectorItem::OnMouseEvent: the modes take no click with a shape chosen
    if (name.size() == 5 && name.starts_with("mode") && name[4] >= '0' && name[4] <= '5') {
        m_editor.mode = name[4] - '0';
        host.toolChanged();
        return true;
    }
    return false;
}

} // namespace hikari::application::visual
