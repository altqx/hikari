// T1: legacy video and visual-tool coordinates. Runs the legacy geometry on
// the cases in inputs/visual-cases.txt and prints one JSON object per case.
//
// Compiled with this file: VisualCross.cpp unchanged (the crosshair, Cross),
// and, copied unchanged by extract_functions.py, RendererVideo::UpdateRects,
// SetZoom, ResetZoom and SetVisualZoom, Visuals::SizeChanged and the inline
// GetCalculatedIn/OutPos conversions (Visuals.h), VideoBox::GetVideoRect,
// GetWindowSize and OnCopyCoords, and getfloat (config.cpp). The stand-ins
// they run against are in visual/standins.h.
//
// Case file lines:
//   case <name>
//   client <width> <height> <panel height>    the VideoBox client size and m_PanelHeight
//   frame <width> <height> <sar num> <sar den> the decoded frame (FFMS2 EncodedWidth/Height, SAR)
//   aspect <ratio>                              AspectRatioDialog: m_AspectRatio (height / width)
//   script <width> <height>                     the subtitles' PlayRes (GetASSRes)
//   dpr <ratio>                                 echoed for the rewrite (legacy works in device pixels)
//   zoom <percent> <x> <y>                      SetZoom(percent, wxPoint(x, y)) (the wheel, VideoBox.cpp:518)
//   zoomtoggle                                  SetZoom() (GLOBAL_VIDEO_ZOOM, Return; VIDEO_ZOOM_PERCENT unset: 2x)
//   setvisual                                   SetVisualZoom: what legacy runs on the next resize, tool
//                                               change or edit (RendererVideo::SetVisual); SetZoom does not
//   resetzoom                                   ResetZoom()
//   drag <x0> <y0> <x1> <y1>                    in zoom mode (after zoomtoggle): ZoomMouseHandle's
//                                               left drag from (x0, y0) to (x1, y1): the pan
//   points <x>,<y> ...                          view positions: crosshair, Out conversion, copy
//   scriptpoints <x>,<y> ...                    script positions: In conversion
//   click <x> <y> middle|ctrl <text>            Cross: \pos into the active Line (text, then TLMode
//   clicktl <x> <y> middle|ctrl <text>|<tl>     with a translation)
//   end
#include "Visuals.h"
#include "VideoBox.h"
#include "RendererVideo.h"

#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>

ProbeOptions Options;
ProbeClipboard probeClipboard;

#include "extracted.inc"

namespace {

std::string json(const wxString &s)
{
    const std::string utf8(s.utf8_str());
    std::string out = "\"";
    for (const unsigned char c : utf8) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        default: out += char(c);
        }
    }
    return out + "\"";
}

std::string num(double v)
{
    char buffer[40];
    std::snprintf(buffer, sizeof buffer, "%.9g", v);
    return buffer;
}

std::string rect(const RECT &r)
{
    return "[" + std::to_string(r.left) + "," + std::to_string(r.top) + "," + std::to_string(r.right) + "," +
           std::to_string(r.bottom) + "]";
}

std::string pair(double x, double y)
{
    return "[" + num(x) + "," + num(y) + "]";
}

std::vector<std::pair<double, double>> points(std::istream &in)
{
    std::vector<std::pair<double, double>> out;
    std::string token;
    while (in >> token) {
        const auto comma = token.find(',');
        out.emplace_back(std::stod(token.substr(0, comma)), std::stod(token.substr(comma + 1)));
    }
    return out;
}

wxMouseEvent mouse(wxEventType type, int x, int y, bool control = false)
{
    wxMouseEvent e(type);
    e.SetX(x);
    e.SetY(y);
    e.SetControlDown(control);
    return e;
}

struct Case {
    std::string name;
    int clientW = 0, clientH = 0, panel = 0;
    int frameW = 0, frameH = 0, sarNum = 0, sarDen = 1;
    bool hasAspect = false;
    float aspect = 0;
    int scriptW = 0, scriptH = 0;
    std::string dpr = "1";
    std::vector<std::string> zoomOps;
    std::vector<std::pair<double, double>> viewPoints, scriptPoints;
    struct Click {
        int x, y;
        bool middle;
        std::string text, tl;
        bool tlMode;
    };
    std::vector<Click> clicks;
};

