#include "theme.h"

#include "settings_store.h"

#include "hikari/application/settings.h"

#include <QGuiApplication>
#include <QPointer>
#include <QQuickStyle>
#include <QStyleHints>
#include <QtQml/qqmlengine.h>

#include <algorithm>
#include <cmath>

namespace hikari::ui {

namespace theme {

namespace {

QColor rgb(QRgb value)
{
    return QColor::fromRgb(value);
}

std::uint32_t toArgb(const QColor &colour)
{
    return colour.rgba();
}

bool isDarkColour(const QColor &colour)
{
    return colour.lightnessF() < 0.5;
}

// The accent presets (K2): seven hues per mode drawn for the Compact Studio
// palette. Light accents carry white text and meet 4.5:1 on every light
// surface; dark accents carry a dark tint of their hue and meet 4.5:1 on
// every dark surface (measured in docs/qt/ux/visual-language.md). Each
// preset's selected background is its hue at the spec green's selected
// lightness and saturation; the focus colour stays the spec's unless the
// accent's hue is within 60 degrees of it (then a blue / sky focus keeps
// focus and accent apart).
const std::array<Accent, 7> kLightAccents{{
    {"red", QT_TRANSLATE_NOOP("Theme", "Red"), rgb(0xB3261E), rgb(0xFFFFFF), rgb(0xEBD7D6), rgb(0x0B57D0)},
    {"orange", QT_TRANSLATE_NOOP("Theme", "Orange"), rgb(0x9C4600), rgb(0xFFFFFF), rgb(0xEBDFD6), rgb(0x0B57D0)},
    {"gold", QT_TRANSLATE_NOOP("Theme", "Gold"), rgb(0x7A5E00), rgb(0xFFFFFF), rgb(0xEBE6D6), rgb(0x0B57D0)},
    {"green", QT_TRANSLATE_NOOP("Theme", "Green"), rgb(0x145C4C), rgb(0xFFFFFF), rgb(0xD6EBE4), rgb(0x8D4200)},
    {"blue", QT_TRANSLATE_NOOP("Theme", "Blue"), rgb(0x1D5BA8), rgb(0xFFFFFF), rgb(0xD6DFEB), rgb(0x8D4200)},
    {"purple", QT_TRANSLATE_NOOP("Theme", "Purple"), rgb(0x5B3FB0), rgb(0xFFFFFF), rgb(0xDBD6EB), rgb(0x8D4200)},
    {"pink", QT_TRANSLATE_NOOP("Theme", "Pink"), rgb(0x9E2A73), rgb(0xFFFFFF), rgb(0xEBD6E3), rgb(0x8D4200)},
}};
const std::array<Accent, 7> kDarkAccents{{
    {"red", QT_TRANSLATE_NOOP("Theme", "Red"), rgb(0xF4A9A3), rgb(0x2C1210), rgb(0x4C3230), rgb(0x8FD8FF)},
    {"orange", QT_TRANSLATE_NOOP("Theme", "Orange"), rgb(0xF5BB8A), rgb(0x2C1D10), rgb(0x4C3D30), rgb(0x8FD8FF)},
    {"gold", QT_TRANSLATE_NOOP("Theme", "Gold"), rgb(0xE3CF7E), rgb(0x2C2610), rgb(0x4C4630), rgb(0x8FD8FF)},
    {"green", QT_TRANSLATE_NOOP("Theme", "Green"), rgb(0x9CDBC9), rgb(0x102C24), rgb(0x304C47), rgb(0xF9D784)},
    {"blue", QT_TRANSLATE_NOOP("Theme", "Blue"), rgb(0xA6C8F2), rgb(0x101D2C), rgb(0x303D4C), rgb(0xF9D784)},
    {"purple", QT_TRANSLATE_NOOP("Theme", "Purple"), rgb(0xC6B5F4), rgb(0x18102C), rgb(0x38304C), rgb(0xF9D784)},
    {"pink", QT_TRANSLATE_NOOP("Theme", "Pink"), rgb(0xEFAAD3), rgb(0x2C1021), rgb(0x4C3041), rgb(0xF9D784)},
}};

// The fixed roles of each theme (visual-language.md): Light and Dark are the
// spec's tokens, with the accent's four from the chosen preset; High contrast
// black is the spec's high-contrast column; High contrast white is drawn to
// match it on white.
Roles baseRoles(Code code)
{
    switch (code) {
    case Code::Light:
        return {rgb(0xE5E9EC), rgb(0xF9FAFB), rgb(0xEDF0F3), rgb(0xFFFFFF), rgb(0x202832), rgb(0x526170), rgb(0xAAB5BE),
                {}, {}, {}, {}, rgb(0xA51F31), rgb(0x8A5A00), rgb(0x008000), rgb(0x74808B)};
    case Code::Dark:
        return {rgb(0x171B20), rgb(0x20262D), rgb(0x29313A), rgb(0x171D24), rgb(0xE8EDF2), rgb(0xA5B1BD), rgb(0x414B57),
                {}, {}, {}, {}, rgb(0xFFADAD), rgb(0xE0A030), rgb(0x008000), rgb(0x75818D)};
    case Code::HighContrastWhite:
        return {rgb(0xFFFFFF), rgb(0xFFFFFF), rgb(0xEBEBEB), rgb(0xFFFFFF), rgb(0x000000), rgb(0x1A1A1A), rgb(0x000000),
                rgb(0x0037B3), rgb(0xFFFFFF), rgb(0xC9DAF8), rgb(0xB4009E), rgb(0xA00000), rgb(0x6B4500), rgb(0x005A00),
                rgb(0x6E6E6E)};
    case Code::HighContrastBlack:
        return {rgb(0x000000), rgb(0x080808), rgb(0x151515), rgb(0x000000), rgb(0xFFFFFF), rgb(0xEEEEEE), rgb(0xFFFFFF),
                rgb(0xFFFF00), rgb(0x000000), rgb(0x253F60), rgb(0x00FFFF), rgb(0xFFADAD), rgb(0xFFD54A), rgb(0x7CFC7C),
                rgb(0x8C8C8C)};
    }
    return {};
}

const char *const kPickSettings[2][3] = {
    {"appearance.highContrastWhite.accent", "appearance.highContrastWhite.text", "appearance.highContrastWhite.border"},
    {"appearance.highContrastBlack.accent", "appearance.highContrastBlack.text", "appearance.highContrastBlack.border"},
};

int hcIndex(Code code)
{
    return code == Code::HighContrastBlack ? 1 : 0;
}

QColor readColour(const QVariant &value)
{
    const auto parsed = application::parseSettingColour(value.toString().toStdString());
    if (!parsed)
        return {};
    return QColor::fromRgb(QRgb(*parsed | 0xFF000000u)); // drawn opaque
}

} // namespace

QString codeName(Code code)
{
    switch (code) {
    case Code::Light:
        return QStringLiteral("light");
    case Code::Dark:
        return QStringLiteral("dark");
    case Code::HighContrastWhite:
        return QStringLiteral("highContrastWhite");
    case Code::HighContrastBlack:
        return QStringLiteral("highContrastBlack");
    }
    return QStringLiteral("dark");
}

std::optional<Code> codeFromName(const QString &name)
{
    for (const auto code : kCodes)
        if (codeName(code) == name)
            return code;
    return std::nullopt;
}

bool isDark(Code code)
{
    return code == Code::Dark || code == Code::HighContrastBlack;
}

bool isHighContrast(Code code)
{
    return code == Code::HighContrastWhite || code == Code::HighContrastBlack;
}

Code withDark(Code code, bool dark)
{
    if (isHighContrast(code))
        return dark ? Code::HighContrastBlack : Code::HighContrastWhite;
    return dark ? Code::Dark : Code::Light;
}

QColor role(const Roles &r, std::size_t index)
{
    const QColor Roles::*members[] = {&Roles::background, &Roles::panel,  &Roles::raised,   &Roles::field,
                                      &Roles::text,       &Roles::muted,  &Roles::line,     &Roles::accent,
                                      &Roles::onAccent,   &Roles::select, &Roles::focus,    &Roles::danger,
                                      &Roles::warning,    &Roles::success, &Roles::disabled};
    static_assert(std::size(members) == kRoleNames.size());
    return index < std::size(members) ? r.*members[index] : QColor();
}

std::span<const Accent> accents(bool dark)
{
    return dark ? std::span<const Accent>(kDarkAccents) : std::span<const Accent>(kLightAccents);
}

const Accent &accent(bool dark, const QString &key)
{
    const auto list = accents(dark);
    for (const auto &a : list)
        if (key == QLatin1String(a.key))
            return a;
    for (const auto &a : list)
        if (QLatin1String(a.key) == QLatin1String(kDefaultAccent))
            return a;
    return list.front();
}

const char *pickSetting(Code code, Pick pick)
{
    return kPickSettings[hcIndex(code)][static_cast<int>(pick)];
}

QColor pickDefault(Code code, Pick pick)
{
    const Roles base = baseRoles(isHighContrast(code) ? code : Code::HighContrastWhite);
    switch (pick) {
    case Pick::Accent:
        return base.accent;
    case Pick::Text:
        return base.text;
    case Pick::Border:
        return base.line;
    }
    return {};
}

Choice choiceFrom(const std::function<QVariant(const char *id)> &value)
{
    Choice choice;
    if (const auto code = codeFromName(value(kThemeSetting).toString()))
        choice.theme = *code;
    const QVariant follow = value(kFollowSystemSetting);
    if (follow.isValid())
        choice.followSystem = follow.toBool();
    choice.lightAccent = accent(false, value(kLightAccentSetting).toString()).key;
    choice.darkAccent = accent(true, value(kDarkAccentSetting).toString()).key;
    for (const auto code : {Code::HighContrastWhite, Code::HighContrastBlack})
        for (const auto pick : kPicks) {
            const QColor picked = readColour(value(pickSetting(code, pick)));
            choice.picks[hcIndex(code)][static_cast<int>(pick)] = picked.isValid() ? picked : pickDefault(code, pick);
        }
    return choice;
}

Code effective(const Choice &choice, Qt::ColorScheme system)
{
    if (!choice.followSystem || system == Qt::ColorScheme::Unknown)
        return choice.theme;
    return withDark(choice.theme, system == Qt::ColorScheme::Dark);
}

Roles resolve(Code code, const Choice &choice)
{
    Roles roles = baseRoles(code);
    if (isHighContrast(code)) {
        const auto &picks = choice.picks[hcIndex(code)];
        if (picks[0].isValid())
            roles.accent = picks[0];
        if (picks[1].isValid())
            roles.text = picks[1];
        if (picks[2].isValid())
            roles.line = picks[2];
        // Text on the accent: the theme's, or the other of black and white
        // when a picked accent leaves it unreadable.
        if (contrastRatio(roles.onAccent, roles.accent) < 4.5) {
            const QColor other = roles.onAccent == QColor(Qt::black) ? QColor(Qt::white) : QColor(Qt::black);
            if (contrastRatio(other, roles.accent) > contrastRatio(roles.onAccent, roles.accent))
                roles.onAccent = other;
        }
        return roles;
    }
    const bool dark = isDark(code);
    const Accent &a = accent(dark, dark ? choice.darkAccent : choice.lightAccent);
    roles.accent = a.accent;
    roles.onAccent = a.onAccent;
    roles.select = a.select;
    roles.focus = a.focus;
    return roles;
}

QPalette palette(const Roles &r)
{
    QPalette p;
    // The palette's derived shades (Light, Midlight, Mid, Dark, Shadow) for
    // the styles' bevels and separators: the boundary for Mid and Dark.
    const QColor lighter = isDarkColour(r.panel) ? r.raised.lighter(115) : r.field;
    for (const auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        const bool off = group == QPalette::Disabled;
        const QColor ink = off ? r.disabled : r.text;
        p.setColor(group, QPalette::Window, r.panel);
        p.setColor(group, QPalette::WindowText, ink);
        p.setColor(group, QPalette::Base, r.field);
        p.setColor(group, QPalette::AlternateBase, r.raised);
        p.setColor(group, QPalette::ToolTipBase, r.panel);
        p.setColor(group, QPalette::ToolTipText, r.text);
        p.setColor(group, QPalette::PlaceholderText, off ? r.disabled : r.muted);
        p.setColor(group, QPalette::Text, ink);
        p.setColor(group, QPalette::Button, r.raised);
        p.setColor(group, QPalette::ButtonText, ink);
        p.setColor(group, QPalette::BrightText, r.onAccent);
        p.setColor(group, QPalette::Light, lighter);
        p.setColor(group, QPalette::Midlight, r.raised);
        p.setColor(group, QPalette::Mid, r.line);
        p.setColor(group, QPalette::Dark, r.line);
        p.setColor(group, QPalette::Shadow, isDarkColour(r.panel) ? QColor(Qt::black) : r.line);
        p.setColor(group, QPalette::Highlight, off ? r.disabled : r.accent);
        p.setColor(group, QPalette::HighlightedText, r.onAccent);
        p.setColor(group, QPalette::Accent, off ? r.disabled : r.accent);
        p.setColor(group, QPalette::Link, r.accent);
        p.setColor(group, QPalette::LinkVisited, r.accent);
    }
    return p;
}

Content content(Code code, const Roles &roles)
{
    Content c;
    application::AudioDisplayOptions &a = c.audio;
    // Selection-type marks take the accent: the selection's background
    // (AUDIO_SELECTION_BACKGROUND and _MODIFIED, legacy's 0x37 alpha) and the
    // waveform inside it (AUDIO_WAVEFORM_SELECTED).
    QColor selection = roles.accent;
    selection.setAlpha(0x37);
    switch (code) {
    case Code::Dark:
        // Legacy's dark theme (config.cpp:451-472, LoadDefaultColors(true)),
        // the AudioDisplayOptions defaults.
        c.spellcheck = rgb(0x940000); // GRID_SPELLCHECKER / EDITOR_SPELLCHECKER (config.cpp:432, 449)
        c.gridAlternate = rgb(0x242A31);
        c.gridWarning = rgb(0x7A2E2E);
        // GRID_COMPARISON_* (config.cpp:427-431)
        c.comparison = {rgb(0x2700FF), rgb(0x272B32), rgb(0x3A3E45), rgb(0x003176), rgb(0x3662A1)};
        break;
    case Code::Light:
        c.spellcheck = rgb(0xFF6968); // legacy's light theme (config.cpp:432, 449)
        c.gridAlternate = rgb(0xF0F2F4);
        c.gridWarning = rgb(0xF4C7C7);
        // legacy's light theme (config.cpp:427-431, LoadDefaultColors(false))
        c.comparison = {rgb(0xFFFFFF), rgb(0xFF000C), rgb(0xB7AC00), rgb(0x9C0000), rgb(0x817900)};
        a.background = 0xFFDDE3E8;
        a.lineStart = a.lineEnd = 0xFF940000;
        a.inactiveBoundary = 0xFF00875A;
        a.cursor = 0xFF4F56D6;
        a.secondBoundaries = 0x37202832;
        a.keyframe = 0xFF2E3A46;
        a.inactiveBackground = 0x18000000;
        a.waveform = 0xFF5B6774;
        a.waveformInactive = 0xFF9AA5B0;
        a.waveformModified = 0xFFC62828;
        a.timescaleBackground = 0xFFEDF0F3;
        a.timescaleText = 0xFF526170;
        a.spectrumBackground = 0xFFFFFFFF;
        a.spectrumEcho = 0xFF674FD7;
        a.spectrumInner = 0xFF1A1F26;
        a.lineBoundaryMark = 0xFF202832;
        a.syllableBoundaries = 0xFF3A4652;
        a.syllableText = 0xFFE0E3FF;
        break;
    case Code::HighContrastWhite:
        c.spellcheck = rgb(0xFF9C9C);
        c.gridAlternate = rgb(0xFFFFFF);
        c.gridWarning = rgb(0xFFC0C0);
        c.comparison = {rgb(0xC000C0), rgb(0xFFD0D0), rgb(0xE0E0E0), rgb(0xD0D0FF), rgb(0xD0E8FF)};
        a.background = 0xFFFFFFFF;
        a.lineStart = a.lineEnd = 0xFFC00000;
        a.inactiveBoundary = 0xFF007A3D;
        a.cursor = 0xFF8000C0;
        a.secondBoundaries = 0x60000000;
        a.keyframe = 0xFF000000;
        a.inactiveBackground = 0x22000000;
        a.waveform = 0xFF303030;
        a.waveformInactive = 0xFF8C8C8C;
        a.waveformModified = 0xFFC00000;
        a.timescaleBackground = 0xFFFFFFFF;
        a.timescaleText = 0xFF000000;
        a.spectrumBackground = 0xFFFFFFFF;
        a.spectrumEcho = 0xFF0050C8;
        a.spectrumInner = 0xFF000000;
        a.lineBoundaryMark = 0xFF000000;
        a.syllableBoundaries = 0xFF000000;
        a.syllableText = 0xFFFFFFFF;
        break;
    case Code::HighContrastBlack:
        c.spellcheck = rgb(0xC00000);
        c.gridAlternate = rgb(0x000000);
        c.gridWarning = rgb(0x800000);
        c.comparison = {rgb(0xFF00FF), rgb(0x3A0000), rgb(0x303030), rgb(0x000060), rgb(0x003060)};
        a.background = 0xFF000000;
        a.lineStart = a.lineEnd = 0xFFFF4040;
        a.inactiveBoundary = 0xFF00FF80;
        a.cursor = 0xFF00FFFF;
        a.secondBoundaries = 0x60FFFFFF;
        a.keyframe = 0xFFFFFFFF;
        a.inactiveBackground = 0x55000000;
        a.waveform = 0xFFC8C8C8;
        a.waveformInactive = 0xFF6E6E6E;
        a.waveformModified = 0xFFFF6968;
        a.timescaleBackground = 0xFF000000;
        a.timescaleText = 0xFFFFFFFF;
        a.spectrumBackground = 0xFF000000;
        a.spectrumEcho = 0xFF00C8FF;
        a.spectrumInner = 0xFFFFFFFF;
        a.lineBoundaryMark = 0xFFFFFFFF;
        a.syllableBoundaries = 0xFFFFFFFF;
        a.syllableText = 0xFF000000;
        break;
    }
    a.selectionBackground = a.selectionModified = toArgb(selection);
    a.waveformSelected = toArgb(roles.accent);
    // GRID_SELECTION (legacy #8791FD at alpha 75, config.cpp:422) over a
    // compared row: the accent at that alpha.
    c.comparisonSelection = roles.accent;
    c.comparisonSelection.setAlpha(75);
    return c;
}

void applyAudioColours(application::AudioDisplayOptions &o, const application::AudioDisplayOptions &c)
{
    o.background = c.background;
    o.lineStart = c.lineStart;
    o.lineEnd = c.lineEnd;
    o.inactiveBoundary = c.inactiveBoundary;
    o.cursor = c.cursor;
    o.secondBoundaries = c.secondBoundaries;
    o.keyframe = c.keyframe;
    o.selectionBackground = c.selectionBackground;
    o.selectionModified = c.selectionModified;
    o.inactiveBackground = c.inactiveBackground;
    o.waveform = c.waveform;
    o.waveformInactive = c.waveformInactive;
    o.waveformModified = c.waveformModified;
    o.waveformSelected = c.waveformSelected;
    o.timescaleBackground = c.timescaleBackground;
    o.timescaleText = c.timescaleText;
    o.spectrumBackground = c.spectrumBackground;
    o.spectrumEcho = c.spectrumEcho;
    o.spectrumInner = c.spectrumInner;
    o.lineBoundaryMark = c.lineBoundaryMark;
    o.syllableBoundaries = c.syllableBoundaries;
    o.syllableText = c.syllableText;
}

double contrastRatio(const QColor &a, const QColor &b)
{
    const auto luminance = [](const QColor &c) {
        const auto channel = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
        return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF()) + 0.0722 * channel(c.blueF());
    };
    const double x = luminance(a), y = luminance(b);
    return (std::max(x, y) + 0.05) / (std::min(x, y) + 0.05);
}

QColor composite(const QColor &over, const QColor &under)
{
    const double a = over.alphaF();
    return QColor::fromRgbF(float(over.redF() * a + under.redF() * (1 - a)), float(over.greenF() * a + under.greenF() * (1 - a)),
                            float(over.blueF() * a + under.blueF() * (1 - a)));
}

namespace {

// What every Theme singleton, the icons and the owner-drawn items follow:
// the profile's choice (or the Options dialog's preview of one) and the
// platform's colour scheme.
class Hub : public QObject {
    Q_OBJECT
public:
    static Hub &get()
    {
        static QPointer<Hub> hub;
        if (!hub)
            hub = new Hub(QCoreApplication::instance());
        return *hub;
    }

