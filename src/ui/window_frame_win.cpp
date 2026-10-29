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
        // Qt resets the window's styles when it makes the native window and when its flags change.
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
    // Qt passes posted messages with no result (true drops them) and sent ones with one.
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

    // Caption styles on Qt's frameless window keep Windows' shadow, animations, Aero Snap and system menu.
    static constexpr LONG_PTR kCaptionStyles =
        WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME;

    // Without a sizing frame and maximise box, Aero Snap and the system menu leave a fixed-size dialog alone.
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
        // DWM draws the shadow and rounded corners for a 1 px extended frame; extended at the top it takes the caption buttons and the snap layouts never show.
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

    // Maximised: the work area, less a pixel on an auto-hidden taskbar's edge so the taskbar can still come up.
    bool calculateSize(const MSG *msg, qintptr *result)
    {
        if (!msg->wParam)
            return answer(result, 0);
        auto *params = reinterpret_cast<NCCALCSIZE_PARAMS *>(msg->lParam);
        if (IsZoomed(hwnd())) {
            MONITORINFO monitor{};
            monitor.cbSize = sizeof(monitor);
            if (GetMonitorInfoW(MonitorFromWindow(hwnd(), MONITOR_DEFAULTTONEAREST), &monitor)) {
                RECT &client = params->rgrc[0];
                client = monitor.rcWork;
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
            // HTMAXBUTTON makes Windows 11 show its snap layouts over the button.
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

}

std::unique_ptr<NativeFrame> createNativeFrame(MainWindowFrame *frame, QQuickWindow *window)
{
    return std::make_unique<WindowsFrame>(frame, window);
}

}
