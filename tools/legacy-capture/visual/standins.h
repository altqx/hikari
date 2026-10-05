// Stand-ins for the legacy GUI classes the visual-tool coordinate code
// touches (T1). The probe compiles a copy of the legacy VisualCross.cpp next
// to headers that all include this one, and definitions copied unchanged out
// of RendererVideo.cpp, Visuals.cpp, Visuals.h, VideoBox.cpp and config.cpp
// (extract_functions.py). Each stand-in keeps the legacy member names and
// types those definitions read; what the GUI would do (cursor, redraw,
// history) is recorded instead.
#pragma once

#include <wx/font.h>
#include <wx/gdicmn.h>
#include <wx/event.h>
#include <wx/regex.h>
#include <wx/string.h>
#include <wx/thread.h>
#include <wx/window.h>
#include <wx/dc.h>

#include "d3dx9.h" // the legacy Linux D3DX stand-ins (D3DXVECTOR2, RECT, ID3DXLine...)

#include <cstdio>
#include <algorithm>
#include <string>
#include <vector>

// config.h:552-560, unchanged (ZoomMouseHandle clamps with MID).
#define MIN(a,b) ((a)<(b))?(a):(b)
#define MAX(a,b) ((a)>(b))?(a):(b)
#define MID(a,b,c) MAX((a),MIN((b),(c)))

// config.h:526-548, unchanged.
#define SAFE_DELETE(x) if (x !=nullptr) { delete x; x = nullptr; }
#define SAFE_RELEASE(x) if (x != nullptr) { x->Release(); x = nullptr; }
#define HR(what,err) if(FAILED(what)) {HikariLogSilent(err); return false;}
#define HRN(what,err) if(FAILED(what)) {HikariLogSilent(err); return;}

inline void HikariLogSilent(const wxString &text)
{
    std::fprintf(stderr, "legacy log: %s\n", static_cast<const char *>(text.utf8_str()));
}

const wxString emptyString;

// VideoBox.h:279-290 draws the label with the D3DX font; the probe has none
// (Cross::Draw only calls it when font is set), so it is never expanded.
#define DRAWOUTTEXT(font, text, rect, align, color) (void)0

// config.h:601 (the definition is copied from config.cpp).
wxString getfloat(float num, const wxString& format = L"5.3f", bool Truncate = true);

// The options these definitions read: VIDEO_PROGRESS_BAR (UpdateRects),
// VIDEO_ZOOM_PERCENT (SetZoom) and the program font (Cross; measured by the
// stand-in VideoBox::GetTextExtent below, so never used).
enum { VIDEO_PROGRESS_BAR, VIDEO_ZOOM_PERCENT };
struct ProbeOptions {
    int zoomPercent = 0; // VIDEO_ZOOM_PERCENT has no default: SetZoom then zooms 2x
    bool GetBool(int) const { return false; }
    int GetInt(int) const { return zoomPercent; }
    wxFont *GetFont(int) const { return nullptr; }
};
extern ProbeOptions Options;

// Visuals.h:56-68.
enum{
	CROSS = 0,
	CHANGEPOS,
	MOVE,
	SCALE,
	ROTATEZ,
	ROTATEXY,
	CLIPRECT,
	VECTORCLIP,
	VECTORDRAW,
	MOVEALL,
	ALL_TAGS
};

// SubsGrid columns (TXT, TXTTL) and the history entry Cross records.
enum { TXT = 512, TXTTL = 1024 };
enum { VISUAL_POSITION = 38 };
enum PlaybackState { Playing, Paused, Stopped, None };

class Dialogue {
public:
    wxString Text, TextTl;
};

class EditBox {
public:
    Dialogue *line = nullptr;
};

class SubsGrid {
public:
    int subsWidth = 0, subsHeight = 0;
    bool hasTLMode = false;
    int currentLine = 0;
    std::vector<std::string> changes; // "<column> <row> <text>"
    std::vector<int> modified;
    void GetASSRes(int *x, int *y) { *x = subsWidth; *y = subsHeight; }
    void ChangeCell(int column, int row, Dialogue *line)
    {
        changes.push_back(std::to_string(column) + " " + std::to_string(row) + " " +
                          std::string((column == TXTTL ? line->TextTl : line->Text).utf8_str()));
    }
    void Refresh(bool = true) {}
    void SetModified(int action) { modified.push_back(action); }
};

class HikariCheckBox {
public:
    bool value = true;
    bool GetValue() const { return value; }
};

class Fullscreen : public wxWindow {
public:
    HikariCheckBox *showToolbar = nullptr;
    int panelsize = 0;
};

class RendererVideo;
class TabPanel;

class VideoBox : public wxWindow {
public:
    // Members the definitions read.
    bool m_IsFullscreen = false;
    Fullscreen *m_FullScreenWindow = nullptr;
    bool m_PanelOnFullscreen = false;
    bool m_FullScreenProgressBar = false;
    int m_PanelHeight = 0;
    float m_AspectRatio = 0.f;
    bool m_IsMenuShown = false;
    RendererVideo *renderer = nullptr;
    TabPanel *tab = nullptr;
    // The probe's window: the client size, the text measure and what was
    // set (cursor, clipboard).
    int clientWidth = 0, clientHeight = 0;
    int charWidth = 7, textHeight = 14;
    int lastCursor = -1;