    State state;
    QPointer<SettingsStore> settings;
    std::optional<QVariantMap> previewValues;
    std::optional<Qt::ColorScheme> forcedScheme;

    void use(SettingsStore *store)
    {
        if (settings)
            disconnect(settings, nullptr, this, nullptr);
        settings = store;
        if (store) {
            connect(store, &SettingsStore::changed, this, [this](const QString &id) {
                if (id.startsWith(QLatin1String("appearance.")))
                    refresh();
            });
            connect(store, &QObject::destroyed, this, [this] { refresh(); });
        }
        refresh(true);
    }

    Qt::ColorScheme scheme() const
    {
        if (forcedScheme)
            return *forcedScheme;
        const auto *hints = QGuiApplication::styleHints();
        return hints ? hints->colorScheme() : Qt::ColorScheme::Unknown;
    }

    void refresh(bool force = false)
    {
        const auto value = [this](const char *id) -> QVariant {
            const QString key = QLatin1String(id);
            if (previewValues) {
                const auto found = previewValues->constFind(key);
                if (found != previewValues->cend())
                    return *found;
            }
            return settings ? settings->value(key) : QVariant();
        };
        State next;
        next.choice = choiceFrom(value);
        next.code = effective(next.choice, scheme());
        next.roles = resolve(next.code, next.choice);
        next.content = content(next.code, next.roles);
        const bool same = next.code == state.code && next.choice == state.choice && next.roles == state.roles;
        state = std::move(next);
        // With a profile the controls draw with the theme's palette.
        if (settings && (force || !same))
            QGuiApplication::setPalette(palette(state.roles));
        if (force || !same)
            emit changed();
    }

signals:
    void changed();

private:
    explicit Hub(QObject *parent) : QObject(parent)
    {
        state.code = effective(state.choice, Qt::ColorScheme::Unknown);
        state.roles = resolve(state.code, state.choice);
        state.content = content(state.code, state.roles);
        if (auto *hints = QGuiApplication::styleHints())
            connect(hints, &QStyleHints::colorSchemeChanged, this, [this] { refresh(); });
        refresh();
    }
};

} // namespace

const State &current()
{
    return Hub::get().state;
}

void onChanged(QObject *context, std::function<void()> slot)
{
    QObject::connect(&Hub::get(), &Hub::changed, context, std::move(slot));
}

void useSettings(SettingsStore *settings)
{
    Hub::get().use(settings);
}

void preview(const QVariantMap &values)
{
    Hub::get().previewValues = values;
    Hub::get().refresh();
}

void endPreview()
{
    if (!Hub::get().previewValues)
        return;
    Hub::get().previewValues.reset();
    Hub::get().refresh();
}

Qt::ColorScheme systemScheme()
{
    return Hub::get().scheme();
}

void forceSystemScheme(std::optional<Qt::ColorScheme> scheme)
{
    Hub::get().forcedScheme = scheme;
    Hub::get().refresh();
}

void chooseControlsStyle()
{
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE"))
        QQuickStyle::setStyle(QStringLiteral("Fusion"));
}

} // namespace theme

