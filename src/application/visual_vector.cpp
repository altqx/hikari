#include "hikari/application/visual_vector.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <climits>
#include <cmath>
#include <cstdio>
#include <utility>

// Built without floating-point contraction so float results match MSVC x64 (SSE, no FMA).

namespace hikari::application::visual {

namespace legacy {

bool cDouble(std::u16string_view text, double &out)
{
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
        return false;
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

}

namespace {

constexpr std::uint32_t kRed = 0xFFBB0000;
constexpr std::uint32_t kBezierArms = 0xFF0000FF;
constexpr std::uint32_t kSplineHull = 0xFFAA33AA;
constexpr std::uint32_t kHoverFill = 0xAACC8748;
constexpr std::uint32_t kCross = 0xFFFF00FF;

bool isCommand(std::u16string_view token)
{
    return token == u"m" || token == u"l" || token == u"b" || token == u"s";
}

// Division multiplies by the inverse, as D3DXVECTOR2 does.
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

}

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
        if (pos.type == u'm' && psize > 1 && ((i >= psize - 1) || (i < psize - 1 && points[i + 1].type == u'm')))
            visual += u"l " + legacy::getfloat(x, format) + u" " + legacy::getfloat(y, format) + u" ";
    }
    if (spline)
        visual += u"c ";
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
    // At most 1024 points a segment: near a float's limit the step underflows to 0.
    if (!(h >= 1.0f / 1024))
        h = 1.0f / 1024;
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
        const float dash = static_cast<float>(dashLen) * out.unit;
        const float singleMovement = 1 / (len / (dash * 2));
        actualPoint[0] = vector[i];
        actualPoint[1] = actualPoint[0];
        for (float j = 0; j <= 1; j += singleMovement) {
            actualPoint[1] = sub(actualPoint[1], mul(diffUnits, dash));
            if (j + singleMovement >= 1)
                actualPoint[1] = vector[iPlus1];
            line(out, actualPoint[0], actualPoint[1], argb, width);
            actualPoint[1] = sub(actualPoint[1], mul(diffUnits, dash));
            actualPoint[0] = sub(actualPoint[0], mul(mul(diffUnits, dash), 2.f));
        }
    }
}

void pointSquare(Overlay &out, PointF pos, bool sel, float rcsize)
{
    OverlayPolygon p;
    p.points = {{pos.x - rcsize, pos.y - rcsize},
                {pos.x + rcsize, pos.y - rcsize},
                {pos.x + rcsize, pos.y + rcsize},
                {pos.x - rcsize, pos.y + rcsize}};
    p.fill = sel ? out.handles.selectedFill : out.handles.fill;
    p.border = out.handles.border;
    p.above = true;
    out.polygons.push_back(std::move(p));
}

void pointCircle(Overlay &out, PointF pos, bool sel, float crsize)
{
    const float rad = 0.01745329251994329576923690768489f;
    OverlayPolygon p;
    for (int j = 0; j < 18; j++)
        p.points.push_back({pos.x + (crsize * std::sin((j * 20) * rad)), pos.y + (crsize * std::cos((j * 20) * rad))});
    p.fill = sel ? out.handles.selectedFill : out.handles.fill;
    p.border = out.handles.border;
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
    // MUST KILL: the wheel cycles legacy's buttons only, so it leaves Freehand as if from beside Spline
    if (mode == Freehand)
        mode = steps > 0 ? Spline + 1 : Spline;
    mode -= steps;
    if (mode < 0)
        mode = modeCount - 1;
    else if (mode >= modeCount)
        mode = 0;
}

int VectorEditor::checkPos(PointF pos, const VectorFrame &frame, bool retlast) const
{
    pos.x = (pos.x * frame.coeffW) - frame.offsetX;
    pos.y = (pos.y * frame.coeffH) - frame.offsetY;
    const float coeffPointArea = frame.pointArea() * frame.coeffW;
    for (std::size_t i = 0; i < points.size(); i++) {
        if (std::fabs(pos.x - points[i].x) < coeffPointArea && std::fabs(pos.y - points[i].y) < coeffPointArea)
            return static_cast<int>(i);
    }
    return retlast ? static_cast<int>(points.size()) : -1;
}

