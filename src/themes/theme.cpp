#include "hikari/themes/theme.h"

#include "hikari/api/schema.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace hikari::themes {

namespace {

using api::Json;

constexpr std::array<std::string_view, 4> kTypeNames{"light", "dark", "highContrastLight", "highContrastDark"};
constexpr std::array<std::string_view, 4> kLabels{"normal", "modified", "saved", "doubtful"};
constexpr std::array<std::string_view, kSyntaxCount> kSyntax{
    "default",           "curlyBraces",       "operators",         "names",           "values",
    "splitAndDrawings",  "templateVariables", "templateCodeMarks", "templateFunctions", "templateKeywords",
    "templateStrings",   "drawingX",          "drawingY"};
constexpr std::array<std::string_view, kPortCount> kPorts{"units", "tags", "number", "text",
                                                          "colour", "point", "bool", "drawing"};

struct Schema {
    Json root;
    Json defs;
};

const Schema &loadedSchema()
{
    static const Schema loaded = [] {
        Schema s{*Json::parse(schema()), Json::object()};
        auto &members = s.root.asObject();
        for (auto it = members.begin(); it != members.end();) {
            if (it->key == "$defs")
                s.defs = it->value;
            if (it->key.starts_with('$'))
                it = members.erase(it);
            else
                ++it;
        }
        return s;
    }();
    return loaded;
}

int hexDigit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return c - 'A' + 10;
}

// The schema has already checked the form.
Colour colourOf(const Json &value)
{
    const std::string &text = value.asString();
    std::uint32_t rgba = 0;
    for (const char c : std::string_view(text).substr(1))
        rgba = rgba << 4 | std::uint32_t(hexDigit(c));
    return text.size() == 7 ? 0xFF000000u | rgba : (rgba & 0xFFu) << 24 | rgba >> 8;
}

Colour colourAt(const Json &object, std::string_view key)
{
    return colourOf(*object.find(key));
}

std::optional<Colour> optionalAt(const Json &object, std::string_view key)
{
    const Json *value = object.find(key);
    return value ? std::optional(colourOf(*value)) : std::nullopt;
}

int hoverAlpha(Type type)
{
    switch (type) {
    case Type::Light:
        return 13;
    case Type::Dark:
        return 15;
    case Type::HighContrastLight:
        return 20;
    case Type::HighContrastDark:
        return 31;
    }
    return 15;
}

Colour opaque(Colour colour)
{
    return colour | 0xFF000000u;
}

Colour readableOn(Colour background)
{
    constexpr Colour black = 0xFF000000u, white = 0xFFFFFFFFu;
    return contrastRatio(black, background) >= contrastRatio(white, background) ? black : white;
}

std::string ratioText(double ratio)
{
    char text[16];
    std::snprintf(text, sizeof text, "%.2f", std::floor(ratio * 100) / 100);
    return text;
}

std::string hex(Colour colour)
{
    char text[10];
    std::snprintf(text, sizeof text, "#%06X", colour & 0xFFFFFFu);
    return text;
}

}

std::string_view typeName(Type type)
{
    return kTypeNames[static_cast<std::size_t>(type)];
}

std::optional<Type> typeFromName(std::string_view name)
{
    const auto it = std::ranges::find(kTypeNames, name);
    return it == kTypeNames.end() ? std::nullopt : std::optional(Type(it - kTypeNames.begin()));
}

bool isDark(Type type)
{
    return type == Type::Dark || type == Type::HighContrastDark;
}

bool isHighContrast(Type type)
{
    return type == Type::HighContrastLight || type == Type::HighContrastDark;
}

std::optional<Role> roleFromName(std::string_view name)
{
    const auto it = std::ranges::find(kRoleNames, name);
    return it == kRoleNames.end() ? std::nullopt : std::optional(Role(it - kRoleNames.begin()));
}

