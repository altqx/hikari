#include "FileTypeIcons.h"
#include "hikari/app/launcher.h"
#include "hikari/app/update_apply.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <string>
#include <vector>

namespace launcher = hikari::app::launcher;

namespace {

constexpr UINT kDismiss = WM_APP + 1;
constexpr UINT_PTR kTick = 1;
constexpr int kWidth = 480;
constexpr int kHeight = 270;
constexpr int kIcon = 96;
constexpr int kBarHeight = 3;

std::atomic<HWND> g_splash = nullptr;

struct Splash {
    launcher::Look look;
    HANDLE painted = nullptr;
    HICON icon = nullptr;
    HFONT font = nullptr;
    HFONT small = nullptr;
    UINT dpi = 96;
    ULONGLONG started = 0;
};

COLORREF colour(std::uint32_t rgb)
{
    return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

std::uint32_t mix(std::uint32_t a, std::uint32_t b, int bPercent)
{
    std::uint32_t out = 0;
    for (int shift = 0; shift <= 16; shift += 8) {
        const int ca = int((a >> shift) & 0xFF);
        const int cb = int((b >> shift) & 0xFF);
        out |= std::uint32_t((ca * (100 - bPercent) + cb * bPercent) / 100) << shift;
    }
    return out;
}

std::wstring environment(const wchar_t *name)
{
    std::wstring value(MAX_PATH, L'\0');
    DWORD n = GetEnvironmentVariableW(name, value.data(), DWORD(value.size()));
    if (n > value.size()) {
        value.resize(n);
        n = GetEnvironmentVariableW(name, value.data(), DWORD(value.size()));
    }
    value.resize(n < value.size() ? n : 0);
    return value;
}

std::optional<launcher::Look> savedLook()
{
    const std::wstring folder = environment(L"LOCALAPPDATA");
    if (folder.empty())
        return std::nullopt;
    const HANDLE file = CreateFileW((folder + launcher::kLookFile).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return std::nullopt;
    char text[128];
    DWORD read = 0;
    const BOOL ok = ReadFile(file, text, sizeof text, &read, nullptr);
    CloseHandle(file);
    return ok ? launcher::parseLook(std::string_view(text, read)) : std::nullopt;
}

bool systemUsesLightTheme()
{
    DWORD light = 0;
    DWORD size = sizeof light;
    return RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size) == ERROR_SUCCESS &&
           light != 0;
}

int scaled(int logical, UINT dpi)
{
    return MulDiv(logical, int(dpi), 96);
}

void paint(HWND window, const Splash &splash)
{
    PAINTSTRUCT ps;
    const HDC target = BeginPaint(window, &ps);
    RECT client;
    GetClientRect(window, &client);
    const int width = client.right;
    const int height = client.bottom;
    const HDC dc = CreateCompatibleDC(target);
    const HBITMAP bitmap = CreateCompatibleBitmap(target, width, height);
    const HGDIOBJ oldBitmap = SelectObject(dc, bitmap);

    const auto fill = [dc](RECT r, std::uint32_t rgb) {
        const HBRUSH brush = CreateSolidBrush(colour(rgb));
        FillRect(dc, &r, brush);
        DeleteObject(brush);
    };
    fill(client, splash.look.background);
    const int icon = scaled(kIcon, splash.dpi);
    const int iconTop = scaled(56, splash.dpi);
    if (splash.icon)
        DrawIconEx(dc, (width - icon) / 2, iconTop, splash.icon, icon, icon, 0, nullptr, DI_NORMAL);
    const HGDIOBJ oldFont = SelectObject(dc, splash.font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, colour(splash.look.text));
    RECT title{0, iconTop + icon + scaled(14, splash.dpi), width, height};
    DrawTextW(dc, L"HikariSub", -1, &title, DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, splash.small);
    SetTextColor(dc, colour(splash.look.muted));
    TEXTMETRICW metrics{};
    GetTextMetricsW(dc, &metrics);
    RECT version{scaled(320, splash.dpi), 0, width - scaled(16, splash.dpi),
                 height - scaled(14, splash.dpi) + metrics.tmDescent};
    DrawTextW(dc, HIKARI_VERSION_W, -1, &version, DT_RIGHT | DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, oldFont);

    const int bar = scaled(kBarHeight, splash.dpi);
    fill(RECT{0, height - bar, width, height}, mix(splash.look.background, splash.look.muted, 20));
    const int segment = width / 3;
    const ULONGLONG period = 1400;
    const int travel = int((GetTickCount64() - splash.started) % period * (width + segment) / period);
    fill(RECT{std::max(0, travel - segment), height - bar, std::min(width, travel), height}, splash.look.accent);
    const HBRUSH border = CreateSolidBrush(colour(mix(splash.look.background, splash.look.muted, 35)));
    FrameRect(dc, &client, border);
    DeleteObject(border);

    BitBlt(target, 0, 0, width, height, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
    EndPaint(window, &ps);
}

void loadResources(Splash &splash)
{
    const int icon = scaled(kIcon, splash.dpi);
    splash.icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_HIKARISUB_APP),
                                                IMAGE_ICON, icon, icon, LR_DEFAULTCOLOR));
    const auto font = [&splash](int pixels, int weight) {
        return CreateFontW(-scaled(pixels, splash.dpi), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    };
    splash.font = font(26, FW_SEMIBOLD);
    splash.small = font(12, FW_NORMAL);
}

LRESULT CALLBACK splashProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto *splash = reinterpret_cast<Splash *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_NCCREATE:
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW *>(lParam)->lpCreateParams));
        break;
    case WM_PAINT:
        if (splash) {
            paint(window, *splash);
            if (splash->painted)
                SetEvent(splash->painted);
            return 0;
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_TIMER:
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case kDismiss:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        g_splash = nullptr;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

DWORD WINAPI runSplash(void *parameter)
{
    auto *splash = static_cast<Splash *>(parameter);
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{sizeof windowClass};
    windowClass.lpfnWndProc = splashProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_APPSTARTING);
    windowClass.lpszClassName = L"HikariSubSplash";
    RegisterClassExW(&windowClass);

    POINT cursor{};
    GetCursorPos(&cursor);
    MONITORINFO monitor{sizeof monitor};
    GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &monitor);
    const RECT area = monitor.rcWork;
    const HWND window = CreateWindowExW(WS_EX_TOOLWINDOW, windowClass.lpszClassName, L"HikariSub", WS_POPUP, area.left,
                                        area.top, 1, 1, nullptr, nullptr, instance, splash);
    if (!window) {
        SetEvent(splash->painted);
        return 0;
    }
    splash->dpi = GetDpiForWindow(window);
    loadResources(*splash);
    const DWM_WINDOW_CORNER_PREFERENCE round = DWMWCP_ROUND;
    DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &round, sizeof round);
    const int width = scaled(kWidth, splash->dpi);
    const int height = scaled(kHeight, splash->dpi);
    splash->started = GetTickCount64();
    g_splash = window;
    // SetWindowPos, not ShowWindow: the first ShowWindow takes STARTUPINFO's show state, which is the main window's.
    SetWindowPos(window, HWND_TOP, (area.left + area.right - width) / 2, (area.top + area.bottom - height) / 2, width,
                 height, SWP_SHOWWINDOW | SWP_NOACTIVATE);
    UpdateWindow(window);
    SetTimer(window, kTick, 30, nullptr);

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (splash->icon)
        DestroyIcon(splash->icon);
    for (const HFONT font : {splash->font, splash->small})
        if (font)
            DeleteObject(font);
    return 0;
}

