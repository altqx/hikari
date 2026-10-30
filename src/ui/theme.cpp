#include "theme.h"

#include "settings_store.h"
#include "style/style_colours.h"
#include "title_bars.h"

#include "hikari/application/settings.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QPlatformSurfaceEvent>
#include <QPointer>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QStyleHints>
#include <QtQml/qqmlengine.h>
#include <QtQml/qqmlextensionplugin.h>

#ifdef HIKARI_COLOUR_SCHEME_PORTAL
#include "portal_colour_scheme.h"
#endif

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

const std::array<Accent, 7> kLightAccents{{
    {"red", QT_TRANSLATE_NOOP("HikariSub.Options", "Red"), rgb(0xB3261E), rgb(0xFFFFFF), rgb(0xEBD7D6)},
    {"orange", QT_TRANSLATE_NOOP("HikariSub.Options", "Orange"), rgb(0x9C4600), rgb(0xFFFFFF), rgb(0xEBDFD6)},
    {"gold", QT_TRANSLATE_NOOP("HikariSub.Options", "Gold"), rgb(0x7A5E00), rgb(0xFFFFFF), rgb(0xEBE6D6)},
    {"green", QT_TRANSLATE_NOOP("HikariSub.Options", "Green"), rgb(0x145C4C), rgb(0xFFFFFF), rgb(0xD6EBE4)},
    {"blue", QT_TRANSLATE_NOOP("HikariSub.Options", "Blue"), rgb(0x1D5BA8), rgb(0xFFFFFF), rgb(0xD6DFEB)},
    {"purple", QT_TRANSLATE_NOOP("HikariSub.Options", "Purple"), rgb(0x5B3FB0), rgb(0xFFFFFF), rgb(0xDBD6EB)},
    {"pink", QT_TRANSLATE_NOOP("HikariSub.Options", "Pink"), rgb(0x9E2A73), rgb(0xFFFFFF), rgb(0xEBD6E3)},
}};
const std::array<Accent, 7> kDarkAccents{{
    {"red", QT_TRANSLATE_NOOP("HikariSub.Options", "Red"), rgb(0xF4A9A3), rgb(0x2C1210), rgb(0x4C3230)},
    {"orange", QT_TRANSLATE_NOOP("HikariSub.Options", "Orange"), rgb(0xF5BB8A), rgb(0x2C1D10), rgb(0x4C3D30)},
    {"gold", QT_TRANSLATE_NOOP("HikariSub.Options", "Gold"), rgb(0xE3CF7E), rgb(0x2C2610), rgb(0x4C4630)},
    {"green", QT_TRANSLATE_NOOP("HikariSub.Options", "Green"), rgb(0x9CDBC9), rgb(0x102C24), rgb(0x304C47)},
    {"blue", QT_TRANSLATE_NOOP("HikariSub.Options", "Blue"), rgb(0xA6C8F2), rgb(0x101D2C), rgb(0x303D4C)},
    {"purple", QT_TRANSLATE_NOOP("HikariSub.Options", "Purple"), rgb(0xC6B5F4), rgb(0x18102C), rgb(0x38304C)},
    {"pink", QT_TRANSLATE_NOOP("HikariSub.Options", "Pink"), rgb(0xEFAAD3), rgb(0x2C1021), rgb(0x4C3041)},
}};

Roles baseRoles(Code code)
{
    switch (code) {
    case Code::Light:
        return {rgb(0xE5E9EC), rgb(0xF9FAFB), rgb(0xEDF0F3), rgb(0xFFFFFF), rgb(0x202832), rgb(0x526170), rgb(0xAAB5BE),
                {}, {}, {}, {}, rgb(0xA51F31), rgb(0x8A5A00), rgb(0x008000), rgb(0x74808B), rgb(0x76828D)};
    case Code::Dark:
        return {rgb(0x171B20), rgb(0x20262D), rgb(0x29313A), rgb(0x171D24), rgb(0xE8EDF2), rgb(0xA5B1BD), rgb(0x414B57),
                {}, {}, {}, {}, rgb(0xFFADAD), rgb(0xE0A030), rgb(0x4DBA4D), rgb(0x75818D), rgb(0x7A8691)};
    case Code::HighContrastWhite:
        return {rgb(0xFFFFFF), rgb(0xFFFFFF), rgb(0xEBEBEB), rgb(0xFFFFFF), rgb(0x000000), rgb(0x1A1A1A), rgb(0x000000),
                rgb(0x0037B3), rgb(0xFFFFFF), rgb(0xC9DAF8), {}, rgb(0xA00000), rgb(0x6B4500), rgb(0x005A00),
                rgb(0x6E6E6E), rgb(0x000000)};
    case Code::HighContrastBlack:
        return {rgb(0x000000), rgb(0x080808), rgb(0x151515), rgb(0x000000), rgb(0xFFFFFF), rgb(0xEEEEEE), rgb(0xFFFFFF),
                rgb(0xFFFF00), rgb(0x000000), rgb(0x253F60), {}, rgb(0xFFADAD), rgb(0xFFD54A), rgb(0x7CFC7C),
                rgb(0x8C8C8C), rgb(0xFFFFFF)};
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
    return QColor::fromRgb(QRgb(*parsed | 0xFF000000u));
}

}

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
                                      &Roles::warning,    &Roles::success, &Roles::disabled, &Roles::control};
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