void VectorEditor::addCurve(PointF pos, int whereis, char16_t type, const VectorFrame &frame)
{
    pos.x = (pos.x * frame.coeffW) - frame.offsetX;
    pos.y = (pos.y * frame.coeffH) - frame.offsetY;
    int prevPoint = whereis - 1;
    if (whereis == 0)
        prevPoint = 0;
    if (whereis != static_cast<int>(points.size()))
        whereis++;
    const int oldx = legacy::toInt(points[prevPoint].x);
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
    // False when not beside a B-spline.
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
    // Where a right click may insert after a point; -1 inside a Bézier or a B-spline.
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
    // Indices past the end (a Bézier missing its points) are skipped.
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
    (void)frame;
    if (event.release)
        return false;
    const int keyCode = event.key;
    if (event.control && keyCode == keys::A) {
        changeSelection(true);
        return true;
    }
    if (keyCode == keys::Delete) {
        forgetStroke();
        removePoints(-1, cb, true);
        return true;
    }
    // Shift nudges by a tenth in a drawing only: a clip is written in whole units.
    if (const auto direction = nudgeDirection(event)) {
        forgetStroke();
        const float increase = (event.shift && drawing) ? 0.1f : 1.f;
        moveSelected(direction->x * increase, direction->y * increase, cb);
        return true;
    }
    return false;
}

void VectorEditor::pointer(const Pointer &event, const VectorFrame &frame, const Callbacks &cb)
{
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

    if (m_stroking) {
        if (leftisdown) {
            const float pixel = frame.coeffW / frame.zoomScale.x;
            const PointF p{(zx * frame.coeffW) - frame.offsetX, (zy * frame.coeffH) - frame.offsetY};
            if (std::hypot(p.x - m_stroke.back().x, p.y - m_stroke.back().y) >= pixel)
                m_stroke.push_back(p);
            return;
        }
        m_stroking = false;
        m_lastStroke = Stroke{points, std::exchange(m_stroke, {}), frame.coeffW / frame.zoomScale.x, 0};
        placeStroke();
        if (m_lastStroke->curves == 0) {
            m_lastStroke.reset();
            return;
        }
        cb.apply(false);
        cb.apply(true);
        return;
    }
    if (press)
        forgetStroke();
    if (mode == Freehand && click) {
        m_grabbed = -1;
        m_drawCross = false;
        m_stroking = true;
        m_stroke = {{(zx * frame.coeffW) - frame.offsetX, (zy * frame.coeffH) - frame.offsetY}};
        return;
    }

    if (event.kind == Pointer::Kind::Release) {
        if (event.button == Pointer::Button::None)
            return;
        if (!m_drawSelection)
            cb.apply(true);
        else
            m_drawSelection = false;
        return;
    }
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
            const float pointx = (points[i].x + frame.offsetX) / frame.coeffW;
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
                cb.notice(u"A double “m” was blocked because of a VSFilter bug");
                return;
            }
            addMove(xy, pos, frame);
            cb.apply(false);
            mode = Line;
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
        zx = (0 > ((zx < w) ? zx : w)) ? 0 : ((zx < w) ? zx : w);
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

void VectorEditor::placeStroke()
{
    Stroke &stroke = *m_lastStroke;
    points = stroke.before;
    const auto curves = fitStroke(stroke.samples, freehandTolerance(smoothing) * stroke.viewPixel);
    stroke.curves = static_cast<int>(curves.size());
    if (curves.empty())
        return;
    points.push_back({curves.front()[0].x, curves.front()[0].y, u'm', true, false});
    for (const auto &c : curves) {
        points.push_back({c[1].x, c[1].y, u'b', true, false});
        points.push_back({c[2].x, c[2].y, u'b', false, false});
        points.push_back({c[3].x, c[3].y, u'b', false, false});
    }
}

