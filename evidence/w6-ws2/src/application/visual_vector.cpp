#include "hikari/application/visual_vector.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <climits>
#include <cmath>
#include <cstdio>

// Built without floating-point contraction, as visual_view.cpp: every float
// step below is legacy's (MSVC x64, SSE, no FMA).

namespace hikari::application::visual {

namespace legacy {

bool cDouble(std::u16string_view text, double &out)
{
    // wxString::ToCDouble (wxStrtod_l in the C locale): the whole text, as
    // strtod reads it (leading blanks, a sign, decimal or hex, inf and nan),
    // false on an out-of-range value (ERANGE). Locale-independent here.
    std::string s;
    for (const char16_t c : text) {
        if (c > 0x7F)
            return false;
        s += static_cast<char>(c);
    }
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || (s[i] >= '\t' && s[i] <= '\r')))
        ++i;
    bool negative = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
        negative = s[i] == '-';
        ++i;
    }
    if (i >= s.size())
        return false;
    std::chars_format format = std::chars_format::general;
    if (s.size() - i > 2 && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X') &&
        (std::isxdigit(static_cast<unsigned char>(s[i + 2])) || s[i + 2] == '.')) {
        format = std::chars_format::hex;
        i += 2;
    }
    if (s[i] == '+' || s[i] == '-')
        return false; // one sign only
    double value = 0;
    const char *first = s.data() + i;
    const char *last = s.data() + s.size();
    const auto [end, ec] = std::from_chars(first, last, value, format);
    if (ec != std::errc() || end != last)
        return false;
    out = negative ? -value : value;
    return true;
}

int atoi(std::u16string_view text)
{
    std::size_t i = 0;
    while (i < text.size() && (text[i] == u' ' || (text[i] >= u'\t' && text[i] <= u'\r')))
        ++i;
    bool negative = false;
    if (i < text.size() && (text[i] == u'+' || text[i] == u'-')) {
        negative = text[i] == u'-';
        ++i;
    }
    long long value = 0;
    for (; i < text.size() && text[i] >= u'0' && text[i] <= u'9'; ++i) {
        value = value * 10 + (text[i] - u'0');
        if (value > static_cast<long long>(INT_MAX) + 1)
            value = static_cast<long long>(INT_MAX) + 1;
    }
    if (negative)
        return value > INT_MAX ? INT_MIN : -static_cast<int>(value);
    return value > INT_MAX ? INT_MAX : static_cast<int>(value);
}

std::u16string getfloat(float value, std::string_view format)
{
    char buffer[512];
    const std::string spec = "%" + std::string(format);
    std::snprintf(buffer, sizeof buffer, spec.c_str(), static_cast<double>(value));
    std::string s(buffer);
    if (!(format.size() >= 3 && format.substr(format.size() - 3) == ".0f")) {
        std::size_t remove = 0;
        for (std::size_t i = s.size() - 1; i > 0; --i) {
            if (s[i] == '0') {
                ++remove;
            } else if (s[i] == '.') {
                ++remove;
                break;
            } else {
                break;
            }
        }
        s.resize(s.size() - remove);
    }
    const auto first = s.find_first_not_of(" \t\r\n");
    s = first == std::string::npos ? std::string() : s.substr(first);
    return std::u16string(s.begin(), s.end());
}

int toInt(float value)
{
    if (!(value > -2147483904.f && value < 2147483648.f))
        return INT_MIN;
    return static_cast<int>(value);
}

} // namespace legacy

namespace {

constexpr std::uint32_t kRed = 0xFFBB0000;         // the outlines and paths
constexpr std::uint32_t kHandleFill = 0xAA121150;  // a point
constexpr std::uint32_t kSelectedFill = 0xAAFCE6B1; // a selected point
constexpr std::uint32_t kBezierArms = 0xFF0000FF;
constexpr std::uint32_t kSplineHull = 0xFFAA33AA;
constexpr std::uint32_t kHoverFill = 0xAACC8748;
constexpr std::uint32_t kCross = 0xFFFF00FF;

bool isCommand(std::u16string_view token)
{
    return token == u"m" || token == u"l" || token == u"b" || token == u"s";
}

// D3DXVECTOR2's operators (d3dx9math.inl): division multiplies by the inverse.
PointF sub(PointF a, PointF b) { return {a.x - b.x, a.y - b.y}; }
PointF mul(PointF a, float f) { return {a.x * f, a.y * f}; }
PointF divide(PointF a, float f)
{
    const float inv = 1.0f / f;
    return {a.x * inv, a.y * inv};
}

void line(Overlay &out, PointF a, PointF b, std::uint32_t argb, float width)
{
    out.lines.push_back({a, b, width, argb});
}

} // namespace