namespace {

Roles withFocus(Roles roles)
{
    roles.focus = roles.text;
    return roles;
}

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
        roles.control = roles.line;
        if (contrastRatio(roles.onAccent, roles.accent) < 4.5) {
            const QColor other = roles.onAccent == QColor(Qt::black) ? QColor(Qt::white) : QColor(Qt::black);
            if (contrastRatio(other, roles.accent) > contrastRatio(roles.onAccent, roles.accent))
                roles.onAccent = other;
        }
        return withFocus(roles);
    }
    const bool dark = isDark(code);
    const Accent &a = accent(dark, dark ? choice.darkAccent : choice.lightAccent);
    roles.accent = a.accent;
    roles.onAccent = a.onAccent;
    roles.select = a.select;
    return withFocus(roles);
}

QPalette palette(const Roles &r)
{
    QPalette p;
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
    QColor selection = roles.accent;
    selection.setAlpha(0x37);
    switch (code) {
    case Code::Dark:
        c.spellcheck = rgb(0xFF6B6B);
        c.grammar = rgb(0x6EA8FE);
        c.gridAlternate = rgb(0x242A31);
        c.gridWarning = rgb(0x7A2E2E);
        c.gridComment = rgb(0x253A57);
        c.comparison = {rgb(0x2700FF), rgb(0x272B32), rgb(0x3A3E45), rgb(0x003176), rgb(0x3662A1)};
        c.syntax = {QColor(), rgb(0x8791FD), rgb(0x00C3FF), rgb(0x00C3FF), rgb(0x0076FF), rgb(0xADABAB),
                    rgb(0xBDB76B), rgb(0xFB4544), rgb(0x00C3FF), rgb(0x8D80D3), rgb(0x11A243)};
        break;
    case Code::Light:
        c.spellcheck = rgb(0xD70000);
        c.grammar = rgb(0x1A56DB);
        c.gridAlternate = rgb(0xF0F2F4);
        c.gridWarning = rgb(0xF4C7C7);
        c.gridComment = rgb(0xD6E3F5);
        c.comparison = {rgb(0xFFFFFF), rgb(0xFF000C), rgb(0xB7AC00), rgb(0x9C0000), rgb(0x817900)};
        c.syntax = {QColor(), rgb(0x7E3B00), rgb(0x787600), rgb(0x787600), rgb(0x34C200), rgb(0x817F7F),
                    rgb(0xBDB76B), rgb(0xFB4544), rgb(0x00C3FF), rgb(0x8D80D3), rgb(0x11A243)};
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
        c.spellcheck = rgb(0xC00000);
        c.grammar = rgb(0x0000C8);
        c.gridAlternate = rgb(0xFFFFFF);
        c.gridWarning = rgb(0xFFC0C0);
        c.gridComment = rgb(0xF2E2B0);
        c.comparison = {rgb(0xC000C0), rgb(0xFFD0D0), rgb(0xE0E0E0), rgb(0xD0D0FF), rgb(0xD0E8FF)};
        c.syntax = {QColor(), rgb(0x7A3E00), rgb(0x005A9E), rgb(0x005A9E), rgb(0x006400), rgb(0x595959),
                    rgb(0x6B5E00), rgb(0xB00000), rgb(0x005A9E), rgb(0x5B2DA8), rgb(0x00663A)};
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
        c.spellcheck = rgb(0xFF6060);
        c.grammar = rgb(0xFFFF00);
        c.gridAlternate = rgb(0x000000);
        c.gridWarning = rgb(0x800000);
        c.gridComment = rgb(0x403400);
        c.comparison = {rgb(0xFF00FF), rgb(0x3A0000), rgb(0x303030), rgb(0x000060), rgb(0x003060)};
        c.syntax = {QColor(), rgb(0xFFB060), rgb(0x60D0FF), rgb(0x60D0FF), rgb(0x7CFC7C), rgb(0xBEBEBE),
                    rgb(0xF0E68C), rgb(0xFF6060), rgb(0x60D0FF), rgb(0xC8A8FF), rgb(0xA0E0A0)};
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
    c.syntax[0] = roles.text;
    a.selectionBackground = a.selectionModified = toArgb(selection);
    a.waveformSelected = toArgb(roles.accent);
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

    // Qt reports no scheme on Linux desktops other than GNOME and KDE; the XDG settings portal's is used then.
    Qt::ColorScheme scheme() const
    {
        if (forcedScheme)
            return *forcedScheme;
        const auto *hints = QGuiApplication::styleHints();
        Qt::ColorScheme platform = hints ? hints->colorScheme() : Qt::ColorScheme::Unknown;
#ifdef HIKARI_COLOUR_SCHEME_PORTAL
        if (platformOverride)
            platform = *platformOverride;
        if (platform == Qt::ColorScheme::Unknown && portal)
            return portal->scheme();
#endif
        return platform;
    }

#ifdef HIKARI_COLOUR_SCHEME_PORTAL
    void followPortal(PortalColourScheme *next)
    {
        if (portal) {
            disconnect(portal, nullptr, this, nullptr);
            if (portal != next && portal->parent() == this)
                portal->deleteLater();
        }
        portal = next;
        if (next) {
            connect(next, &PortalColourScheme::changed, this, [this] { refresh(); });
            connect(next, &QObject::destroyed, this, [this] { refresh(); });
        }
        refresh();
    }

    QPointer<PortalColourScheme> portal;
    std::optional<Qt::ColorScheme> platformOverride;
#endif

    void refresh(bool force = false)
    {
        // QStyleHints::setColorScheme can emit colorSchemeChanged at once, re-entering here.
        if (refreshing)
            return;
        refreshing = true;
        const auto done = qScopeGuard([this] { refreshing = false; });
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
        if (next.choice.followSystem)
            requestScheme(Qt::ColorScheme::Unknown);
        next.code = effective(next.choice, scheme());
        next.roles = resolve(next.code, next.choice);
        next.content = content(next.code, next.roles);
        const bool same = next.code == state.code && next.choice == state.choice && next.roles == state.roles;
        state = std::move(next);
        if (settings && (force || !same))
            QGuiApplication::setPalette(palette(state.roles));
        if (!state.choice.followSystem)
            requestScheme(isDark(state.code) ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
        style::StyleColours::setFocus(settings ? state.roles.focus : QColor());
        style::StyleColours::setControl(settings ? state.roles.control : QColor());
        // Qt resets the title bars from its own scheme on every system change.
        themeTitleBars(QGuiApplication::topLevelWindows());
        if (force || !same)
            emit changed();
    }

    void themeTitleBars(const QList<QWindow *> &windows) const
    {
        const bool dark = isDark(state.code);
        for (QWindow *window : windows)
            if (titlebars::themed(window))
                titlebars::setDark(window, dark);
    }

    bool eventFilter(QObject *object, QEvent *event) override
    {
        const auto type = event->type();
        if (type != QEvent::PlatformSurface && type != QEvent::Show && type != QEvent::ApplicationPaletteChange)
            return false;
        auto *window = qobject_cast<QWindow *>(object);
        if (!window || !titlebars::themed(window))
            return false;
        if (type == QEvent::PlatformSurface) {
            if (static_cast<QPlatformSurfaceEvent *>(event)->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated)
                themeTitleBars({window});
        } else if (type == QEvent::Show) {
            themeTitleBars({window});
        } else {
            // Qt sets the frame after the palette-change event's filters run.
            QMetaObject::invokeMethod(
                this, [this, w = QPointer<QWindow>(window)] {
                    if (w)
                        themeTitleBars({w.data()});
                },
                Qt::QueuedConnection);
        }
        return false;
    }

    // Windows only: Qt draws title bars from the requested scheme (elsewhere it restyles the whole platform theme); each request makes Qt refresh every palette.
    void requestScheme(Qt::ColorScheme scheme)
    {
#ifdef Q_OS_WIN
        auto *hints = QGuiApplication::styleHints();
        if (!hints)
            return;
        if (requested == scheme && (scheme == Qt::ColorScheme::Unknown || hints->colorScheme() == scheme))
            return;
        requested = scheme;
        hints->setColorScheme(scheme);
#else
        Q_UNUSED(scheme);
#endif
    }

    // A QQuickWindow creates its palette on first read from the style's defaults and then follows only application palette changes.
    void adopt(const QList<QWindow *> &windows, bool again = false)
    {
        bool added = again;
        for (QWindow *window : windows) {
            auto *quick = qobject_cast<QQuickWindow *>(window);
            if (!quick || (quick->property(kAdopted).toBool() && !again))
                continue;
            quick->setProperty(kAdopted, true);
            quick->property("palette");
            added = true;
        }
        if (added && settings) {
            QGuiApplication::setPalette(QPalette());
            QGuiApplication::setPalette(palette(state.roles));
        }
        if (added)
            themeTitleBars(windows);
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
#ifdef HIKARI_COLOUR_SCHEME_PORTAL
        const QString platform = QGuiApplication::platformName();
        if (platform == QLatin1String("xcb") || platform.startsWith(QLatin1String("wayland")))
            portal = new PortalColourScheme(QDBusConnection::sessionBus(), this);
        if (portal)
            connect(portal, &PortalColourScheme::changed, this, [this] { refresh(); });
#endif
        if (auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
            connect(app, &QGuiApplication::focusWindowChanged, this, [this](QWindow *window) {
                if (window)
                    adopt({window});
            });
            app->installEventFilter(this);
        }
        refresh();
    }

    static constexpr const char *kAdopted = "hikariThemePalette";
    bool refreshing = false;
    std::optional<Qt::ColorScheme> requested;
};

}

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

namespace {

void repaletteAfterLoad()
{
    QMetaObject::invokeMethod(
        &Hub::get(), [] { Hub::get().adopt(QGuiApplication::topLevelWindows(), true); }, Qt::QueuedConnection);
}

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

#ifdef HIKARI_COLOUR_SCHEME_PORTAL
void followPortal(PortalColourScheme *portal)
{
    Hub::get().followPortal(portal);
}

void forcePlatformScheme(std::optional<Qt::ColorScheme> scheme)
{
    Hub::get().platformOverride = scheme;
    Hub::get().refresh();
}
#endif

void chooseControlsStyle()
{
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        QQuickStyle::setStyle(QString::fromLatin1(kControlsStyle));
        QQuickStyle::setFallbackStyle(QStringLiteral("Fusion"));
    }
}

}