void run(const Case &c)
{
    VideoBox video;
    SubsGrid grid;
    EditBox edit;
    Dialogue line;
    TabPanel tab{&video, &grid, &edit};
    edit.line = &line;
    video.tab = &tab;
    grid.subsWidth = c.scriptW;
    grid.subsHeight = c.scriptH;
    video.clientWidth = c.clientW;
    video.clientHeight = c.clientH;
    video.m_PanelHeight = c.panel;

    RendererVideo renderer;
    renderer.videoControl = &video;
    video.renderer = &renderer;

    // ProviderFFMS2.cpp:365-380: the aspect from the frame and its SAR,
    // reduced by common factors up to 10.
    int m_width = c.frameW, m_height = c.frameH;
    int m_arwidth = (c.sarNum == 0) ? m_width : (float)m_width * ((float)c.sarNum / (float)c.sarDen);
    int m_arheight = m_height;
    while (1) {
        bool divided = false;
        for (int i = 10; i > 1; i--) {
            if ((m_arwidth % i) == 0 && (m_arheight % i) == 0) {
                m_arwidth /= i; m_arheight /= i;
                divided = true;
                break;
            }
        }
        if (!divided) { break; }
    }
    // RendererFFMS2.cpp:460-491 (OpenFile): the frame size, odd widths made
    // even, the aspect, the whole frame shown, then UpdateRects.
    renderer.m_Width = m_width;
    renderer.m_Height = m_height;
    if (renderer.m_Width % 2 != 0){ renderer.m_Width++; }
    if (m_arheight == 0 || m_arwidth == 0){ video.m_AspectRatio = 0.0f; }
    else{ video.m_AspectRatio = (float)m_arheight / (float)m_arwidth; }
    // VideoBox::SetAspectRatio (AspectRatioDialog): the override, then UpdateVideoWindow.
    if (c.hasAspect)
        video.m_AspectRatio = c.aspect;
    renderer.m_MainStreamRect = {0, 0, renderer.m_Width, renderer.m_Height};
    renderer.UpdateRects();

    // RendererVideo::SetVisual: the tool sized to the video rect (with the
    // script's size, as Visuals::SetVisual then computes it) and the zoom.
    Cross cross;
    cross.tab = &tab;
    cross.SubsSize = wxSize(c.scriptW, c.scriptH);
    renderer.m_Visual = &cross;
    const RECT bb = renderer.m_BackBufferRect;
    cross.Visuals::SizeChanged(wxRect(bb.left, bb.top, bb.right, bb.bottom), nullptr, nullptr, nullptr);
    renderer.SetVisualZoom();

    for (const std::string &op : c.zoomOps) {
        std::istringstream in(op);
        std::string kind;
        in >> kind;
        if (kind == "zoom") {
            float percent;
            int x, y;
            in >> percent >> x >> y;
            renderer.SetZoom(percent, wxPoint(x, y));
        } else if (kind == "zoomtoggle") {
            renderer.SetZoom();
        } else if (kind == "setvisual") {
            renderer.SetVisualZoom();
        } else if (kind == "resetzoom") {
            renderer.ResetZoom();
        } else if (kind == "drag") {
            // VideoBox::OnMouseEvent hands every event to ZoomMouseHandle
            // while m_HasZoom (VideoBox.cpp:488-491).
            int x0, y0, x1, y1;
            in >> x0 >> y0 >> x1 >> y1;
            wxMouseEvent down = mouse(wxEVT_LEFT_DOWN, x0, y0);
            down.SetLeftDown(true);
            renderer.ZoomMouseHandle(down);
            wxMouseEvent drag = mouse(wxEVT_MOTION, x1, y1);
            drag.SetLeftDown(true);
            renderer.ZoomMouseHandle(drag);
            wxMouseEvent up = mouse(wxEVT_LEFT_UP, x1, y1);
            renderer.ZoomMouseHandle(up);
        }
    }
    cross.SetCurVisual();

    std::cout << "{\"case\":" << json(c.name) << ",\"client\":[" << c.clientW << "," << c.clientH << "," << c.panel
              << "],\"frame\":[" << c.frameW << "," << c.frameH << "," << c.sarNum << "," << c.sarDen << "]"
              << ",\"aspectOverride\":" << (c.hasAspect ? num(c.aspect) : std::string("null"))
              << ",\"script\":[" << c.scriptW << "," << c.scriptH << "],\"dpr\":" << c.dpr << ",\"zoomOps\":[";
    for (std::size_t i = 0; i < c.zoomOps.size(); ++i)
        std::cout << (i ? "," : "") << json(c.zoomOps[i]);
    std::cout << "],\"width\":" << renderer.m_Width << ",\"height\":" << renderer.m_Height
              << ",\"aspect\":" << num(video.m_AspectRatio) << ",\"window\":" << rect(renderer.m_WindowRect)
              << ",\"videoRect\":" << rect(renderer.m_BackBufferRect)
              << ",\"sourceRect\":" << rect(renderer.m_MainStreamRect) << ",\"zoomPercent\":" << num(renderer.m_ZoomPercent)
              << ",\"hasZoom\":" << (renderer.m_HasZoom ? "true" : "false")
              << ",\"zoomRect\":[" << num(renderer.m_ZoomRect.x) << "," << num(renderer.m_ZoomRect.y) << ","
              << num(renderer.m_ZoomRect.width) << "," << num(renderer.m_ZoomRect.height) << "]"
              << ",\"zoomMove\":" << pair(cross.zoomMove.x, cross.zoomMove.y)
              << ",\"zoomScale\":" << pair(cross.zoomScale.x, cross.zoomScale.y)
              << ",\"coeff\":" << pair(cross.coeffW, cross.coeffH);

    std::cout << ",\"points\":[";
    for (std::size_t i = 0; i < c.viewPoints.size(); ++i) {
        const int x = static_cast<int>(c.viewPoints[i].first), y = static_cast<int>(c.viewPoints[i].second);
        wxMouseEvent move = mouse(wxEVT_MOTION, x, y);
        cross.OnMouseEvent(move);
        std::cout << (i ? "," : "") << "{\"at\":[" << x << "," << y << "],\"out\":"
                  << pair(cross.GetCalculatedOutPosX(x), cross.GetCalculatedOutPosY(y));
        // Cross::DrawLines's state: the label and its rectangle, the two
        // lines' ends, whether the pointer is on the video.
        std::cout << ",\"label\":" << json(cross.coords) << ",\"labelRect\":" << rect(cross.crossRect)
                  << ",\"lines\":[" << pair(cross.vectors[0].x, cross.vectors[0].y) << ","
                  << pair(cross.vectors[1].x, cross.vectors[1].y) << "," << pair(cross.vectors[2].x, cross.vectors[2].y)
                  << "," << pair(cross.vectors[3].x, cross.vectors[3].y) << "],\"onVideo\":"
                  << (cross.isOnVideo ? "true" : "false") << ",\"shown\":" << (cross.cross ? "true" : "false")
                  << ",\"crossCoeff\":" << pair(cross.coeffX, cross.coeffY);
        video.OnCopyCoords(wxPoint(x, y));
        std::cout << ",\"copy\":" << json(probeClipboard.text) << "}";
    }
    std::cout << "],\"scriptPoints\":[";
    for (std::size_t i = 0; i < c.scriptPoints.size(); ++i) {
        const float x = static_cast<float>(c.scriptPoints[i].first), y = static_cast<float>(c.scriptPoints[i].second);
        std::cout << (i ? "," : "") << "{\"at\":" << pair(x, y) << ",\"in\":"
                  << pair(cross.GetCalculatedInPosX(x), cross.GetCalculatedInPosY(y)) << "}";
    }
    std::cout << "],\"clicks\":[";
    for (std::size_t i = 0; i < c.clicks.size(); ++i) {
        const auto &k = c.clicks[i];
        line.Text = wxString::FromUTF8(k.text);
        line.TextTl = wxString::FromUTF8(k.tl);
        grid.hasTLMode = k.tlMode;
        grid.changes.clear();
        grid.modified.clear();
        wxMouseEvent move = mouse(wxEVT_MOTION, k.x, k.y);
        cross.OnMouseEvent(move);
        wxMouseEvent click = k.middle ? mouse(wxEVT_MIDDLE_DOWN, k.x, k.y) : mouse(wxEVT_LEFT_DOWN, k.x, k.y, true);
        cross.OnMouseEvent(click);
        std::cout << (i ? "," : "") << "{\"at\":[" << k.x << "," << k.y << "],\"button\":\""
                  << (k.middle ? "middle" : "ctrl") << "\",\"tlMode\":" << (k.tlMode ? "true" : "false")
                  << ",\"text\":" << json(wxString::FromUTF8(k.text)) << ",\"tl\":" << json(wxString::FromUTF8(k.tl))
                  << ",\"newText\":" << json(line.Text) << ",\"newTl\":" << json(line.TextTl)
                  << ",\"changes\":" << grid.changes.size() << ",\"modified\":[";
        for (std::size_t m = 0; m < grid.modified.size(); ++m)
            std::cout << (m ? "," : "") << grid.modified[m];
        std::cout << "]}";
    }
    std::cout << "]}\n";
}

} // namespace