std::vector<VectorPoint> parseVectorPoints(std::u16string_view vector)
{
    std::vector<VectorPoint> points;
    double tmpx = 0;
    bool gotx = false;
    bool start = false;
    int pointsAfterStart = 1;
    char16_t type = u'm';
    std::size_t at = 0;
    while (at < vector.size()) {
        // wxStringTokenizer(vector, " "): blank delimiters, so no empty tokens.
        if (vector[at] == u' ') {
            ++at;
            continue;
        }
        const std::size_t end = std::min(vector.find(u' ', at), vector.size());
        std::u16string_view token = vector.substr(at, end - at);
        at = end;
        if (token == u"p")
            token = u"s";
        if (isCommand(token)) {
            type = token[0];
            start = true;
            pointsAfterStart = 1;
        } else if (token == u"c") {
            start = true;
            continue;
        } else if (gotx) {
            double tmpy = 0;
            if (!legacy::cDouble(token, tmpy)) {
                gotx = false;
                continue;
            }
            points.push_back({static_cast<float>(tmpx), static_cast<float>(tmpy), type, start, false});
            gotx = false;
            if ((type == u'l' || (type == u'm' && pointsAfterStart == 1)) || (type == u'b' && pointsAfterStart == 3)) {
                if (type == u'm')
                    type = u'l';
                start = true;
                pointsAfterStart = 0;
            } else {
                start = false;
            }
            pointsAfterStart++;
        } else {
            if (legacy::cDouble(token, tmpx))
                gotx = true;
        }
    }
    return points;
}

std::u16string serializeVectorPoints(const std::vector<VectorPoint> &points, std::string_view format, PointF offset)
{
    std::u16string visual;
    char16_t lasttype = 0;
    int countB = 0;
    bool spline = false;
    const std::size_t psize = points.size();
    for (std::size_t i = 0; i < psize; i++) {
        const VectorPoint &pos = points[i];
        const float x = pos.x + offset.x;
        const float y = pos.y + offset.y;
        if (countB && !pos.start) {
            visual += legacy::getfloat(x, format) + u" " + legacy::getfloat(y, format) + u" ";
            countB++;
        } else {
            if (spline) {
                visual += u"c ";
                spline = false;
            }
            if (lasttype != pos.type || pos.type == u'm') {
                visual += pos.type;
                visual += u" ";
                lasttype = pos.type;
            }
            visual += legacy::getfloat(x, format) + u" " + legacy::getfloat(y, format) + u" ";
            if (pos.type == u'b' || pos.type == u's') {
                countB = 1;
                if (pos.type == u's')
                    spline = true;
            }
        }
        // "fix for m one after another" (VisualClips.cpp:352-356).
        if (pos.type == u'm' && psize > 1 && ((i >= psize - 1) || (i < psize - 1 && points[i + 1].type == u'm')))
            visual += u"l " + legacy::getfloat(x, format) + u" " + legacy::getfloat(y, format) + u" ";
    }
    if (spline)
        visual += u"c ";
    // wxString::Trim(): trailing blanks.
    while (!visual.empty() && (visual.back() == u' ' || visual.back() == u'\t' || visual.back() == u'\r' ||
                               visual.back() == u'\n'))
        visual.pop_back();
    return visual;
}

std::vector<PointF> flattenCurve(const PointF (&control)[4], bool bspline)
{
    std::vector<PointF> table;
    float a[4], b[4];
    float x[4], y[4];
    for (int g = 0; g < 4; g++) {
        x[g] = control[g].x;
        y[g] = control[g].y;
    }
    if (bspline) {
        a[3] = (-x[0] + 3 * x[1] - 3 * x[2] + x[3]) / 6.0;
        a[2] = (3 * x[0] - 6 * x[1] + 3 * x[2]) / 6.0;
        a[1] = (-3 * x[0] + 3 * x[2]) / 6.0;
        a[0] = (x[0] + 4 * x[1] + x[2]) / 6.0;
        b[3] = (-y[0] + 3 * y[1] - 3 * y[2] + y[3]) / 6.0;
        b[2] = (3 * y[0] - 6 * y[1] + 3 * y[2]) / 6.0;
        b[1] = (-3 * y[0] + 3 * y[2]) / 6.0;
        b[0] = (y[0] + 4 * y[1] + y[2]) / 6.0;
    } else {
        a[3] = -x[0] + 3 * x[1] - 3 * x[2] + x[3];
        a[2] = 3 * x[0] - 6 * x[1] + 3 * x[2];
        a[1] = -3 * x[0] + 3 * x[1];
        a[0] = x[0];
        b[3] = -y[0] + 3 * y[1] - 3 * y[2] + y[3];
        b[2] = 3 * y[0] - 6 * y[1] + 3 * y[2];
        b[1] = -3 * y[0] + 3 * y[1];
        b[0] = y[0];
    }
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
    return table;
}

