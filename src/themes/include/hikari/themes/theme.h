#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::themes {

// 0xAARRGGBB.
using Colour = std::uint32_t;

enum class Type { Light, Dark, HighContrastLight, HighContrastDark };
std::string_view typeName(Type type);
std::optional<Type> typeFromName(std::string_view name);
bool isDark(Type type);
bool isHighContrast(Type type);

enum class Role {
    Background,
    Panel,
    Raised,
    Field,
    Text,
    Muted,
    Line,
    Control,
    Disabled,
    Accent,
    OnAccent,
    Select,
    Hover,
    Pressed,
    Focus,
    Danger,
    Warning,
    Success,
};
inline constexpr std::size_t kRoleCount = 18;
// In Role's order; the theme file's keys.
inline constexpr std::array<std::string_view, kRoleCount> kRoleNames{
    "background", "panel",  "raised", "field", "text",  "muted",  "line",   "control", "disabled",
    "accent",     "onAccent", "select", "hover", "pressed", "focus", "danger", "warning", "success"};
std::optional<Role> roleFromName(std::string_view name);

using Roles = std::array<Colour, kRoleCount>;
inline Colour at(const Roles &roles, Role role)
{
    return roles[static_cast<std::size_t>(role)];
}

struct AccentChoice {
    std::string id;
    std::string title;
    Colour accent = 0;
    std::optional<Colour> onAccent;
    Colour select = 0;
};

enum class Label { Normal, Modified, Saved, Doubtful };

struct GridColours {
    Colour alternate = 0, warning = 0, comment = 0;
    std::array<Colour, 5> comparison{};
    std::optional<Colour> comparisonSelection;
    // In Label's order.
    std::array<Colour, 4> labels{};
};

struct AudioColours {
    Colour background = 0, lineStart = 0, lineEnd = 0, inactiveBoundary = 0, cursor = 0, secondBoundaries = 0,
           keyframe = 0;
    std::optional<Colour> selectionBackground, selectionModified;
    Colour inactiveBackground = 0, waveform = 0, waveformInactive = 0, waveformModified = 0;
    std::optional<Colour> waveformSelected;
    Colour timescaleBackground = 0, timescaleText = 0, spectrumBackground = 0, spectrumEcho = 0, spectrumInner = 0,
           lineBoundaryMark = 0, syllableBoundaries = 0, syllableText = 0;
};

struct VisualToolColours {
    Colour handle = 0, handleSelected = 0, handleBorder = 0;
};

inline constexpr std::size_t kSyntaxCount = 13;
inline constexpr std::size_t kPortCount = 8;

struct Content {
    Colour spellcheck = 0, grammar = 0;
    GridColours grid;
    // In core::syntax::Role's order; the first, the default, may be left out.
    std::array<std::optional<Colour>, kSyntaxCount> syntax{};
    // units, tags, number, text, colour, point, bool, drawing.
    std::array<Colour, kPortCount> ports{};
    AudioColours audio;
    VisualToolColours visualTools;
};

// A theme file as written: a role it leaves out is nullopt.
struct ThemeFile {
    Type type = Type::Dark;
    std::array<std::optional<Colour>, kRoleCount> roles{};
    std::vector<AccentChoice> accents;
    std::vector<Role> userColours;
    Content content;
};

// Every colour worked out (docs/plugins.md#themes-and-icons).
struct Resolved {
    Type type = Type::Dark;
    Roles roles{};
    Content content;
};

// What the user chose on top of a theme: an accent choice id and their colours.
struct Picks {
    std::string accent;
    std::map<Role, Colour> colours;
};

struct Problem {
    std::string path;
    std::string message;
};

std::string_view schema();

// Schema plus the rules the schema can't say; no contrast check.
std::expected<ThemeFile, Problem> parse(std::string_view json);

// The accent choice `picks.accent` names, else the default one; user colours only for roles in userColours.
Resolved resolve(const ThemeFile &file, const Picks &picks = {});
const AccentChoice *defaultAccent(const ThemeFile &file);

// The floors of docs/plugins.md, on the theme with each of its accent choices.
std::optional<Problem> checkContrast(const ThemeFile &file);

// parse, then checkContrast: what installing a theme runs.
std::expected<ThemeFile, Problem> load(std::string_view json);

double contrastRatio(Colour a, Colour b);
Colour composite(Colour over, Colour under);
Colour withAlpha(Colour colour, int alpha);

}