void dismissSplash()
{
    if (const HWND window = g_splash.load())
        PostMessageW(window, kDismiss, 0, 0);
}

bool mutexExists(const wchar_t *name)
{
    if (const HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, name)) {
        CloseHandle(mutex);
        return true;
    }
    return GetLastError() == ERROR_ACCESS_DENIED;
}

bool wantsSplash(int argc, char **argv)
{
    std::vector<std::string_view> arguments(argv + std::min(argc, 1), argv + argc);
    return launcher::startsWindow(arguments) && !mutexExists(launcher::kRunningMutex) &&
           !mutexExists(hikari::app::update_apply::kSetupMutex);
}

std::wstring programFolder()
{
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
        if (n < path.size()) {
            path.resize(n);
            break;
        }
        path.resize(path.size() * 2);
    }
    return path.substr(0, path.find_last_of(L"\\/") + 1);
}

int refuse(const std::wstring &library, DWORD error)
{
    wchar_t *reason = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                   error, 0, reinterpret_cast<wchar_t *>(&reason), 0, nullptr);
    const std::wstring text = library + L"\n\n" + (reason ? reason : std::to_wstring(error));
    LocalFree(reason);
    MessageBoxW(nullptr, text.c_str(), L"HikariSub", MB_OK | MB_ICONERROR);
    return 1;
}

}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    Splash splash;
    HANDLE thread = nullptr;
    if (wantsSplash(__argc, __argv)) {
        const std::optional<launcher::Look> saved = savedLook();
        splash.look = saved ? *saved : systemUsesLightTheme() ? launcher::kLightLook : launcher::kDarkLook;
        splash.painted = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        thread = CreateThread(nullptr, 0, runSplash, &splash, 0, nullptr);
        // Painted before the loader lock is taken: the splash thread must not wait on it for its first frame.
        if (thread && splash.painted)
            WaitForSingleObject(splash.painted, 2000);
    }
    const auto finish = [&](int code) {
        dismissSplash();
        if (thread) {
            WaitForSingleObject(thread, 2000);
            CloseHandle(thread);
        }
        return code;
    };

    const std::wstring library = programFolder() + launcher::kAppLibrary;
    const HMODULE app = LoadLibraryExW(library.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!app)
        return finish(refuse(library, GetLastError()));
    const auto entry = reinterpret_cast<launcher::Entry>(GetProcAddress(app, launcher::kEntryPoint));
    if (!entry)
        return finish(refuse(library, GetLastError()));
    return finish(entry(__argc, __argv, dismissSplash));
}