void dashedLines(Overlay &out, const std::vector<PointF> &vector, int dashLen, std::uint32_t argb, float width)
{
    if (vector.size() < 2)
        return;
    PointF actualPoint[2];
    for (std::size_t i = 0; i < vector.size() - 1; i++) {
        const std::size_t iPlus1 = i + 1;
        const PointF pdiff = sub(vector[i], vector[iPlus1]);
        const float len = std::sqrt((pdiff.x * pdiff.x) + (pdiff.y * pdiff.y));
        if (len == 0)
            return;
        const PointF diffUnits = divide(pdiff, len);
        const float singleMovement = 1 / (len / (dashLen * 2));
        actualPoint[0] = vector[i];
        actualPoint[1] = actualPoint[0];
        for (float j = 0; j <= 1; j += singleMovement) {
            actualPoint[1] = sub(actualPoint[1], mul(diffUnits, static_cast<float>(dashLen)));
            if (j + singleMovement >= 1)
                actualPoint[1] = vector[iPlus1];
            line(out, actualPoint[0], actualPoint[1], argb, width);
            actualPoint[1] = sub(actualPoint[1], mul(diffUnits, static_cast<float>(dashLen)));
            actualPoint[0] = sub(actualPoint[0], mul(mul(diffUnits, static_cast<float>(dashLen)), 2.f));
        }
    }
}

void pointSquare(Overlay &out, PointF pos, bool sel, float rcsize)
{
    // A filled square and its one-pixel outline (a LINESTRIP), over the path.
    OverlayPolygon p;
    p.points = {{pos.x - rcsize, pos.y - rcsize},
                {pos.x + rcsize, pos.y - rcsize},
                {pos.x + rcsize, pos.y + rcsize},
                {pos.x - rcsize, pos.y + rcsize}};
    p.fill = sel ? kSelectedFill : kHandleFill;
    p.border = kRed;
    p.above = true;
    out.polygons.push_back(std::move(p));
}

void pointCircle(Overlay &out, PointF pos, bool sel, float crsize)
{
    // An 18-sided fan from the centre and its outline (Visuals.cpp:380-401).
    const float rad = 0.01745329251994329576923690768489f;
    OverlayPolygon p;
    for (int j = 0; j < 18; j++)
        p.points.push_back({pos.x + (crsize * std::sin((j * 20) * rad)), pos.y + (crsize * std::cos((j * 20) * rad))});
    p.fill = sel ? kSelectedFill : kHandleFill;
    p.border = kRed;
    p.above = true;
    out.polygons.push_back(std::move(p));
}

PointF VectorFrame::toView(const VectorPoint &p) const
{
    return {(((p.x + offsetX) / coeffW) - zoomMove.x) * zoomScale.x,
            (((p.y + offsetY) / coeffH) - zoomMove.y) * zoomScale.y};
}

void VectorEditor::normaliseFirst()
{
    if (!points.empty() && points[0].type != u'm')
        points[0].type = u'm';
}

void VectorEditor::wheel(int steps)
{
    // DrawingAndClip::OnMouseEvent (VisualClips.cpp:872-878) and
    // VectorItem::SetItemToggled (VideoToolbar.h:117-123).
    mode -= steps;
    if (mode < 0)
        mode = modeCount - 1;
    else if (mode >= modeCount)
        mode = 0;
}

int VectorEditor::checkPos(PointF pos, const VectorFrame &frame, bool retlast) const
{
    // DrawingAndClip::CheckPos (VisualClips.cpp:659-673), pos in the
    // zoomed-out view.
    pos.x = (pos.x * frame.coeffW) - frame.offsetX;
    pos.y = (pos.y * frame.coeffH) - frame.offsetY;
    const float coeffPointArea = frame.pointArea() * frame.coeffW;
    for (std::size_t i = 0; i < points.size(); i++) {
        // ClipPoint::IsInPos.
        if (std::fabs(pos.x - points[i].x) < coeffPointArea && std::fabs(pos.y - points[i].y) < coeffPointArea)
            return static_cast<int>(i);
    }
    return retlast ? static_cast<int>(points.size()) : -1;
}

void VectorEditor::addCurve(PointF pos, int whereis, char16_t type, const VectorFrame &frame)
{
    // DrawingAndClip::AddCurve (VisualClips.cpp:682-701).
    pos.x = (pos.x * frame.coeffW) - frame.offsetX;
    pos.y = (pos.y * frame.coeffH) - frame.offsetY;
    int prevPoint = whereis - 1;
    if (whereis == 0)
        prevPoint = 0;
    if (whereis != static_cast<int>(points.size()))
        whereis++;
    const int oldx = legacy::toInt(points[prevPoint].x); // wxPoint
    const int oldy = legacy::toInt(points[prevPoint].y);
    const int diffx = legacy::toInt((pos.x - oldx) / 3.0f);
    const int diffy = legacy::toInt((pos.y - oldy) / 3.0f);
    points.insert(points.begin() + whereis, {pos.x - (diffx * 2), pos.y - (diffy * 2), type, true, false});
    points.insert(points.begin() + whereis + 1, {pos.x - diffx, pos.y - diffy, type, false, false});
    points.insert(points.begin() + whereis + 2, {pos.x, pos.y, type, false, false});
    m_acpoint = points[whereis + 2];
}

