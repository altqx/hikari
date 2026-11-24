#pragma once

// On Windows HikariSub.exe is a small launcher: it shows a splash, then loads the app from kAppLibrary and
// calls kEntryPoint, which calls `shown` once the main window has drawn its first frame.

#include "hikari/application/update_check.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace hikari::app::launcher {

inline constexpr wchar_t kAppLibrary[] = L"HikariSubApp.dll";
inline constexpr char kEntryPoint[] = "hikari_app_main";
using Shown = void (*)();
using Entry = int (*)(int argc, char **argv, Shown shown);

inline constexpr wchar_t kRunningMutex[] = L"Global\\io.github.altqx.HikariSub.Running";

struct Look {
    std::uint32_t background;
    std::uint32_t text;
    std::uint32_t muted;
    bool operator==(const Look &) const = default;
};
inline constexpr Look kDarkLook{0x181B20, 0xE4E7EB, 0xA0A8B2};
inline constexpr Look kLightLook{0xEEF0F3, 0x1E2329, 0x56616C};
// Under %LOCALAPPDATA%, beside the app's cache: "#background #text #muted".
inline constexpr wchar_t kLookFile[] = L"\\HikariSub\\cache\\startup-look.txt";

inline std::optional<Look> parseLook(std::string_view text)
{
    std::array<std::uint32_t, 3> colours{};
    std::size_t at = 0;
    for (std::uint32_t &colour : colours) {
        while (at < text.size() && (text[at] == ' ' || text[at] == '\r' || text[at] == '\n'))
            ++at;
        if (text.size() - at < 7 || text[at] != '#')
            return std::nullopt;
        colour = 0;
        for (const char c : text.substr(at + 1, 6)) {
            const int digit = c >= '0' && c <= '9'   ? c - '0'
                              : c >= 'a' && c <= 'f' ? c - 'a' + 10
                              : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                     : -1;
            if (digit < 0)
                return std::nullopt;
            colour = colour << 4 | std::uint32_t(digit);
        }
        at += 7;
    }
    return Look{colours[0], colours[1], colours[2]};
}

// The starts that never show a window: --version, --export-docs, --test-plugin and the update handoff.
inline bool startsWindow(std::span<const std::string_view> arguments)
{
    if (!arguments.empty() && arguments.front() == application::kApplyUpdateSwitch)
        return false;
    for (const std::string_view argument : arguments)
        if (argument == "--version" || argument == "--export-docs" || argument.starts_with("--export-docs=") ||
            argument == "--test-plugin")
            return false;
    return true;
}

}