bool VectorEditor::refitStroke()
{
    if (!m_lastStroke)
        return false;
    placeStroke();
    return true;
}

ToolOption freehandOption(const VectorEditor &editor, bool enabled)
{
    ToolOption out;
    out.name = "freehand";
    out.iconRole = "vector-freehand";
    out.tooltip = u"Freehand";
    out.checked = enabled && editor.mode == VectorEditor::Freehand;
    out.enabled = enabled;
    out.group = u"Points";
    return out;
}

std::vector<ToolValue> freehandValues(const VectorEditor &editor)
{
    if (editor.mode != VectorEditor::Freehand)
        return {};
    ToolValue smoothing;
    smoothing.name = "smoothing";
    smoothing.label = u"Smoothing";
    smoothing.text = legacy::getfloat(static_cast<float>(editor.smoothing), ".0f");
    smoothing.editable = true;
    smoothing.group = u"Freehand";
    std::vector<ToolValue> out{smoothing};
    if (const auto curves = editor.strokeCurves()) {
        ToolValue points;
        points.name = "strokePoints";
        points.label = u"Points";
        points.text = legacy::getfloat(static_cast<float>(*curves + 1), ".0f");
        points.group = u"Freehand";
        ToolValue count = points;
        count.name = "strokeCurves";
        count.label = u"Curves";
        count.text = legacy::getfloat(static_cast<float>(*curves), ".0f");
        out.push_back(points);
        out.push_back(count);
    }
    return out;
}

std::optional<int> smoothingOf(const std::string &name, const std::u16string &text)
{
    double value = 0;
    if (name != "smoothing" || !parseValue(text, value))
        return std::nullopt;
    return static_cast<int>(std::lround(std::clamp(value, 0.0, 100.0)));
}

std::optional<int> VectorEditor::strokeCurves() const
{
    return m_lastStroke ? std::optional(m_lastStroke->curves) : std::nullopt;
}

void VectorEditor::drawRect(Overlay &out, const VectorFrame &frame, int i) const
{
    pointSquare(out, frame.toView(points[i]), points[i].selected, 3.0f * frame.unit);
    out.named.push_back({{frame.toView(points[i])}, 5 * frame.unit, 0});
}

void VectorEditor::drawCircle(Overlay &out, const VectorFrame &frame, int i) const
{
    pointCircle(out, frame.toView(points[i]), points[i].selected, 3.0f * frame.unit);
    out.named.push_back({{frame.toView(points[i])}, 5 * frame.unit, 1});
}

void VectorEditor::drawLine(Overlay &out, const VectorFrame &frame, int i) const
{
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
            return size - i; // a Bézier missing its points
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
    for (std::size_t i = 1; m_stroking && i < m_stroke.size(); ++i)
        line(out, frame.toView({m_stroke[i - 1].x, m_stroke[i - 1].y}), frame.toView({m_stroke[i].x, m_stroke[i].y}),
             kRed, 1);
    if (points.empty())
        return;
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
        const float rcsize = 3 * frame.unit;
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

}