bool VectorEditor::addCurvePoint(PointF pos, int whereis, const VectorFrame &frame)
{
    // DrawingAndClip::AddCurvePoint (VisualClips.cpp:703-717): false for
    // wxBell (not beside a B-spline).
    bool isstart = false;
    const bool isInRange = static_cast<int>(points.size()) > whereis;
    if (points[(whereis == 0) ? 0 : whereis - 1].type == u's' || (isInRange && points[whereis].type == u's')) {
        if (isInRange && points[whereis].start) {
            points[whereis].start = false;
            isstart = true;
        }
        points.insert(points.begin() + whereis,
                      {(pos.x * frame.coeffW) - frame.offsetX, (pos.y * frame.coeffH) - frame.offsetY, u's', isstart,
                       false});
        return true;
    }
    return false;
}

void VectorEditor::addLine(PointF pos, int whereis, const VectorFrame &frame)
{
    points.insert(points.begin() + whereis,
                  {(pos.x * frame.coeffW) - frame.offsetX, (pos.y * frame.coeffH) - frame.offsetY, u'l', true, false});
    m_acpoint = points[whereis];
}

void VectorEditor::addMove(PointF pos, int whereis, const VectorFrame &frame)
{
    points.insert(points.begin() + whereis,
                  {(pos.x * frame.coeffW) - frame.offsetX, (pos.y * frame.coeffH) - frame.offsetY, u'm', true, false});
    m_acpoint = points[whereis];
}

void VectorEditor::selectPoints(const VectorFrame &frame)
{
    // DrawingAndClip::SelectPoints (VisualClips.cpp:1086-1103), in the view.
    const int x = (m_selection.left < m_selection.right) ? m_selection.left : m_selection.right;
    const int y = (m_selection.top < m_selection.bottom) ? m_selection.top : m_selection.bottom;
    const int r = (m_selection.left > m_selection.right) ? m_selection.left : m_selection.right;
    const int b = (m_selection.top > m_selection.bottom) ? m_selection.top : m_selection.bottom;
    for (auto &p : points) {
        const PointF point = frame.toView(p);
        p.selected = point.x >= x && point.x <= r && point.y >= y && point.y <= b;
    }
}

void VectorEditor::changeSelection(bool select)
{
    for (auto &p : points)
        p.selected = select;
}

int VectorEditor::findPoint(int pos, char16_t type, bool nextStart, bool fromEnd) const
{
    // DrawingAndClip::FindPoint (VisualClips.cpp:1112-1130).
    int j = pos;
    const int size = static_cast<int>(points.size());
    while (fromEnd ? j >= 0 : j < size) {
        if (nextStart ? points[j].start : points[j].type == type)
            break;
        if (fromEnd)
            j--;
        else
            j++;
    }
    return j;
}

VectorPoint VectorEditor::findSnapPoint(const VectorPoint &pos, std::size_t pointToSkip) const
{
    // DrawingAndClip::FindSnapPoint (VisualClips.cpp:1132-1159): within ten
    // units of another point's x or y, the first such point's.
    bool xfound = false, yfound = false;
    float modPosx = pos.x;
    float modPosy = pos.y;
    const float maxdiff = 10.f;
    for (std::size_t i = 0; i < points.size(); i++) {
        if (i == pointToSkip)
            continue;
        if (!xfound && std::fabs(points[i].x - modPosx) <= maxdiff) {
            xfound = true;
            modPosx = points[i].x;
        }
        if (!yfound && std::fabs(points[i].y - modPosy) <= maxdiff) {
            yfound = true;
            modPosy = points[i].y;
        }
    }
    VectorPoint out = pos;
    out.x = modPosx;
    out.y = modPosy;
    return out;
}

int VectorEditor::checkCurve(int pos, bool checkSpline) const
{
    // DrawingAndClip::CheckCurve (VisualClips.cpp:1213-1252): where a right
    // click may insert after a point (-1: inside a Bézier or a B-spline).
    const std::size_t pointsSize = points.size();
    if (pos < 0 || static_cast<std::size_t>(pos) > pointsSize)
        return static_cast<int>(pointsSize);
    if (pos < 2 || static_cast<std::size_t>(pos) >= pointsSize)
        return pos;
    if (points[pos].type == u'b') {
        int start = 1;
        int end = static_cast<int>(pointsSize) - 1;
        int result = -1;
        for (int i = pos - 1; i >= 1; i--) {
            if (points[i].type != u'b') {
                start = i + 1;
                break;
            }
        }
        for (int i = pos + 1; i < static_cast<int>(pointsSize); i++) {
            if (points[i].type != u'b') {
                end = i;
                break;
            }
        }
        if (end - start > 4 || start == pos) {
            for (int i = start; i <= end; i += 3) {
                if (pos == i)
                    result = i;
                else if (i > pos)
                    break;
            }
        }
        return result;
    }
    if (checkSpline && points[pos].type == u's')
        return -1;
    return pos;
}