Theme::Theme(QObject *parent) : QObject(parent)
{
    theme::onChanged(this, [this] { emit changed(); });
    theme::repaletteAfterLoad();
}

Theme *Theme::create(QQmlEngine *engine, QJSEngine *)
{
    return new Theme(engine);
}

QVariantList Theme::syntax() const
{
    QVariantList out;
    for (const QColor &c : theme::current().content.syntax)
        out << c;
    return out;
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
    const char *names[] = {
        QT_TRANSLATE_NOOP("HikariSub.Options", "Light"),
        QT_TRANSLATE_NOOP("HikariSub.Options", "Dark"),
        QT_TRANSLATE_NOOP("HikariSub.Options", "High contrast white"),
        QT_TRANSLATE_NOOP("HikariSub.Options", "High contrast black"),
    };
    QVariantList out;
    for (const auto code : theme::kCodes)
        out << QVariantMap{{QStringLiteral("code"), theme::codeName(code)},
                           {QStringLiteral("name"), QCoreApplication::translate("HikariSub.Options", names[static_cast<int>(code)])},
                           {QStringLiteral("source"), QString::fromLatin1(names[static_cast<int>(code)])},
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
                           {QStringLiteral("name"), QCoreApplication::translate("HikariSub.Options", a.name)},
                           {QStringLiteral("source"), QString::fromLatin1(a.name)},
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
    const char *names[] = {
        QT_TRANSLATE_NOOP("HikariSub.Options", "Accent color"),
        QT_TRANSLATE_NOOP("HikariSub.Options", "Text and icons"),
        QT_TRANSLATE_NOOP("HikariSub.Options", "Border color"),
    };
    QVariantList out;
    for (const auto pick : theme::kPicks)
        out << QVariantMap{{QStringLiteral("setting"), QString::fromLatin1(theme::pickSetting(*c, pick))},
                           {QStringLiteral("name"), QCoreApplication::translate("HikariSub.Options", names[static_cast<int>(pick)])},
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

}

#include "theme.moc"

Q_IMPORT_QML_PLUGIN(HikariStylePlugin)