namespace hikari::application::visual {

namespace {

struct Vec {
    double x = 0, y = 0;
};
Vec operator+(Vec a, Vec b) { return {a.x + b.x, a.y + b.y}; }
Vec operator-(Vec a, Vec b) { return {a.x - b.x, a.y - b.y}; }
Vec operator*(Vec a, double s) { return {a.x * s, a.y * s}; }
double dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y; }
double length(Vec a) { return std::sqrt(dot(a, a)); }
Vec unit(Vec a)
{
    const double l = length(a);
    return l > 0 ? a * (1 / l) : Vec{};
}

using Cubic = std::array<Vec, 4>;

Vec bezier(const Cubic &c, double t)
{
    const double s = 1 - t;
    return c[0] * (s * s * s) + c[1] * (3 * s * s * t) + c[2] * (3 * s * t * t) + c[3] * (t * t * t);
}

double newtonRoot(const Cubic &c, Vec p, double t)
{
    const Cubic d1{(c[1] - c[0]) * 3, (c[2] - c[1]) * 3, (c[3] - c[2]) * 3, {}};
    const Cubic d2{(d1[1] - d1[0]) * 2, (d1[2] - d1[1]) * 2, {}, {}};
    const double s = 1 - t;
    const Vec q = bezier(c, t) - p;
    const Vec q1 = d1[0] * (s * s) + d1[1] * (2 * s * t) + d1[2] * (t * t);
    const Vec q2 = d2[0] * s + d2[1] * t;
    const double denominator = dot(q1, q1) + dot(q, q2);
    return denominator == 0 ? t : t - dot(q, q1) / denominator;
}

class Fitter {
public:
    Fitter(const std::vector<Vec> &samples, double tolerance)
        : m_p(samples), m_squaredTolerance(tolerance * tolerance)
    {
    }

    std::vector<Cubic> run()
    {
        const std::size_t last = m_p.size() - 1;
        fit(0, last, unit(m_p[1] - m_p[0]), unit(m_p[last - 1] - m_p[last]));
        return std::move(m_out);
    }

private:
    Cubic straight(std::size_t first, std::size_t last, Vec t1, Vec t2) const
    {
        const double third = length(m_p[last] - m_p[first]) / 3;
        return {m_p[first], m_p[first] + t1 * third, m_p[last] + t2 * third, m_p[last]};
    }

    Cubic generate(std::size_t first, std::size_t last, const std::vector<double> &u, Vec t1, Vec t2) const
    {
        double c00 = 0, c01 = 0, c11 = 0, x0 = 0, x1 = 0;
        const Vec a = m_p[first], b = m_p[last];
        for (std::size_t i = first; i <= last; ++i) {
            const double t = u[i - first], s = 1 - t;
            const Vec a1 = t1 * (3 * s * s * t);
            const Vec a2 = t2 * (3 * s * t * t);
            c00 += dot(a1, a1);
            c01 += dot(a1, a2);
            c11 += dot(a2, a2);
            const Vec rest = m_p[i] - (a * (s * s * s + 3 * s * s * t) + b * (3 * s * t * t + t * t * t));
            x0 += dot(a1, rest);
            x1 += dot(a2, rest);
        }
        const double det = c00 * c11 - c01 * c01;
        const double alpha1 = det == 0 ? 0 : (x0 * c11 - x1 * c01) / det;
        const double alpha2 = det == 0 ? 0 : (c00 * x1 - c01 * x0) / det;
        const double epsilon = 1e-6 * length(b - a);
        if (alpha1 < epsilon || alpha2 < epsilon)
            return straight(first, last, t1, t2);
        return {a, a + t1 * alpha1, b + t2 * alpha2, b};
    }

    std::vector<double> chordLengths(std::size_t first, std::size_t last) const
    {
        std::vector<double> u{0};
        for (std::size_t i = first + 1; i <= last; ++i)
            u.push_back(u.back() + length(m_p[i] - m_p[i - 1]));
        for (double &v : u)
            v = u.back() > 0 ? v / u.back() : 0;
        return u;
    }

    double maxError(std::size_t first, std::size_t last, const Cubic &c, const std::vector<double> &u,
                    std::size_t &split) const
    {
        double worst = 0;
        split = (first + last) / 2;
        for (std::size_t i = first + 1; i < last; ++i) {
            const Vec d = bezier(c, u[i - first]) - m_p[i];
            if (dot(d, d) >= worst) {
                worst = dot(d, d);
                split = i;
            }
        }
        return worst;
    }