void VectorEditor::removePoints(int selectedPoint, const Callbacks &cb, bool fromKeyboard)
{
    // DrawingAndClip::RemovePoints (VisualClips.cpp:1367-1462). Legacy read
    // and erased past the end on some broken paths (a Bézier missing its
    // points); those indices are skipped here (approved T4-bezier-past-end).
    std::vector<std::size_t> sels;
    for (std::size_t i = 0; i < points.size(); i++)
        if (points[i].selected)
            sels.push_back(i);
    std::size_t ssize = sels.size();
    if (selectedPoint != -1) {
        bool needToAdd = true;
        for (std::size_t i = 0; i < ssize; i++)
            if (sels[i] == static_cast<std::size_t>(selectedPoint))
                needToAdd = false;
        if (needToAdd) {
            sels.push_back(static_cast<std::size_t>(selectedPoint));
            ssize = sels.size();
            std::sort(sels.begin(), sels.end());
        }
    }
    if (ssize < 1)
        return;
    std::size_t lastRemovei = static_cast<std::size_t>(-1);
    for (std::size_t i = ssize; i > 0; i--) {
        const std::size_t j = sels[i - 1];
        if (j >= lastRemovei || j >= points.size())
            continue;
        const char16_t type = points[j].type;
        std::size_t numOfRemoved = 1;
        std::size_t removei = j;
        if (type == u'b') {
            for (; removei > 0; removei--)
                if (points[removei].start)
                    break;
            numOfRemoved = 2;
            if (removei + 2 == j) {
                numOfRemoved = 3;
            } else if (removei + 2 < points.size()) {
                points[removei + 2].type = u'l';
                points[removei + 2].start = true;
            }
        }
        if (type == u's') {
            std::size_t firstPoint = j;
            for (; firstPoint > 0; firstPoint--)
                if (points[firstPoint].start)
                    break;
            std::size_t lastPoint = j + 1;
            for (; lastPoint < points.size(); lastPoint++) {
                if (points[lastPoint].start) {
                    lastPoint--;
                    break;
                }
            }
            if ((lastPoint - firstPoint) < 4) {
                removei = firstPoint;
                numOfRemoved = lastPoint - firstPoint;
            }
        }
        const std::size_t removeEnd = std::min(points.size(), removei + numOfRemoved);
        points.erase(points.begin() + static_cast<std::ptrdiff_t>(removei),
                     points.begin() + static_cast<std::ptrdiff_t>(removeEnd));
        if (type == u'm') {
            if (j < points.size() && points[j].type != u'm') {
                const bool isBezier = points[j].type == u'b';
                points[j].type = u'm';
                points[j].start = true;
                if (isBezier && j == 0 && j + 2 < points.size()) {
                    points[2].type = u'l';
                    points.erase(points.begin() + 1, points.begin() + 2);
                }
            }
        }
        lastRemovei = removei;
    }
    cb.apply(false);
    if (fromKeyboard)
        cb.apply(true);
}

void VectorEditor::moveSelected(float x, float y, const Callbacks &cb)
{
    // DrawingAndClip::OnMoveSelected (VisualClips.cpp:1194-1211).
    bool modified = false;
    for (auto &p : points) {
        if (p.selected) {
            p.x += x;
            p.y += y;
            modified = true;
        }
    }
    if (modified) {
        cb.apply(false);
        cb.apply(true);
    }
}

bool VectorEditor::key(const Key &event, const VectorFrame &frame, const Callbacks &cb)
{
    // DrawingAndClip::OnKeyPress (VisualClips.cpp:1161-1192).
    (void)frame;
    if (event.release)
        return false;
    const int keyCode = event.key;
    if (event.control && keyCode == keys::A) {
        changeSelection(true);
        return true;
    }
    if (keyCode == keys::Delete) {
        removePoints(-1, cb, true);
        return true;
    }
    if (keyCode == keys::W || keyCode == keys::S || keyCode == keys::A || keyCode == keys::D) {
        float x = 0;
        float y = 0;
        const float increase = (event.shift && drawing) ? 0.1f : 1.f;
        switch (keyCode) {
        case keys::A:
            x = -increase;
            break;
        case keys::D:
            x = increase;
            break;
        case keys::S:
            y = increase;
            break;
        case keys::W:
            y = -increase;
            break;
        default:
            break;
        }
        moveSelected(x, y, cb);
        return true;
    }
    return false;
}