double contrastRatio(Colour a, Colour b)
{
    const auto luminance = [](Colour c) {
        const auto channel = [](int v) {
            const double s = v / 255.0;
            return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * channel(c >> 16 & 0xFF) + 0.7152 * channel(c >> 8 & 0xFF) + 0.0722 * channel(c & 0xFF);
    };
    const double x = luminance(a), y = luminance(b);
    return (std::max(x, y) + 0.05) / (std::min(x, y) + 0.05);
}

Colour composite(Colour over, Colour under)
{
    const double a = (over >> 24) / 255.0;
    const auto mix = [a](int shift, Colour o, Colour u) {
        return Colour(std::lround((o >> shift & 0xFF) * a + (u >> shift & 0xFF) * (1 - a))) << shift;
    };
    return 0xFF000000u | mix(16, over, under) | mix(8, over, under) | mix(0, over, under);
}

Colour withAlpha(Colour colour, int alpha)
{
    return (colour & 0xFFFFFFu) | Colour(alpha & 0xFF) << 24;
}

std::expected<ThemeFile, Problem> parse(std::string_view json)
{
    auto parsed = Json::parse(json);
    if (!parsed)
        return std::unexpected(Problem{"", parsed.error().message});
    const Json &root = *parsed;
    if (auto failure = api::validate(loadedSchema().root, root, loadedSchema().defs))
        return std::unexpected(Problem{failure->path, failure->message});

    ThemeFile file;
    file.type = *typeFromName(root.find("type")->asString());
    const Json &roles = *root.find("roles");
    for (std::size_t i = 0; i < kRoleCount; ++i)
        file.roles[i] = optionalAt(roles, kRoleNames[i]);

    if (const Json *accents = root.find("accents")) {
        std::set<std::string> ids;
        for (std::size_t i = 0; i < accents->asArray().size(); ++i) {
            const Json &a = accents->asArray()[i];
            AccentChoice choice{a.find("id")->asString(), a.find("title")->asString(), colourAt(a, "accent"),
                                optionalAt(a, "onAccent"), colourAt(a, "select")};
            if (!ids.insert(choice.id).second)
                return std::unexpected(Problem{"/accents/" + std::to_string(i) + "/id", "repeats an earlier id"});
            file.accents.push_back(std::move(choice));
        }
        if (!defaultAccent(file))
            return std::unexpected(Problem{"/accents", "no choice equals the roles' accent, onAccent and select"});
    }
    if (const Json *user = root.find("userColours")) {
        for (std::size_t i = 0; i < user->asArray().size(); ++i) {
            const Role role = *roleFromName(user->asArray()[i].asString());
            if (std::ranges::contains(file.userColours, role))
                return std::unexpected(Problem{"/userColours/" + std::to_string(i), "repeats an earlier role"});
            file.userColours.push_back(role);
        }
    }

    const Json &content = *root.find("content");
    Content &c = file.content;
    c.spellcheck = colourAt(content, "spellcheck");
    c.grammar = colourAt(content, "grammar");
    const Json &grid = *content.find("grid");
    c.grid.alternate = colourAt(grid, "alternate");
    c.grid.warning = colourAt(grid, "warning");
    c.grid.comment = colourAt(grid, "comment");
    for (std::size_t i = 0; i < c.grid.comparison.size(); ++i)
        c.grid.comparison[i] = colourOf(grid.find("comparison")->asArray()[i]);
    c.grid.comparisonSelection = optionalAt(grid, "comparisonSelection");
    const Json &labels = *grid.find("labels");
    for (std::size_t i = 0; i < kLabels.size(); ++i)
        c.grid.labels[i] = colourAt(labels, kLabels[i]);

    const Json &syntax = *content.find("syntax");
    for (std::size_t i = 0; i < kSyntax.size(); ++i)
        c.syntax[i] = optionalAt(syntax, kSyntax[i]);

    const Json &ports = *content.find("ports");
    for (std::size_t i = 0; i < kPorts.size(); ++i)
        c.ports[i] = colourAt(ports, kPorts[i]);

    const Json &audio = *content.find("audio");
    AudioColours &a = c.audio;
    a.background = colourAt(audio, "background");
    a.lineStart = colourAt(audio, "lineStart");
    a.lineEnd = colourAt(audio, "lineEnd");
    a.inactiveBoundary = colourAt(audio, "inactiveBoundary");
    a.cursor = colourAt(audio, "cursor");
    a.secondBoundaries = colourAt(audio, "secondBoundaries");
    a.keyframe = colourAt(audio, "keyframe");
    a.selectionBackground = optionalAt(audio, "selectionBackground");
    a.selectionModified = optionalAt(audio, "selectionModified");
    a.inactiveBackground = colourAt(audio, "inactiveBackground");
    a.waveform = colourAt(audio, "waveform");
    a.waveformInactive = colourAt(audio, "waveformInactive");
    a.waveformModified = colourAt(audio, "waveformModified");
    a.waveformSelected = optionalAt(audio, "waveformSelected");
    a.timescaleBackground = colourAt(audio, "timescaleBackground");
    a.timescaleText = colourAt(audio, "timescaleText");
    a.spectrumBackground = colourAt(audio, "spectrumBackground");
    a.spectrumEcho = colourAt(audio, "spectrumEcho");
    a.spectrumInner = colourAt(audio, "spectrumInner");
    a.lineBoundaryMark = colourAt(audio, "lineBoundaryMark");
    a.syllableBoundaries = colourAt(audio, "syllableBoundaries");
    a.syllableText = colourAt(audio, "syllableText");

    const Json &tools = *content.find("visualTools");
    c.visualTools = {colourAt(tools, "handle"), colourAt(tools, "handleSelected"), colourAt(tools, "handleBorder")};
    return file;
}

const AccentChoice *defaultAccent(const ThemeFile &file)
{
    const Resolved plain = resolve(ThemeFile{file.type, file.roles, {}, {}, file.content});
    for (const AccentChoice &choice : file.accents)
        if (choice.accent == at(plain.roles, Role::Accent) && choice.select == at(plain.roles, Role::Select) &&
            choice.onAccent.value_or(readableOn(choice.accent)) == at(plain.roles, Role::OnAccent))
            return &choice;
    return nullptr;
}

Resolved resolve(const ThemeFile &file, const Picks &picks)
{
    auto given = file.roles;
    const auto set = [&given](Role role) -> std::optional<Colour> & { return given[static_cast<std::size_t>(role)]; };

    const AccentChoice *choice = nullptr;
    for (const AccentChoice &a : file.accents)
        if (a.id == picks.accent)
            choice = &a;
    if (!choice && !file.accents.empty())
        choice = defaultAccent(file);
    if (choice) {
        set(Role::Accent) = choice->accent;
        set(Role::OnAccent) = choice->onAccent;
        set(Role::Select) = choice->select;
    }
    for (const auto &[role, colour] : picks.colours)
        if (std::ranges::contains(file.userColours, role)) {
            set(role) = opaque(colour);
            if (role == Role::Accent && !picks.colours.contains(Role::OnAccent))
                set(Role::OnAccent) = file.roles[static_cast<std::size_t>(Role::OnAccent)];
        }

    Resolved out;
    out.type = file.type;
    for (std::size_t i = 0; i < kRoleCount; ++i)
        out.roles[i] = given[i].value_or(0);
    const auto role = [&out](Role r) -> Colour & { return out.roles[static_cast<std::size_t>(r)]; };
    const Colour text = role(Role::Text);
    if (!set(Role::Focus))
        role(Role::Focus) = text;
    if (!set(Role::Control))
        role(Role::Control) = role(Role::Line);
    if (!set(Role::OnAccent))
        role(Role::OnAccent) = readableOn(role(Role::Accent));
    if (!set(Role::Hover))
        role(Role::Hover) = withAlpha(text, hoverAlpha(file.type));
    if (!set(Role::Pressed))
        role(Role::Pressed) = withAlpha(text, std::min(255, 2 * int(role(Role::Hover) >> 24)));

    const Colour accent = role(Role::Accent);
    out.content = file.content;
    Content &c = out.content;
    if (!c.syntax[0])
        c.syntax[0] = text;
    if (!c.grid.comparisonSelection)
        c.grid.comparisonSelection = withAlpha(accent, 75);
    if (!c.audio.selectionBackground)
        c.audio.selectionBackground = withAlpha(accent, 0x37);
    if (!c.audio.selectionModified)
        c.audio.selectionModified = c.audio.selectionBackground;
    if (!c.audio.waveformSelected)
        c.audio.waveformSelected = accent;
    return out;
}

namespace {

struct Floors {
    const Resolved &theme;
    std::string accentNote;
    std::optional<Problem> problem;

    void need(Colour fore, std::string_view foreName, Colour back, std::string_view backName, double floor)
    {
        if (problem)
            return;
        const Colour under = composite(back, at(theme.roles, Role::Panel));
        const double ratio = contrastRatio(composite(fore, under), under);
        if (ratio + 1e-9 < floor)
            problem = Problem{std::string(foreName),
                              std::string(foreName) + " " + hex(fore) + " on " + std::string(backName) + " " +
                                  hex(back) + accentNote + " measures " + ratioText(ratio) + ":1, below " +
                                  ratioText(floor) + ":1"};
    }

    void onSurfaces(Role role, double floor)
    {
        for (const Role surface : {Role::Background, Role::Panel, Role::Raised, Role::Field})
            need(at(theme.roles, role), path(role), at(theme.roles, surface), kRoleNames[std::size_t(surface)], floor);
    }

    static std::string path(Role role) { return "/roles/" + std::string(kRoleNames[std::size_t(role)]); }
};

std::optional<Problem> checkOne(const Resolved &t, std::string accentNote)
{
    const bool hc = isHighContrast(t.type);
    const double text = hc ? 7 : 4.5;
    Floors f{t, std::move(accentNote), {}};
    for (const Role r : {Role::Text, Role::Muted, Role::Danger, Role::Warning, Role::Accent})
        f.onSurfaces(r, text);
    if (t.type != Type::Light)
        f.onSurfaces(Role::Success, text);
    if (hc)
        f.onSurfaces(Role::Line, 7);
    for (const Role r : {Role::Focus, Role::Disabled, Role::Control})
        f.onSurfaces(r, 3);
    const auto r = [&t](Role role) { return at(t.roles, role); };
    f.need(r(Role::OnAccent), "/roles/onAccent", r(Role::Accent), "accent", text);
    f.need(r(Role::Text), "/roles/text", r(Role::Select), "select", text);
    f.need(r(Role::Disabled), "/roles/disabled", r(Role::Text), "text", 2);
    const Content &c = t.content;
    f.need(c.spellcheck, "/content/spellcheck", r(Role::Field), "field", 3);
    f.need(c.spellcheck, "/content/spellcheck", r(Role::Panel), "panel", 3);
    f.need(c.spellcheck, "/content/spellcheck", composite(c.grid.alternate, r(Role::Field)), "grid alternate", 3);
    f.need(c.grammar, "/content/grammar", r(Role::Field), "field", 3);
    if (t.type != Type::Dark) {
        f.need(c.audio.waveform, "/content/audio/waveform", c.audio.background, "audio background", hc ? 4.5 : 3);
        f.need(c.audio.cursor, "/content/audio/cursor", c.audio.background, "audio background", 3);
        f.need(c.audio.timescaleText, "/content/audio/timescaleText", c.audio.timescaleBackground,
               "timescale background", 4.5);
    }
    return f.problem;
}

}

std::optional<Problem> checkContrast(const ThemeFile &file)
{
    if (file.accents.empty())
        return checkOne(resolve(file), "");
    for (const AccentChoice &choice : file.accents)
        if (auto problem = checkOne(resolve(file, {choice.id, {}}), " with the accent " + choice.id))
            return problem;
    return std::nullopt;
}

std::expected<ThemeFile, Problem> load(std::string_view json)
{
    auto file = parse(json);
    if (!file)
        return file;
    if (auto problem = checkContrast(*file))
        return std::unexpected(*problem);
    return file;
}

ThemeFile complete(const ThemeFile &file)
{
    const auto userPicks = [&file](Role role) { return std::ranges::contains(file.userColours, role); };
    const auto workedOutFrom = [](Role role) -> std::optional<Role> {
        switch (role) {
        case Role::Focus:
        case Role::Hover:
        case Role::Pressed:
            return Role::Text;
        case Role::Control:
            return Role::Line;
        case Role::OnAccent:
            return Role::Accent;
        default:
            return std::nullopt;
        }
    };
    const Resolved resolved = resolve(file);
    ThemeFile out = file;
    for (std::size_t i = 0; i < kRoleCount; ++i) {
        const auto source = workedOutFrom(Role(i));
        if (!out.roles[i] && !(source && userPicks(*source)))
            out.roles[i] = resolved.roles[i];
    }
    for (AccentChoice &choice : out.accents)
        choice.onAccent = at(resolve(file, {choice.id, {}}).roles, Role::OnAccent);

    Content &c = out.content;
    const Content &worked = resolved.content;
    if (!userPicks(Role::Text))
        c.syntax[0] = worked.syntax[0];
    if (file.accents.empty() && !userPicks(Role::Accent)) {
        c.grid.comparisonSelection = worked.grid.comparisonSelection;
        c.audio.selectionBackground = worked.audio.selectionBackground;
        c.audio.selectionModified = worked.audio.selectionModified;
        c.audio.waveformSelected = worked.audio.waveformSelected;
    }
    return out;
}

namespace {

Json colourJson(Colour colour)
{
    char text[12];
    if (colour >> 24 == 0xFF)
        std::snprintf(text, sizeof text, "#%06X", colour & 0xFFFFFFu);
    else
        std::snprintf(text, sizeof text, "#%06X%02X", colour & 0xFFFFFFu, colour >> 24);
    return Json(text);
}

void put(Json &object, std::string_view key, std::optional<Colour> colour)
{
    if (colour)
        object[key] = colourJson(*colour);
}

Json contentJson(const Content &c)
{
    Json content = Json::object();
    put(content, "spellcheck", c.spellcheck);
    put(content, "grammar", c.grammar);
    Json grid = Json::object();
    put(grid, "alternate", c.grid.alternate);
    put(grid, "warning", c.grid.warning);
    put(grid, "comment", c.grid.comment);
    Json comparison = Json::array();
    for (const Colour colour : c.grid.comparison)
        comparison.push(colourJson(colour));
    grid["comparison"] = std::move(comparison);
    put(grid, "comparisonSelection", c.grid.comparisonSelection);
    Json labels = Json::object();
    for (std::size_t i = 0; i < kLabels.size(); ++i)
        put(labels, kLabels[i], c.grid.labels[i]);
    grid["labels"] = std::move(labels);
    content["grid"] = std::move(grid);

    Json syntax = Json::object();
    for (std::size_t i = 0; i < kSyntax.size(); ++i)
        put(syntax, kSyntax[i], c.syntax[i]);
    content["syntax"] = std::move(syntax);
    Json ports = Json::object();
    for (std::size_t i = 0; i < kPorts.size(); ++i)
        put(ports, kPorts[i], c.ports[i]);
    content["ports"] = std::move(ports);

    const AudioColours &a = c.audio;
    Json audio = Json::object();
    put(audio, "background", a.background);
    put(audio, "lineStart", a.lineStart);
    put(audio, "lineEnd", a.lineEnd);
    put(audio, "inactiveBoundary", a.inactiveBoundary);
    put(audio, "cursor", a.cursor);
    put(audio, "secondBoundaries", a.secondBoundaries);
    put(audio, "keyframe", a.keyframe);
    put(audio, "selectionBackground", a.selectionBackground);
    put(audio, "selectionModified", a.selectionModified);
    put(audio, "inactiveBackground", a.inactiveBackground);
    put(audio, "waveform", a.waveform);
    put(audio, "waveformInactive", a.waveformInactive);
    put(audio, "waveformModified", a.waveformModified);
    put(audio, "waveformSelected", a.waveformSelected);
    put(audio, "timescaleBackground", a.timescaleBackground);
    put(audio, "timescaleText", a.timescaleText);
    put(audio, "spectrumBackground", a.spectrumBackground);
    put(audio, "spectrumEcho", a.spectrumEcho);
    put(audio, "spectrumInner", a.spectrumInner);
    put(audio, "lineBoundaryMark", a.lineBoundaryMark);
    put(audio, "syllableBoundaries", a.syllableBoundaries);
    put(audio, "syllableText", a.syllableText);
    content["audio"] = std::move(audio);

    Json tools = Json::object();
    put(tools, "handle", c.visualTools.handle);
    put(tools, "handleSelected", c.visualTools.handleSelected);
    put(tools, "handleBorder", c.visualTools.handleBorder);
    content["visualTools"] = std::move(tools);
    return content;
}

}

std::string write(const ThemeFile &file)
{
    Json root = Json::object();
    root["$schema"] = "https://hikarisub.altqx.com/schemas/hikari-theme.schema.json";
    root["type"] = typeName(file.type);
    Json roles = Json::object();
    for (std::size_t i = 0; i < kRoleCount; ++i)
        put(roles, kRoleNames[i], file.roles[i]);
    root["roles"] = std::move(roles);
    if (!file.accents.empty()) {
        Json accents = Json::array();
        for (const AccentChoice &choice : file.accents) {
            Json a = Json::object({{"id", choice.id}, {"title", choice.title}});
            put(a, "accent", choice.accent);
            put(a, "onAccent", choice.onAccent);
            put(a, "select", choice.select);
            accents.push(std::move(a));
        }
        root["accents"] = std::move(accents);
    }
    if (!file.userColours.empty()) {
        Json user = Json::array();
        for (const Role role : file.userColours)
            user.push(kRoleNames[std::size_t(role)]);
        root["userColours"] = std::move(user);
    }
    root["content"] = contentJson(file.content);
    return root.dump(2) + "\n";
}
}