int main()
{
    Case c;
    std::string lineText;
    while (std::getline(std::cin, lineText)) {
        std::istringstream in(lineText);
        std::string key;
        if (!(in >> key))
            continue;
        if (key == "case") {
            c = Case{};
            in >> c.name;
        } else if (key == "client") {
            in >> c.clientW >> c.clientH >> c.panel;
        } else if (key == "frame") {
            in >> c.frameW >> c.frameH >> c.sarNum >> c.sarDen;
        } else if (key == "aspect") {
            c.hasAspect = true;
            in >> c.aspect;
        } else if (key == "script") {
            in >> c.scriptW >> c.scriptH;
        } else if (key == "dpr") {
            in >> c.dpr;
        } else if (key == "zoom" || key == "zoomtoggle" || key == "resetzoom" || key == "drag" ||
                   key == "setvisual") {
            c.zoomOps.push_back(lineText.substr(lineText.find(key)));
        } else if (key == "points") {
            c.viewPoints = points(in);
        } else if (key == "scriptpoints") {
            c.scriptPoints = points(in);
        } else if (key == "click" || key == "clicktl") {
            Case::Click k{};
            std::string button;
            in >> k.x >> k.y >> button;
            k.middle = button == "middle";
            std::string rest;
            std::getline(in, rest);
            if (!rest.empty() && rest[0] == ' ')
                rest.erase(0, 1);
            k.tlMode = key == "clicktl";
            if (k.tlMode) {
                const auto bar = rest.find('|');
                k.text = rest.substr(0, bar);
                k.tl = bar == std::string::npos ? std::string() : rest.substr(bar + 1);
            } else {
                k.text = rest;
            }
            c.clicks.push_back(k);
        } else if (key == "end") {
            run(c);
        }
    }
    return 0;
}