void VectorEditor::pointer(const Pointer &event, const VectorFrame &frame, const Callbacks &cb)
{
    // DrawingAndClip::OnMouseEvent (VisualClips.cpp:870-1083) after the
    // wheel and blockevents (the owner and the host).
    m_x = event.x;
    m_y = event.y;
    float zx = (m_x / frame.zoomScale.x) + frame.zoomMove.x;
    float zy = (m_y / frame.zoomScale.y) + frame.zoomMove.y;
    const PointF xy{zx, zy};
    const bool press = event.kind == Pointer::Kind::Press;
    const bool click = press && event.button == Pointer::Button::Left;
    const bool leftisdown = event.leftDown;
    const bool right = press && event.button == Pointer::Button::Right;
    const bool ctrl = event.control;
    const bool leaving = event.kind == Pointer::Kind::Leave;
    const std::size_t psize = points.size();

    if (!press && !leftisdown) {
        const int pos = checkPos(xy, frame);
        if (pos != -1) {
            m_acpoint = points[pos];
            m_lastpos = pos;
        } else if (m_lastpos >= 0 && m_lastpos < static_cast<int>(psize)) {
            m_lastpos = -1;
        }
        if (mode >= 1 && mode <= 3 && pos == -1 && !leaving) {
            if (psize < 1)
                return;
            m_drawToolLines = true;
        } else if (m_drawToolLines) {
            m_drawToolLines = false;
        }
        if (!m_drawSelection) {
            if (mode == 0 || pos != -1 || mode > 3)
                m_drawCross = true;
        }
        if (leaving && m_drawCross)
            m_drawCross = false;
    }

    if (event.kind == Pointer::Kind::Release) {
        // LeftUp / RightUp / MiddleUp; another button's up does nothing.
        if (event.button == Pointer::Button::None)
            return;
        if (!m_drawSelection)
            cb.apply(true);
        else
            m_drawSelection = false;
        return;
    }
    // Remove points: a middle click, or a click in the delete mode.
    if ((press && event.button == Pointer::Button::Middle) || (mode == Delete && click)) {
        m_grabbed = -1;
        const int pos = checkPos(xy, frame);
        removePoints(pos, cb, false);
        return;
    }

    if (click || right) {
        m_grabbed = -1;
        m_axis = 0;
        for (std::size_t i = 0; i < psize; i++) {
            const float pointx = (points[i].x + frame.offsetX) / frame.coeffW; // ClipPoint::wx
            const float pointy = (points[i].y + frame.offsetY) / frame.coeffH;
            if (std::fabs(pointx - zx) < frame.pointArea() && std::fabs(pointy - zy) < frame.pointArea()) {
                m_lastpoint = m_acpoint = points[i];
                if (!m_acpoint.selected && !ctrl)
                    changeSelection(false);
                points[i].selected = right ? false : ctrl ? !points[i].selected : true;
                m_grabbed = static_cast<int>(i);
                m_diffX = legacy::toInt(pointx - zx);
                m_diffY = legacy::toInt(pointy - zy);
                m_firstmove = {zx, zy};
                break;
            }
        }

        if (mode >= 1 && mode <= 3 && (m_grabbed == -1 || right)) {
            if (points.empty()) {
                addMove(xy, 0, frame);
                cb.apply(false);
                return;
            }
            int pos = checkPos(xy, frame, true);
            switch (mode) {
            case Line:
                if (right && m_grabbed != -1) {
                    pos++;
                    pos = checkCurve(pos, true);
                    if (pos < 0) {
                        cb.bell();
                        return;
                    }
                }
                addLine(xy, pos, frame);
                break;
            case Bezier:
                addCurve(xy, pos, u'b', frame);
                break;
            case Spline:
                if (right && m_grabbed != -1) {
                    pos++;
                    pos = checkCurve(pos, false);
                    if (pos < 0) {
                        cb.bell();
                        return;
                    }
                }
                if (points[(pos == static_cast<int>(psize)) ? psize - 1 : static_cast<std::size_t>(pos)].type == u's') {
                    if (!addCurvePoint(xy, pos, frame))
                        cb.bell();
                    break;
                }
                addCurve(xy, pos, u's', frame);
                break;
            default:
                break;
            }
            cb.apply(false);
            return;
        }

        if (mode == Move && m_grabbed == -1) {
            const int pos = checkPos(xy, frame, true);
            if (psize > 0 && points[(pos == static_cast<int>(psize)) ? psize - 1 : static_cast<std::size_t>(pos)].type == u'm') {
                // Legacy's HikariMessageBox, shown without blocking.
                cb.notice(u"Double \"m\" was blocked because of Vsfilter bug");
                return;
            }
            addMove(xy, pos, frame);
            cb.apply(false);
            mode = Line; // tool = 1, and the toolbar follows
        } else if (m_grabbed == -1) {
            m_drawSelection = true;
            m_drawCross = false;
            m_selection = {m_x, m_y, m_x, m_y};
            selectPoints(frame);
        }
        return;
    }

    if (leftisdown && m_grabbed != -1 && m_grabbed < static_cast<int>(psize) && event.kind == Pointer::Kind::Move) {
        const int w = frame.videoRect.right - frame.videoRect.left;
        const int h = frame.videoRect.bottom - frame.videoRect.top;
        zx = (0 > ((zx < w) ? zx : w)) ? 0 : ((zx < w) ? zx : w); // MID(0, zx, w)
        zy = (0 > ((zy < h) ? zy : h)) ? 0 : ((zy < h) ? zy : h);
        VectorPoint &g = points[m_grabbed];
        g.x = ((zx + m_diffX) * frame.coeffW) - frame.offsetX;
        g.y = ((zy + m_diffY) * frame.coeffH) - frame.offsetY;
        if (event.alt)
            g = findSnapPoint(g, static_cast<std::size_t>(m_grabbed));
        if (!g.selected)
            g.selected = true;
        if (event.shift) {
            const int diffx = legacy::toInt(std::fabs(m_firstmove.x - zx));
            const int diffy = legacy::toInt(std::fabs(m_firstmove.y - zy));
            if (diffx != diffy)
                m_axis = diffx > diffy ? 2 : 1;
            if (m_axis == 1)
                g.x = m_lastpoint.x;
            if (m_axis == 2)
                g.y = m_lastpoint.y;
        }
        if (g.selected) {
            const float movementx = m_acpoint.x - g.x;
            const float movementy = m_acpoint.y - g.y;
            for (std::size_t i = 0; i < psize; i++) {
                if (points[i].selected && static_cast<int>(i) != m_grabbed) {
                    points[i].x = points[i].x - movementx;
                    points[i].y = points[i].y - movementy;
                }
            }
        }
        m_acpoint = g;
        cb.apply(false);
        m_lastpos = -1;
    }
    if (m_drawSelection) {
        m_selection.right = m_x;
        m_selection.bottom = m_y;
        selectPoints(frame);
    }
}