    void GetClientSize(int *w, int *h) const { *w = clientWidth; *h = clientHeight; }
    wxRect GetClientRect() const { return wxRect(0, 0, clientWidth, clientHeight); }
    void GetTextExtent(const wxString &text, int *w, int *h, int *, int *, const wxFont *) const
    {
        *w = charWidth * static_cast<int>(text.length());
        *h = textHeight;
    }
    bool IsFullScreen() { return m_IsFullscreen; }   // VideoBox.cpp:1770
    Fullscreen *GetFullScreenWindow() { return m_FullScreenWindow; } // VideoBox.cpp:1933
    bool IsMenuShown() { return m_IsMenuShown; }    // VideoBox.cpp:1998
    bool HasVideo() { return renderer != nullptr; } // VideoBox.h:129
    int GetPanelHeight() { return m_PanelHeight; }  // VideoBox.cpp:1956
    bool SetCursor(int cursorId) { lastCursor = cursorId; return true; }
    bool HasArrow() { return true; }
    bool HasCapture() { return captured; }
    void CaptureMouse() { captured = true; }
    void ReleaseMouse() { captured = false; }
    bool captured = false;
    void Render(bool = true) {}
    bool RedrawPaused() { return false; } // paused redraws are the GUI's
    void SetScaleAndZoom() {}
    // Copied from VideoBox.cpp.
    RECT GetVideoRect();
    void GetWindowSize(int* x, int* y, bool withTabPanel = true);
    void OnCopyCoords(const wxPoint &pos);
};

class TabPanel {
public:
    VideoBox *video = nullptr;
    SubsGrid *grid = nullptr;
    EditBox *edit = nullptr;
};

// VIDEO_COPY_COORDS writes the clipboard; the probe keeps the text.
class wxTextDataObject {
public:
    explicit wxTextDataObject(const wxString &text) : m_text(text) {}
    wxString GetText() const { return m_text; }
private:
    wxString m_text;
};
struct ProbeClipboard {
    wxString text;
    bool Open() { return true; }
    void SetData(wxTextDataObject *data) { text = data->GetText(); delete data; }
    void Close() {}
};
extern ProbeClipboard probeClipboard;
#define wxTheClipboard (&probeClipboard)

// The Visuals members Cross and the copied definitions use (Visuals.h:73-195),
// with the inline coordinate conversions copied from Visuals.h.
class Visuals {
public:
    virtual ~Visuals() = default;
    virtual void SizeChanged(wxRect wsize, LPD3DXLINE _line, LPD3DXFONT _font, LPDIRECT3DDEVICE9 _device);
    virtual void SetZoom(D3DXVECTOR2 move, D3DXVECTOR2 scale){
        zoomMove = move;
        zoomScale = scale;
    };
    virtual void Draw(int time) {}
    virtual void SetCurVisual() {}
    virtual void OnMouseEvent(wxMouseEvent &evt) {}
    virtual void DrawWx(wxDC& dc, int time) {}
#include "visuals_inline.inc"
    float coeffW = 1, coeffH = 1;
    LPD3DXLINE line = nullptr;
    LPD3DXFONT font = nullptr;
    LPDIRECT3DDEVICE9 device = nullptr;
    unsigned char Visual = CROSS;
    wxSize SubsSize;
    wxRect VideoSize;
    TabPanel *tab = nullptr;
    wxString *dummytext = nullptr;
    D3DXVECTOR2 zoomMove = D3DXVECTOR2(0, 0);
    D3DXVECTOR2 zoomScale = D3DXVECTOR2(1.0f, 1.0f);
};

// Copied from Visuals.h (class Cross). The probe reads the crosshair's
// private drawing state, so its members are opened up here.
#define private public
#include "cross_class.inc"
#undef private

// RendererVideo.h:66-77 (class FloatRect), copied.
#include "float_rect.inc"

// The RendererVideo members UpdateRects, SetZoom, ResetZoom, SetVisualZoom,
// Zoom and ZoomMouseHandle use (RendererVideo.h).
class RendererVideo {
public:
    VideoBox *videoControl = nullptr;
    HWND m_HWND = nullptr;
    RECT m_WindowRect{0, 0, 0, 0};
    RECT m_BackBufferRect{0, 0, 0, 0};
    RECT m_MainStreamRect{0, 0, 0, 0};
    FloatRect m_ZoomRect;
    char m_Grabbed = -1;
    wxPoint m_ZoomDiff;
    int m_Width = 0, m_Height = 0;
    Visuals *m_Visual = nullptr;
    bool m_HasZoom = false;
    float m_ZoomPercent = 1.f;
    bool m_HasVisualEdition = false;
    PlaybackState m_State = Paused;
    void ZoomChanged() {}
    void Render(bool = true) {}
    // Copied from RendererVideo.cpp.
    bool UpdateRects(bool changeZoom = true);
    void SetZoom(float percent = -1, const wxPoint &mousePos = wxDefaultPosition);
    void ResetZoom();
    void SetVisualZoom();
    void Zoom(const wxSize &size);
    void ZoomMouseHandle(wxMouseEvent &evt);
};
