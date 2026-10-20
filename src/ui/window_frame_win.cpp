// SH1 on Windows: MuseScore 4's main-window frame (docs/research/musescore-shell.md
// §1, "Windows title bar: frame behaviour"; winwindowscontroller.cpp), with
// the maximise button handed to Windows so that Windows 11 shows its snap
// layouts over it (§7, SH1).
//
// Qt makes the window frameless (WS_POPUP), and the frame puts the caption
// styles back, so Windows keeps the drop shadow, the minimise and maximise
// animations, Aero Snap, Win+arrows and the system menu; WM_NCCALCSIZE then
// makes the whole window client area. WM_NCHITTEST answers from the title
// bar (MainWindowFrame::hitAt): its free parts are the caption (move,
// double-click, snap, the system menu on a right click), the maximise
// button is the frame's own (HTMAXBUTTON, whose hover and press come here as
// WM_NC* messages and are handed to the QML button) and the application
// icon is the system menu's (HTSYSMENU). A band inside the window's edges
// resizes it while it is not maximised.

#include "window_frame.h"
#include "window_frame_native.h"

#include <QAbstractNativeEventFilter>
#include <QColor>
#include <QCoreApplication>
#include <QEvent>
#include <QPlatformSurfaceEvent>
#include <QPointer>
#include <QQuickWindow>

#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <windowsx.h>

namespace hikari::ui::windowframe {

namespace {

class WindowsFrame final : public QObject, public NativeFrame, public QAbstractNativeEventFilter {
public:
    WindowsFrame(MainWindowFrame *frame, QQuickWindow *window)
        : m_frame(frame)
        , m_window(window)
    {
        QCoreApplication::instance()->installNativeEventFilter(this);
        // Qt sets the window's styles again when it makes the native window
        // and when its flags change.
        m_window->installEventFilter(this);
    }
    ~WindowsFrame() override
    {
        QCoreApplication::instance()->removeNativeEventFilter(this);
        if (hwnd() && m_custom)
            restoreSystemFrame();
    }

    void apply(bool custom) override
    {
        const bool was = m_custom;
        m_custom = custom;
        if (!hwnd())
            return;
        if (custom)
            applyCustomFrame();
        else if (was)
            restoreSystemFrame();
    }

    bool showWindowMenu(QPointF position) override
    {
        if (!hwnd())
            return false;
        const qreal scale = m_window->devicePixelRatio();
        POINT at{LONG(position.x() * scale), LONG(position.y() * scale)};
        ClientToScreen(hwnd(), &at);
        return trackSystemMenu(at);
    }

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override
    {
        if (eventType != "windows_generic_MSG" || !m_custom || !hwnd())
            return false;
        const MSG *msg = static_cast<const MSG *>(message);
        if (!msg || msg->hwnd != hwnd())
            return false;
        switch (msg->message) {
        case WM_NCCALCSIZE:
            return calculateSize(msg, result);
        case WM_NCHITTEST:
            return answer(result, hitTest(POINT{GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam)}));
        case WM_NCMOUSEMOVE:
            trackMaximizeButton(msg->wParam == HTMAXBUTTON);
            return false;
        case WM_NCMOUSELEAVE:
            m_tracking = false;
            m_frame->setMaximizeState(false, false);
            return false;
        case WM_NCLBUTTONDOWN:
        case WM_NCLBUTTONDBLCLK:
            if (msg->wParam != HTMAXBUTTON)
                return false;
            // Windows would draw its own button there.
            m_frame->setMaximizeState(true, true);
            return answer(result, 0);
        case WM_NCLBUTTONUP:
            if (msg->wParam != HTMAXBUTTON)
                return false;
            if (m_frame->maximizePressed())
                m_frame->toggleMaximized();
            m_frame->setMaximizeState(true, false);
            return answer(result, 0);
        case WM_NCRBUTTONUP:
            if (msg->wParam != HTCAPTION && msg->wParam != HTSYSMENU)
                return false;
            trackSystemMenu(POINT{GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam)});
            return answer(result, 0);
        case WM_ERASEBKGND:
            // No white flash before the scene's first frame.
            eraseBackground();
            return answer(result, 1);
        default:
            return false;
        }
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_window && m_custom) {
            const bool made = event->type() == QEvent::PlatformSurface
                              && static_cast<QPlatformSurfaceEvent *>(event)->surfaceEventType()
                                     == QPlatformSurfaceEvent::SurfaceCreated;
            if (made || event->type() == QEvent::Show)
                QMetaObject::invokeMethod(this, [this] { apply(m_custom); }, Qt::QueuedConnection);
        }
        return QObject::eventFilter(watched, event);
    }