    void fit(std::size_t first, std::size_t last, Vec t1, Vec t2)
    {
        if (last - first == 1) {
            m_out.push_back(straight(first, last, t1, t2));
            return;
        }
        std::vector<double> u = chordLengths(first, last);
        Cubic c = generate(first, last, u, t1, t2);
        std::size_t split = 0;
        double error = maxError(first, last, c, u, split);
        if (error <= m_squaredTolerance) {
            m_out.push_back(c);
            return;
        }
        if (error <= m_squaredTolerance * 16) {
            for (int iteration = 0; iteration < 4; ++iteration) {
                for (std::size_t i = first; i <= last; ++i)
                    u[i - first] = newtonRoot(c, m_p[i], u[i - first]);
                c = generate(first, last, u, t1, t2);
                error = maxError(first, last, c, u, split);
                if (error <= m_squaredTolerance) {
                    m_out.push_back(c);
                    return;
                }
            }
        }
        Vec centre = unit(m_p[split - 1] - m_p[split + 1]);
        if (length(centre) == 0)
            centre = unit(Vec{-(m_p[split + 1] - m_p[split]).y, (m_p[split + 1] - m_p[split]).x});
        fit(first, split, t1, centre);
        fit(split, last, centre * -1, t2);
    }

    const std::vector<Vec> &m_p;
    double m_squaredTolerance;
    std::vector<Cubic> m_out;
};

double distanceToLine(Vec p, Vec a, Vec b)
{
    const Vec ab = b - a;
    const double l = dot(ab, ab);
    const double t = l > 0 ? std::clamp(dot(p - a, ab) / l, 0.0, 1.0) : 0;
    return length(p - (a + ab * t));
}

Vec vec(const VectorPoint &p) { return {p.x, p.y}; }

Cubic cubicAt(const std::vector<VectorPoint> &points, int i)
{
    return {vec(points[i - 1]), vec(points[i]), vec(points[i + 1]), vec(points[i + 2])};
}

constexpr int kCurveSamples = 64;

double nearestT(const Cubic &c, Vec p)
{
    double best = 0, bestDistance = INFINITY;
    for (int k = 0; k <= kCurveSamples; ++k) {
        const double t = static_cast<double>(k) / kCurveSamples;
        const Vec d = bezier(c, t) - p;
        if (dot(d, d) < bestDistance) {
            bestDistance = dot(d, d);
            best = t;
        }
    }
    return best;
}

}

std::vector<Segment> VectorEditor::segments() const
{
    std::vector<Segment> out;
    const int size = static_cast<int>(points.size());
    int run = 0;
    for (int i = 1; i < size; ++i) {
        if (points[i].type != u'b' || points[i - 1].type != u'b')
            run = i;
        if (points[i - 1].type == u's')
            continue;
        if (points[i].type == u'l')
            out.push_back({i, false});
        else if (points[i].type == u'b' && (i - run) % 3 == 0 && i + 2 < size && points[i + 1].type == u'b' &&
                 points[i + 2].type == u'b')
            out.push_back({i, true});
    }
    return out;
}

PointF VectorEditor::toPoints(PointF device, const VectorFrame &frame) const
{
    const float zx = (device.x / frame.zoomScale.x) + frame.zoomMove.x;
    const float zy = (device.y / frame.zoomScale.y) + frame.zoomMove.y;
    return {(zx * frame.coeffW) - frame.offsetX, (zy * frame.coeffH) - frame.offsetY};
}

std::optional<Segment> VectorEditor::segmentAt(PointF pos, const VectorFrame &frame) const
{
    const PointF zoomedOut{(pos.x / frame.zoomScale.x) + frame.zoomMove.x, (pos.y / frame.zoomScale.y) + frame.zoomMove.y};
    if (checkPos(zoomedOut, frame) != -1)
        return std::nullopt;
    const PointF at = toPoints(pos, frame);
    const Vec p{at.x, at.y};
    const double reach = static_cast<double>(frame.pointArea()) * frame.coeffW;
    std::optional<Segment> best;
    double bestDistance = reach;
    for (const Segment &s : segments()) {
        double distance = 0;
        if (s.curve) {
            const Cubic c = cubicAt(points, s.index);
            distance = length(bezier(c, nearestT(c, p)) - p);
        } else {
            distance = distanceToLine(p, vec(points[s.index - 1]), vec(points[s.index]));
        }
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = s;
        }
    }
    return best;
}

