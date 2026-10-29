#include "title_bars.h"

#include <QWindow>

#ifdef Q_OS_WIN
#include <qt_windows.h>

#include <dwmapi.h>
#endif

namespace hikari::ui::titlebars {

namespace {

titlebars::Setter &overridden()
{
    static titlebars::Setter setter;
    return setter;
}

#ifdef Q_OS_WIN
// DWMWA_USE_IMMERSIVE_DARK_MODE: 20 from Windows 10 20H1, 19 before it.
constexpr DWORD kDarkMode = 20;
constexpr DWORD kDarkModeBefore20H1 = 19;

HWND nativeWindow(QWindow *window)
{
    // handle(), not winId(): winId() would make the native window now.
    return window->handle() ? reinterpret_cast<HWND>(window->winId()) : nullptr;
}
#endif

}

bool themed(const QWindow *window)
{
    if (!window || !window->isTopLevel() || window->flags().testFlag(Qt::FramelessWindowHint))
        return false;
    switch (window->type()) {
    case Qt::Popup:
    case Qt::ToolTip:
    case Qt::SplashScreen:
    case Qt::ForeignWindow:
        return false;
    default:
        return true;
    }
}

void setDark(QWindow *window, bool dark)
{
    if (!window)
        return;
    if (const Setter &setter = overridden()) {
        setter(window, dark);
        return;
    }
#ifdef Q_OS_WIN
    if (HWND hwnd = nativeWindow(window)) {
        const BOOL value = dark ? TRUE : FALSE;
        if (FAILED(DwmSetWindowAttribute(hwnd, kDarkMode, &value, sizeof(value))))
            DwmSetWindowAttribute(hwnd, kDarkModeBefore20H1, &value, sizeof(value));
    }
#endif
}

std::optional<bool> isDark(QWindow *window)
{
#ifdef Q_OS_WIN
    if (HWND hwnd = window ? nativeWindow(window) : nullptr) {
        BOOL value = FALSE;
        if (SUCCEEDED(DwmGetWindowAttribute(hwnd, kDarkMode, &value, sizeof(value)))
            || SUCCEEDED(DwmGetWindowAttribute(hwnd, kDarkModeBefore20H1, &value, sizeof(value))))
            return value != FALSE;
    }
#else
    Q_UNUSED(window);
#endif
    return std::nullopt;
}

void overrideSetter(Setter setter)
{
    overridden() = std::move(setter);
}

}