Theme::Theme(QObject *parent) : QObject(parent)
{
    theme::onChanged(this, [this] { emit changed(); });
}

Theme *Theme::create(QQmlEngine *engine, QJSEngine *)
{
    return new Theme(engine);
}

QString Theme::accentKey() const
{
    const auto &s = theme::current();
    if (theme::isHighContrast(s.code))
        return {};
    return theme::isDark(s.code) ? s.choice.darkAccent : s.choice.lightAccent;
}

QVariantList Theme::themes() const
{
    const char *names[] = {QT_TR_NOOP("Light"), QT_TR_NOOP("Dark"), QT_TR_NOOP("High contrast white"),
                           QT_TR_NOOP("High contrast black")};
    QVariantList out;
    for (const auto code : theme::kCodes)
        out << QVariantMap{{QStringLiteral("code"), theme::codeName(code)},
                           {QStringLiteral("name"), tr(names[static_cast<int>(code)])},
                           {QStringLiteral("dark"), theme::isDark(code)},
                           {QStringLiteral("highContrast"), theme::isHighContrast(code)}};
    return out;
}

QVariantList Theme::accents(bool dark) const
{
    const auto surfaces = [dark] {
        const theme::Roles r = theme::resolve(dark ? theme::Code::Dark : theme::Code::Light, {});
        return std::array{r.background, r.panel, r.raised, r.field};
    }();
    QVariantList out;
    for (const auto &a : theme::accents(dark)) {
        double lowest = 21;
        for (const QColor &s : surfaces)
            lowest = std::min(lowest, theme::contrastRatio(a.accent, s));
        out << QVariantMap{{QStringLiteral("key"), QString::fromLatin1(a.key)},
                           {QStringLiteral("name"), QCoreApplication::translate("Theme", a.name)},
                           {QStringLiteral("accent"), a.accent},
                           {QStringLiteral("onAccent"), a.onAccent},
                           {QStringLiteral("contrast"), lowest}};
    }
    return out;
}