bool VectorEditor::editSegment(Segment segment, SegmentEdit edit, PointF pos, const VectorFrame &frame)
{
    if (const auto all = segments(); std::ranges::find(all, segment) == all.end())
        return false;
    forgetStroke();
    const int i = segment.index;
    const auto at = [&](Vec v, char16_t type, bool start) {
        return VectorPoint{static_cast<float>(v.x), static_cast<float>(v.y), type, start, false};
    };
    const auto it = [&](int k) { return points.begin() + k; };
    switch (edit) {
    case SegmentEdit::MakeCurve: {
        if (segment.curve)
            return false;
        const Vec a = vec(points[i - 1]), b = vec(points[i]);
        points[i] = at(b, u'b', false);
        points.insert(it(i), {at(a + (b - a) * (1.0 / 3), u'b', true), at(a + (b - a) * (2.0 / 3), u'b', false)});
        if (i + 3 < static_cast<int>(points.size()) && points[i + 3].type == u'l')
            points[i + 3].start = true;
        return true;
    }
    case SegmentEdit::MakeStraight:
        if (!segment.curve)
            return false;
        points.erase(it(i), it(i + 2));
        points[i].type = u'l';
        points[i].start = true;
        return true;
    case SegmentEdit::InsertPoint: {
        const PointF click = toPoints(pos, frame);
        if (!segment.curve) {
            points.insert(it(i), at({click.x, click.y}, u'l', true));
            return true;
        }
        const Cubic c = cubicAt(points, i);
        const double t = std::clamp(nearestT(c, {click.x, click.y}), 1.0 / kCurveSamples, 1 - 1.0 / kCurveSamples);
        const auto mix = [t](Vec u, Vec v) { return u + (v - u) * t; };
        const Vec q0 = mix(c[0], c[1]), q1 = mix(c[1], c[2]), q2 = mix(c[2], c[3]);
        const Vec r0 = mix(q0, q1), r1 = mix(q1, q2);
        const Vec split = mix(r0, r1);
        points[i] = at(q0, u'b', true);
        points[i + 1] = at(r0, u'b', false);
        points[i + 2] = at(split, u'b', false);
        points.insert(it(i + 3), {at(r1, u'b', true), at(q2, u'b', false), at(c[3], u'b', false)});
        return true;
    }
    case SegmentEdit::Delete: {
        int m = i;
        if (segment.curve)
            points.erase(it(i), it(i + 2));
        points[m].type = u'm';
        points[m].start = true;
        if (points[m - 1].type == u'm') {
            points.erase(it(m - 1));
            --m;
        }
        if (m + 1 == static_cast<int>(points.size()) || points[m + 1].type == u'm')
            points.erase(it(m));
        return true;
    }
    }
    return false;
}

double freehandTolerance(int smoothing)
{
    const double s = std::clamp(smoothing, 0, 100);
    return 0.5 + s * s / 400;
}

std::vector<std::array<PointF, 4>> fitStroke(const std::vector<PointF> &stroke, double tolerance)
{
    std::vector<Vec> samples;
    for (const PointF &p : stroke)
        if (samples.empty() || samples.back().x != p.x || samples.back().y != p.y)
            samples.push_back({p.x, p.y});
    if (samples.size() < 2)
        return {};
    std::vector<std::array<PointF, 4>> out;
    const auto toPoint = [](Vec v) { return PointF{static_cast<float>(v.x), static_cast<float>(v.y)}; };
    for (const Cubic &c : Fitter(samples, std::max(tolerance, 1e-3)).run())
        out.push_back({toPoint(c[0]), toPoint(c[1]), toPoint(c[2]), toPoint(c[3])});
    return out;
}

}