private:
    // Qt hands the filters the posted messages (the mouse's) from its event
    // loop with no result, and true there drops them; the sent ones come
    // from the window procedure with one. The window procedure passes none
    // of the input messages on (WM_SYSCOMMAND, the mouse's), so a sent
    // WM_SYSCOMMAND never comes here.
    static bool answer(qintptr *result, qintptr value)
    {
        if (result)
            *result = value;
        return true;
    }

    HWND hwnd() const
    {
        // handle(), not winId(): winId() would make the native window now.
        return m_window && m_window->handle() ? reinterpret_cast<HWND>(m_window->winId()) : nullptr;
    }

    static constexpr LONG_PTR kCaptionStyles =
        WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME;

    // DG1: a dialog has no minimise box, and no sizing frame or maximise
    // box while it cannot be resized (so Aero Snap and the system menu's
    // Size and Maximise leave it as it is).
    LONG_PTR captionStyles() const
    {
        if (!m_frame->dialog())
            return kCaptionStyles;
        return WS_CAPTION | WS_SYSMENU | (m_frame->resizable() ? WS_MAXIMIZEBOX | WS_THICKFRAME : 0);
    }

    void applyCustomFrame()
    {
        const LONG_PTR style = GetWindowLongPtrW(hwnd(), GWL_STYLE);
        const LONG_PTR wanted = captionStyles();
        const LONG_PTR styled = (style & ~(kCaptionStyles & ~wanted)) | wanted;
        if (styled != style)
            SetWindowLongPtrW(hwnd(), GWL_STYLE, styled);
        // A one-pixel frame extended into the client area: DWM draws the
        // window's shadow and its rounded corners (Windows 11). The bottom
        // edge's alone: a frame extended at the top takes the caption
        // buttons for DWM's, and the snap layouts never come over Maximise.
        const MARGINS shadow{0, 0, 0, 1};
        DwmExtendFrameIntoClientArea(hwnd(), &shadow);
        SetWindowPos(hwnd(), nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }

    void restoreSystemFrame()
    {
        const MARGINS none{0, 0, 0, 0};
        DwmExtendFrameIntoClientArea(hwnd(), &none);
        SetWindowPos(hwnd(), nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }

    // The whole window is client area. Maximised, it is the monitor's work
    // area, less a pixel on the edge of an auto-hidden taskbar so that the
    // taskbar can still come up.
    bool calculateSize(const MSG *msg, qintptr *result)
    {
        if (!msg->wParam)
            return answer(result, 0); // the proposed window rectangle is the client's as it is
        auto *params = reinterpret_cast<NCCALCSIZE_PARAMS *>(msg->lParam);
        if (IsZoomed(hwnd())) {
            MONITORINFO monitor{};
            monitor.cbSize = sizeof(monitor);
            if (GetMonitorInfoW(MonitorFromWindow(hwnd(), MONITOR_DEFAULTTONEAREST), &monitor)) {
                RECT &client = params->rgrc[0];
                client = monitor.rcWork;
                // An auto-hidden taskbar on this monitor's edge, the primary's
                // or a secondary one's (Shell_SecondaryTrayWnd).
                for (const UINT edge : {ABE_LEFT, ABE_TOP, ABE_RIGHT, ABE_BOTTOM}) {
                    APPBARDATA bar{};
                    bar.cbSize = sizeof(bar);
                    bar.uEdge = edge;
                    bar.rc = monitor.rcMonitor;
                    if (!reinterpret_cast<HWND>(SHAppBarMessage(ABM_GETAUTOHIDEBAREX, &bar)))
                        continue;
                    switch (edge) {
                    case ABE_LEFT: client.left += 1; break;
                    case ABE_TOP: client.top += 1; break;
                    case ABE_RIGHT: client.right -= 1; break;
                    default: client.bottom -= 1; break;
                    }
                }
            }
        }
        return answer(result, 0);
    }

    // The resize band: the sizing frame Windows would draw outside the
    // window, inside its edges here (at the top, the thin sizing frame
    // alone, so the window buttons keep most of their height).
    LRESULT hitTest(POINT screen) const
    {
        RECT window{};
        GetWindowRect(hwnd(), &window);
        if (m_frame->resizable() && !IsZoomed(hwnd()) && m_window->visibility() != QWindow::FullScreen) {
            const UINT dpi = GetDpiForWindow(hwnd());
            const int band = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            const int topBand = GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi);
            const bool left = screen.x < window.left + band;
            const bool right = screen.x >= window.right - band;
            const bool top = screen.y < window.top + topBand;
            const bool topCorner = screen.y < window.top + band;
            const bool bottom = screen.y >= window.bottom - band;
            if (bottom)
                return left ? HTBOTTOMLEFT : right ? HTBOTTOMRIGHT : HTBOTTOM;
            if (topCorner && (left || right))
                return left ? HTTOPLEFT : HTTOPRIGHT;
            if (top)
                return HTTOP;
            if (left)
                return HTLEFT;
            if (right)
                return HTRIGHT;
        }
        POINT client = screen;
        ScreenToClient(hwnd(), &client);
        const qreal scale = m_window->devicePixelRatio();
        switch (m_frame->hitAt(QPointF(client.x / scale, client.y / scale))) {
        case MainWindowFrame::Caption:
            return HTCAPTION;
        case MainWindowFrame::MaximizeButton:
            return HTMAXBUTTON;
        case MainWindowFrame::WindowMenuButton:
            return HTSYSMENU;
        case MainWindowFrame::Client:
            break;
        }
        return HTCLIENT;
    }

    void trackMaximizeButton(bool over)
    {
        if (over && !m_tracking) {
            TRACKMOUSEEVENT track{};
            track.cbSize = sizeof(track);
            track.dwFlags = TME_LEAVE | TME_NONCLIENT;
            track.hwndTrack = hwnd();
            m_tracking = TrackMouseEvent(&track);
        }
        m_frame->setMaximizeState(over, over && m_frame->maximizePressed());
    }

    // The system menu, its items enabled for the window's state (MuseScore's
    // updateContextMenuState).
    bool trackSystemMenu(POINT at)
    {
        HMENU menu = GetSystemMenu(hwnd(), FALSE);
        if (!menu)
            return false;
        const bool maximized = IsZoomed(hwnd());
        const bool sizes = m_frame->resizable();
        const auto enable = [menu](UINT item, bool on) {
            EnableMenuItem(menu, item, MF_BYCOMMAND | (on ? MF_ENABLED : MF_GRAYED));
        };
        enable(SC_RESTORE, maximized);
        enable(SC_MOVE, !maximized);
        enable(SC_SIZE, sizes && !maximized);
        enable(SC_MINIMIZE, !m_frame->dialog());
        enable(SC_MAXIMIZE, sizes && !maximized);
        enable(SC_CLOSE, true);
        SetMenuDefaultItem(menu, SC_CLOSE, FALSE);
        const UINT flags = TPM_RETURNCMD | TPM_RIGHTBUTTON
                           | (GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN);
        const UINT command = TrackPopupMenu(menu, flags, at.x, at.y, 0, hwnd(), nullptr);
        if (command)
            PostMessageW(hwnd(), WM_SYSCOMMAND, command, 0);
        return true;
    }

    void eraseBackground()
    {
        const QColor colour = m_window->color();
        HDC dc = GetDC(hwnd());
        RECT rect{};
        GetClientRect(hwnd(), &rect);
        HBRUSH brush = CreateSolidBrush(RGB(colour.red(), colour.green(), colour.blue()));
        FillRect(dc, &rect, brush);
        DeleteObject(brush);
        ReleaseDC(hwnd(), dc);
    }

    MainWindowFrame *const m_frame;
    QPointer<QQuickWindow> m_window;
    bool m_custom = false;
    bool m_tracking = false;
};

} // namespace

std::unique_ptr<NativeFrame> createNativeFrame(MainWindowFrame *frame, QQuickWindow *window)
{
    return std::make_unique<WindowsFrame>(frame, window);
}

} // namespace hikari::ui::windowframe