void VectorEditor::drawRect(Overlay &out, const VectorFrame &frame, int i) const
{
    pointSquare(out, frame.toView(points[i]), points[i].selected, 3.0f);
}

void VectorEditor::drawCircle(Overlay &out, const VectorFrame &frame, int i) const
{
    pointCircle(out, frame.toView(points[i]), points[i].selected, 3.0f);
}

void VectorEditor::drawLine(Overlay &out, const VectorFrame &frame, int i) const
{
    // DrawingAndClip::DrawLine (VisualClips.cpp:731-748).
    int diff = 1;
    if (points[i - 1].type == u's') {
        int j = i - 2;
        while (j >= 0) {
            if (points[j].type != u's')
                break;
            j--;
        }
        diff = (i - j) - 2;
    }
    line(out, frame.toView(points[i - diff]), frame.toView(points[i]), kRed, 1);
    if (i > 1)
        drawRect(out, frame, i - 1);
}

int VectorEditor::drawCurve(Overlay &out, const VectorFrame &frame, int i, bool bspline) const
{
    // DrawingAndClip::DrawCurve (VisualClips.cpp:760-818) and its Curve on
    // the zoomed view (ClipPoint::wx / wy with the zoom).
    std::vector<PointF> v4;
    int pts = 3;
    const int size = static_cast<int>(points.size());
    if (bspline) {
        const int acpos = i - 1;
        int bssize = 1;
        int spos = i + 1;
        while (spos < size) {
            if (points[spos].start)
                break;
            bssize++;
            spos++;
        }
        pts = bssize;
        bssize++;
        for (int k = 0; k < bssize; k++) {
            PointF control[4];
            int acpt = k;
            for (int g = 0; g < 4; g++) {
                if (acpt > (bssize - 1))
                    acpt = 0;
                control[g] = frame.toView(points[acpos + acpt]);
                acpt++;
            }
            const auto part = flattenCurve(control, true);
            v4.insert(v4.end(), part.begin(), part.end());
        }
        std::vector<PointF> hull;
        for (int j = 0, g = i - 1; j < bssize; j++, g++)
            hull.push_back(frame.toView(points[g]));
        hull.push_back(frame.toView(points[i - 1]));
        for (std::size_t k = 0; k + 1 < hull.size(); ++k)
            line(out, hull[k], hull[k + 1], kSplineHull, 1);
        const int iplus1 = (i + bssize - 2 < size - 1) ? i + 1 : 0;
        if (i - 1 != 0 || iplus1 != 0) {
            line(out, frame.toView(points[i - 1]), v4[0], kRed, 1);
            line(out, v4[0], frame.toView(points[iplus1]), kRed, 1);
        }
    } else {
        if (i + 2 >= size)
            return size - i; // a Bézier missing its points (approved T4-bezier-past-end)
        VectorPoint first = points[i - 1];
        if (points[i - 1].type == u's') {
            int j = i - 2;
            while (j >= 0) {
                if (points[j].type != u's')
                    break;
                j--;
            }
            const int diff = (i - j) - 2;
            first = points[i - diff];
        }
        const PointF control[4] = {frame.toView(first), frame.toView(points[i]), frame.toView(points[i + 1]),
                                   frame.toView(points[i + 2])};
        v4 = flattenCurve(control, false);
        line(out, control[0], control[1], kBezierArms, 1);
        line(out, control[2], control[3], kBezierArms, 1);
    }
    for (std::size_t k = 0; k + 1 < v4.size(); ++k)
        line(out, v4[k], v4[k + 1], kRed, 1);
    if (i > 1)
        drawRect(out, frame, i - 1);
    for (int j = 1; j < pts; j++)
        drawCircle(out, frame, i + j - 1);
    return pts;
}