QVariantList Theme::picks(const QString &code) const
{
    const auto c = theme::codeFromName(code);
    if (!c || !theme::isHighContrast(*c))
        return {};
    const char *names[] = {QT_TR_NOOP("Accent color"), QT_TR_NOOP("Text and icons"), QT_TR_NOOP("Border color")};
    QVariantList out;
    for (const auto pick : theme::kPicks)
        out << QVariantMap{{QStringLiteral("setting"), QString::fromLatin1(theme::pickSetting(*c, pick))},
                           {QStringLiteral("name"), tr(names[static_cast<int>(pick)])},
                           {QStringLiteral("defaultColour"), theme::pickDefault(*c, pick).name(QColor::HexRgb).toUpper()}};
    return out;
}

QVariantMap Theme::rolesOf(const QString &code, const QVariantMap &values) const
{
    const auto c = theme::codeFromName(code);
    if (!c)
        return {};
    const auto choice = theme::choiceFrom([&values](const char *id) { return values.value(QLatin1String(id)); });
    const theme::Roles roles = theme::resolve(*c, choice);
    QVariantMap out;
    for (std::size_t i = 0; i < theme::kRoleNames.size(); ++i)
        out.insert(QLatin1String(theme::kRoleNames[i]), theme::role(roles, i));
    return out;
}

bool Theme::systemSchemeKnown() const
{
    return theme::systemScheme() != Qt::ColorScheme::Unknown;
}

} // namespace hikari::ui

#include "theme.moc"