void VectorEditor::draw(Overlay &out, const VectorFrame &frame) const
{
    // DrawingAndClip::DrawVisual (VisualClips.cpp:94-179). Legacy drew the
    // selection rectangle with the two-pixel line Visuals::Draw sets, then
    // everything else one pixel wide.
    if (m_drawSelection) {
        const IntRect &s = m_selection;
        const std::vector<PointF> v5 = {
            {static_cast<float>(s.left), static_cast<float>(s.top)},
            {static_cast<float>(s.right), static_cast<float>(s.top)},
            {static_cast<float>(s.right), static_cast<float>(s.bottom)},
            {static_cast<float>(s.left), static_cast<float>(s.bottom)},
            {static_cast<float>(s.left), static_cast<float>(s.top)}};
        dashedLines(out, v5, 4, kRed, 2);
    }
    if (points.empty())
        return;
    // DrawVisual's own fix: the first point is drawn as "m".
    VectorEditor copy = *this;
    copy.normaliseFirst();
    copy.drawPoints(out, frame);
}

void VectorEditor::drawPoints(Overlay &out, const VectorFrame &frame) const
{
    const int size = static_cast<int>(points.size());
    if (m_drawToolLines) {
        const int mPoint = findPoint(size - 1, u'm', false, true);
        if (mPoint >= 0 && mPoint < size - 1) {
            std::vector<PointF> v3 = {frame.toView(points[mPoint]),
                                      {static_cast<float>(m_x), static_cast<float>(m_y)},
                                      frame.toView(points[size - 1])};
            if (points[size - 1].type == u'm')
                v3.pop_back();
            dashedLines(out, v3, 4, kRed, 1);
        }
    }
    int g = (size < 2 || (size > 1 && points[1].type == u'm')) ? 0 : 1;
    int lastM = 0;
    while (g < size) {
        if (points[g].type == u'l') {
            drawLine(out, frame, g);
            g++;
        } else if (points[g].type == u'b' || points[g].type == u's') {
            g += drawCurve(out, frame, g, points[g].type == u's');
        } else if (points[g].type != u'm') {
            g++;
        }
        if (g >= size || points[g].type == u'm') {
            if (g < size && points[g].type == u'm')
                drawRect(out, frame, g);
            if (g > 1) {
                line(out, frame.toView(points[g - 1]), frame.toView(points[lastM]), kRed, 1);
                drawRect(out, frame, lastM);
                drawRect(out, frame, g - 1);
            }
            lastM = g;
            g++;
        }
    }
    if (m_lastpos >= 0 && m_lastpos < size) {
        const PointF pos = frame.toView(points[m_lastpos]);
        const float rcsize = 3;
        OverlayPolygon hover;
        hover.points = {{pos.x - rcsize, pos.y - rcsize},
                        {pos.x + rcsize, pos.y - rcsize},
                        {pos.x + rcsize, pos.y + rcsize},
                        {pos.x - rcsize, pos.y + rcsize}};
        hover.fill = kHoverFill;
        hover.border = 0;
        hover.above = true;
        out.polygons.push_back(std::move(hover));
    }
    if (m_drawCross) {
        const float x = static_cast<float>(m_x);
        const float y = static_cast<float>(m_y);
        dashedLines(out, {{x, 0}, {x, static_cast<float>(frame.videoRect.bottom)}}, 4, kCross, 1);
        dashedLines(out, {{0, y}, {static_cast<float>(frame.videoRect.right), y}}, 4, kCross, 1);
    }
}

} // namespace hikari::application::visual
