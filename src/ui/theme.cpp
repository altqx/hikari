#include "theme.h"

#include "icon_theme.h"
#include "settings_store.h"
#include "menu_key.h"
#include "style/style_colours.h"
#include "style/text_menus.h"
#include "title_bars.h"

#include "hikari/application/settings.h"

#include <QCoreApplication>
#include <QFile>
#include <QFont>
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

std::string_view fallbackDark();

namespace {

QColor qcolour(themes::Colour c)
{
    return QColor::fromRgba(QRgb(c));
}

bool isDarkColour(const QColor &colour)
{
    return colour.lightnessF() < 0.5;
}

const std::array<const char *, 4> kBuiltinTitles{
    QT_TRANSLATE_NOOP("HikariSub.Options", "Light"),
    QT_TRANSLATE_NOOP("HikariSub.Options", "Dark"),
    QT_TRANSLATE_NOOP("HikariSub.Options", "High contrast white"),
    QT_TRANSLATE_NOOP("HikariSub.Options", "High contrast black"),
};
// The built-in accent choices' titles, for lupdate; the theme files name them.
[[maybe_unused]] const char *const kAccentTitles[] = {
    QT_TRANSLATE_NOOP("HikariSub.Options", "Red"),    QT_TRANSLATE_NOOP("HikariSub.Options", "Orange"),
    QT_TRANSLATE_NOOP("HikariSub.Options", "Gold"),   QT_TRANSLATE_NOOP("HikariSub.Options", "Green"),
    QT_TRANSLATE_NOOP("HikariSub.Options", "Blue"),   QT_TRANSLATE_NOOP("HikariSub.Options", "Purple"),
    QT_TRANSLATE_NOOP("HikariSub.Options", "Pink"),
};
const std::array<Type, 4> kBuiltinTypes{Type::Light, Type::Dark, Type::HighContrastLight, Type::HighContrastDark};

struct Registry {
    std::vector<Entry> entries;
    QStringList problems;

    Registry()
    {
        for (const auto code : kCodes) {
            const QString id = codeName(code);
            QFile file(QStringLiteral(":/hikari/ui/themes/%1.json").arg(id));
            const QByteArray json = file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
            auto loaded = themes::load(std::string_view(json.constData(), std::size_t(json.size())));
            if (loaded && loaded->type == kBuiltinTypes[std::size_t(code)]) {
                entries.push_back({id, QString::fromLatin1(kBuiltinTitles[std::size_t(code)]), {}, std::move(*loaded)});
                continue;
            }
            problems << (loaded ? QStringLiteral("%1: the type is %2").arg(id, QLatin1String(themes::typeName(loaded->type)))
                                : QStringLiteral("%1: %2 %3")
                                      .arg(id, QString::fromStdString(loaded.error().path),
                                           QString::fromStdString(loaded.error().message)));
        }
        if (!std::ranges::any_of(entries, [](const Entry &e) { return e.id == QLatin1String("dark"); })) {
            const auto dark = fallbackDark();
            auto parsed = themes::parse(std::string_view(dark.data(), dark.size()));
            entries.push_back({QStringLiteral("dark"), QString::fromLatin1(kBuiltinTitles[1]), {}, std::move(*parsed)});
        }
    }
};

Registry &registry()
{
    static Registry r;
    return r;
}

const Entry &entryOrDark(const QString &id)
{
    if (const Entry *e = find(id))
        return *e;
    return *find(QStringLiteral("dark"));
}

themes::Picks picksOf(const Choice &choice, const Entry &entry)
{
    themes::Picks picks;
    picks.accent = accentOf(choice, entry.id).toStdString();
    for (const auto role : entry.file.userColours) {
        const QColor c = colourOf(choice, entry.id, QLatin1String(themes::kRoleNames[std::size_t(role)]));
        if (c.isValid())
            picks.colours[role] = c.rgba();
    }
    return picks;
}

Roles rolesOf(const themes::Resolved &r)
{
    using R = themes::Role;
    const auto c = [&r](R role) { return qcolour(themes::at(r.roles, role)); };
    return {c(R::Background), c(R::Panel),   c(R::Raised),  c(R::Field),    c(R::Text),    c(R::Muted),
            c(R::Line),       c(R::Accent),  c(R::OnAccent), c(R::Select),  c(R::Focus),   c(R::Danger),
            c(R::Warning),    c(R::Success), c(R::Disabled), c(R::Control), c(R::Hover),   c(R::Pressed)};
}

Content contentOf(const themes::Resolved &r)
{
    const themes::Content &t = r.content;
    Content c;
    c.spellcheck = qcolour(t.spellcheck);
    c.grammar = qcolour(t.grammar);
    c.gridAlternate = qcolour(t.grid.alternate);
    c.gridWarning = qcolour(t.grid.warning);
    c.gridComment = qcolour(t.grid.comment);
    for (std::size_t i = 0; i < c.comparison.size(); ++i)
        c.comparison[i] = qcolour(t.grid.comparison[i]);
    c.comparisonSelection = qcolour(*t.grid.comparisonSelection);
    for (std::size_t i = 0; i < c.gridLabels.size(); ++i)
        c.gridLabels[i] = qcolour(t.grid.labels[i]);
    for (std::size_t i = 0; i < c.syntax.size(); ++i)
        c.syntax[i] = qcolour(*t.syntax[i]);
    for (std::size_t i = 0; i < c.ports.size(); ++i)
        c.ports[i] = qcolour(t.ports[i]);
    const themes::AudioColours &a = t.audio;
    application::AudioDisplayOptions &o = c.audio;
    o.background = a.background;
    o.lineStart = a.lineStart;
    o.lineEnd = a.lineEnd;
    o.inactiveBoundary = a.inactiveBoundary;
    o.cursor = a.cursor;
    o.secondBoundaries = a.secondBoundaries;
    o.keyframe = a.keyframe;
    o.selectionBackground = *a.selectionBackground;
    o.selectionModified = *a.selectionModified;
    o.inactiveBackground = a.inactiveBackground;
    o.waveform = a.waveform;
    o.waveformInactive = a.waveformInactive;
    o.waveformModified = a.waveformModified;
    o.waveformSelected = *a.waveformSelected;
    o.timescaleBackground = a.timescaleBackground;
    o.timescaleText = a.timescaleText;
    o.spectrumBackground = a.spectrumBackground;
    o.spectrumEcho = a.spectrumEcho;
    o.spectrumInner = a.spectrumInner;
    o.lineBoundaryMark = a.lineBoundaryMark;
    o.syllableBoundaries = a.syllableBoundaries;
    o.syllableText = a.syllableText;
    c.handle = qcolour(t.visualTools.handle);
    c.handleSelected = qcolour(t.visualTools.handleSelected);
    c.handleBorder = qcolour(t.visualTools.handleBorder);
    return c;
}

themes::Resolved resolved(const QString &themeId, const Choice &choice)
{
    const Entry &entry = entryOrDark(themeId);
    return themes::resolve(entry.file, picksOf(choice, entry));
}

QString lastValueFor(const QStringList &list, const QString &key)
{
    QString found;
    for (const QString &item : list) {
        const auto eq = item.indexOf(QLatin1Char('='));
        if (eq > 0 && QStringView(item).left(eq) == key)
            found = item.mid(eq + 1);
    }
    return found;
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

QColor role(const Roles &r, std::size_t index)
{
    const QColor Roles::*members[] = {&Roles::background, &Roles::panel,   &Roles::raised,   &Roles::field,
                                      &Roles::text,       &Roles::muted,   &Roles::line,     &Roles::accent,
                                      &Roles::onAccent,   &Roles::select,  &Roles::focus,    &Roles::danger,
                                      &Roles::warning,    &Roles::success, &Roles::disabled, &Roles::control,
                                      &Roles::hover,      &Roles::pressed};
    static_assert(std::size(members) == kRoleNames.size());
    return index < std::size(members) ? r.*members[index] : QColor();
}

const std::vector<Entry> &entries()
{
    return registry().entries;
}

QString shownTitle(const Entry &entry)
{
    const QByteArray source = entry.title.toUtf8();
    return codeFromName(entry.id) ? QCoreApplication::translate("HikariSub.Options", source.constData()) : entry.title;
}

const Entry *find(const QString &id)
{
    for (const Entry &e : registry().entries)
        if (e.id == id)
            return &e;
    return nullptr;
}

QStringList loadProblems()
{
    return registry().problems;
}

std::vector<Accent> accents(const QString &themeId)
{
    const Entry &entry = entryOrDark(themeId);
    std::vector<Accent> out;
    for (const themes::AccentChoice &a : entry.file.accents) {
        const themes::Resolved r = themes::resolve(entry.file, {a.id, {}});
        out.push_back({QString::fromStdString(a.id), QString::fromStdString(a.title), qcolour(a.accent),
                       qcolour(themes::at(r.roles, themes::Role::OnAccent)), qcolour(a.select)});
    }
    return out;
}

std::vector<Accent> accents(bool dark)
{
    return accents(codeName(dark ? Code::Dark : Code::Light));
}

Accent accent(bool dark, const QString &key)
{
    const auto list = accents(dark);
    for (const QString &k : {key, QString::fromLatin1(kDefaultAccent)})
        for (const auto &a : list)
            if (a.key == k)
                return a;
    return list.front();
}

Choice choiceFrom(const std::function<QVariant(const char *id)> &value)
{
    Choice choice;
    if (const QString id = value(kThemeSetting).toString(); find(id))
        choice.theme = id;
    const QVariant follow = value(kFollowSystemSetting);
    if (follow.isValid())
        choice.followSystem = follow.toBool();
    for (std::size_t i = 0; i < kPairSettings.size(); ++i) {
        const QString id = value(kPairSettings[i]).toString();
        if (const Entry *e = find(id); e && e->file.type == kBuiltinTypes[i])
            choice.pair[i] = id;
    }
    choice.accents = value(kAccentsSetting).toStringList();
    choice.colours = value(kColoursSetting).toStringList();
    return choice;
}

QString accentOf(const Choice &choice, const QString &themeId)
{
    return lastValueFor(choice.accents, themeId);
}

QColor colourOf(const Choice &choice, const QString &themeId, const QString &role)
{
    const QString text = lastValueFor(choice.colours, themeId + QLatin1Char('/') + role);
    const auto parsed = application::parseSettingColour(text.toStdString());
    return parsed ? QColor::fromRgba(QRgb(*parsed | 0xFF000000u)) : QColor();
}

QStringList withEntry(QStringList list, const QString &key, const QString &value)
{
    list.removeIf([&key](const QString &item) {
        const auto eq = item.indexOf(QLatin1Char('='));
        return eq > 0 && QStringView(item).left(eq) == key;
    });
    if (!value.isEmpty())
        list << key + QLatin1Char('=') + value;
    return list;
}

Type typeOf(const QString &themeId)
{
    return entryOrDark(themeId).file.type;
}

QString effective(const Choice &choice, Qt::ColorScheme system)
{
    const QString chosen = find(choice.theme) ? choice.theme : QStringLiteral("dark");
    if (!choice.followSystem || system == Qt::ColorScheme::Unknown)
        return chosen;
    const bool dark = system == Qt::ColorScheme::Dark;
    const Type side = themes::isHighContrast(typeOf(chosen)) ? (dark ? Type::HighContrastDark : Type::HighContrastLight)
                                                            : (dark ? Type::Dark : Type::Light);
    const QString &paired = choice.pair[std::size_t(side)];
    return find(paired) && typeOf(paired) == side ? paired : codeName(kCodes[std::size_t(side)]);
}

Roles resolve(const QString &themeId, const Choice &choice)
{
    return rolesOf(resolved(themeId, choice));
}

Roles resolve(Code code, const Choice &choice)
{
    return resolve(codeName(code), choice);
}

Content content(const QString &themeId, const Choice &choice)
{
    return contentOf(resolved(themeId, choice));
}

Content content(Code code, const Choice &choice)
{
    return content(codeName(code), choice);
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
    return themes::contrastRatio(a.rgba(), b.rgba());
}

QColor composite(const QColor &over, const QColor &under)
{
    const double a = over.alphaF();
    return QColor::fromRgbF(float(over.redF() * a + under.redF() * (1 - a)), float(over.greenF() * a + under.greenF() * (1 - a)),
                            float(over.blueF() * a + under.blueF() * (1 - a)));
}

namespace {

style::StyleColours::Colours styleColours(const Roles &r, Type type)
{
    return {r.focus,  r.control,  r.text,  r.muted, r.line,    r.panel,    r.raised, r.field,
            r.accent, r.onAccent, r.select, r.hover, r.pressed, r.disabled, r.danger, themes::isHighContrast(type)};
}

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
        next.id = effective(next.choice, scheme());
        next.type = typeOf(next.id);
        const themes::Resolved look = resolved(next.id, next.choice);
        next.roles = rolesOf(look);
        next.content = contentOf(look);
        const bool same = !themeListChanged && next.id == state.id && next.choice == state.choice && next.roles == state.roles;
        themeListChanged = false;
        state = std::move(next);
        if (settings && (force || !same))
            QGuiApplication::setPalette(palette(state.roles));
        if (!state.choice.followSystem)
            requestScheme(themes::isDark(state.type) ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
        style::StyleColours::set(settings ? styleColours(state.roles, state.type) : style::StyleColours::Colours{});
        // Qt resets the title bars from its own scheme on every system change.
        themeTitleBars(QGuiApplication::topLevelWindows());
        if (force || !same)
            emit changed();
    }

    void themeTitleBars(const QList<QWindow *> &windows) const
    {
        const bool dark = themes::isDark(state.type);
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
        if (type == QEvent::Show && window && window->type() == Qt::Dialog)
            if (auto *quick = qobject_cast<QQuickWindow *>(window))
                adoptOne(quick);
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

    // A dialog shown without focus never reaches adopt(); this palette change reaches only that window.
    void adoptOne(QQuickWindow *quick)
    {
        if (!settings || quick->property(kAdopted).toBool())
            return;
        quick->setProperty(kAdopted, true);
        quick->property("palette");
        QEvent change(QEvent::ApplicationPaletteChange);
        QCoreApplication::sendEvent(quick, &change);
    }

signals:
    void changed();

private:
    explicit Hub(QObject *parent) : QObject(parent)
    {
        state.id = effective(state.choice, Qt::ColorScheme::Unknown);
        state.type = typeOf(state.id);
        state.roles = resolve(state.id, state.choice);
        state.content = content(state.id, state.choice);
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

public:
    bool themeListChanged = false;
};

}

const State &current()
{
    return Hub::get().state;
}

bool currentIs(Code code)
{
    return current().id == codeName(code);
}

void install(Entry entry)
{
    auto &list = registry().entries;
    const auto it = std::ranges::find(list, entry.id, &Entry::id);
    if (it != list.end())
        *it = std::move(entry);
    else
        list.push_back(std::move(entry));
    Hub::get().themeListChanged = true;
    Hub::get().refresh();
}

void fallBackFrom(const QString &id, Type type)
{
    SettingsStore *settings = Hub::get().settings;
    if (!settings || codeFromName(id))
        return;
    if (settings->value(QString::fromLatin1(kThemeSetting)).toString() == id)
        settings->setValue(QString::fromLatin1(kThemeSetting), codeName(kCodes[std::size_t(type)]));
    for (std::size_t i = 0; i < kPairSettings.size(); ++i)
        if (settings->value(QString::fromLatin1(kPairSettings[i])).toString() == id)
            settings->setValue(QString::fromLatin1(kPairSettings[i]), codeName(kCodes[i]));
}

void uninstall(const QString &id)
{
    const Entry *entry = find(id);
    if (codeFromName(id) || !entry)
        return;
    fallBackFrom(id, entry->file.type);
    std::erase_if(registry().entries, [&id](const Entry &e) { return e.id == id; });
    Hub::get().themeListChanged = true;
    Hub::get().refresh();
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
    if (const auto icons = values.constFind(QLatin1String(icons::kThemeSetting)); icons != values.cend())
        icons::preview(icons->toString());
}

void endPreview()
{
    icons::endPreview();
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
    style::TextMenus::setSource(QStringLiteral("Hikari.Ui"), QStringLiteral("TextMenu"));
    MenuKey::install();
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

QVariantMap Theme::ports() const
{
    QVariantMap out;
    const auto &colours = theme::current().content.ports;
    for (std::size_t i = 0; i < theme::kPortColourTypes.size(); ++i)
        out.insert(QString::fromLatin1(theme::kPortColourTypes[i]), colours[i]);
    out.insert(QStringLiteral("Time"), colours[2]);
    out.insert(QStringLiteral("Alpha"), colours[2]);
    return out;
}

QString Theme::accentKey() const
{
    const auto &s = theme::current();
    const auto list = theme::accents(s.id);
    if (list.empty())
        return {};
    const QString picked = theme::accentOf(s.choice, s.id);
    for (const auto &a : list)
        if (a.key == picked)
            return picked;
    for (const auto &a : list)
        if (a.accent == s.roles.accent && a.select == s.roles.select)
            return a.key;
    return list.front().key;
}

QVariantList Theme::themes() const
{
    QVariantList out;
    for (const auto &e : theme::entries()) {
        out << QVariantMap{{QStringLiteral("code"), e.id},
                           {QStringLiteral("name"), theme::shownTitle(e)},
                           {QStringLiteral("source"), e.title},
                           {QStringLiteral("plugin"), e.plugin},
                           {QStringLiteral("type"), QString::fromLatin1(themes::typeName(e.file.type))},
                           {QStringLiteral("dark"), themes::isDark(e.file.type)},
                           {QStringLiteral("highContrast"), themes::isHighContrast(e.file.type)}};
    }
    return out;
}

QVariantList Theme::accents(bool dark) const
{
    return accentsOf(theme::codeName(dark ? theme::Code::Dark : theme::Code::Light));
}

QVariantList Theme::accentsOf(const QString &code) const
{
    const theme::Roles r = theme::resolve(code, {});
    const std::array surfaces{r.background, r.panel, r.raised, r.field};
    QVariantList out;
    for (const auto &a : theme::accents(code)) {
        double lowest = 21;
        for (const QColor &s : surfaces)
            lowest = std::min(lowest, theme::contrastRatio(a.accent, s));
        const QByteArray source = a.name.toUtf8();
        out << QVariantMap{{QStringLiteral("key"), a.key},
                           {QStringLiteral("name"), QCoreApplication::translate("HikariSub.Options", source.constData())},
                           {QStringLiteral("source"), a.name},
                           {QStringLiteral("accent"), a.accent},
                           {QStringLiteral("onAccent"), a.onAccent},
                           {QStringLiteral("contrast"), lowest}};
    }
    return out;
}

namespace {

const char *roleTitle(themes::Role role)
{
    using R = themes::Role;
    switch (role) {
    case R::Accent:
        return QT_TRANSLATE_NOOP("HikariSub.Options", "Accent colour");
    case R::Text:
        return QT_TRANSLATE_NOOP("HikariSub.Options", "Text and icons");
    case R::Line:
        return QT_TRANSLATE_NOOP("HikariSub.Options", "Border colour");
    case R::Background:
        return QT_TRANSLATE_NOOP("HikariSub.Options", "Background");
    case R::Panel:
        return QT_TRANSLATE_NOOP("HikariSub.Options", "Panels");
    case R::Raised:
        return QT_TRANSLATE_NOOP("HikariSub.Options", "Buttons and headers");
    case R::Field:
        return QT_TRANSLATE_NOOP("HikariSub.Options", "Fields");
    case R::Muted:
        return QT_TRANSLATE_NOOP("HikariSub.Options", "Secondary text");
    case R::Control:
        return QT_TRANSLATE_NOOP("HikariSub.Options", "Control outlines");
    case R::Disabled:
        return QT_TRANSLATE_NOOP("HikariSub.Options", "Disabled text");
    case R::Select:
        return QT_TRANSLATE_NOOP("HikariSub.Options", "Selection");
    case R::Danger:
        return QT_TRANSLATE_NOOP("HikariSub.Options", "Errors");
    case R::Warning:
        return QT_TRANSLATE_NOOP("HikariSub.Options", "Warnings");
    case R::Success:
        return QT_TRANSLATE_NOOP("HikariSub.Options", "Success");
    default:
        return "";
    }
}

}

QVariantList Theme::picks(const QString &code) const
{
    const theme::Entry *entry = theme::find(code);
    if (!entry)
        return {};
    const themes::Resolved plain = themes::resolve(entry->file);
    QVariantList out;
    for (const auto role : entry->file.userColours) {
        const QString name = QLatin1String(themes::kRoleNames[std::size_t(role)]);
        out << QVariantMap{{QStringLiteral("role"), name},
                           {QStringLiteral("name"), QCoreApplication::translate("HikariSub.Options", roleTitle(role))},
                           {QStringLiteral("defaultColour"),
                            QColor::fromRgb(QRgb(themes::at(plain.roles, role))).name(QColor::HexRgb).toUpper()}};
    }
    return out;
}

QString Theme::accentChoice(const QStringList &list, const QString &code) const
{
    theme::Choice choice;
    choice.accents = list;
    const QString picked = theme::accentOf(choice, code);
    const auto all = theme::accents(code);
    for (const auto &a : all)
        if (a.key == picked)
            return picked;
    const theme::Roles r = theme::resolve(code, {});
    for (const auto &a : all)
        if (a.accent == r.accent && a.select == r.select)
            return a.key;
    return {};
}

QStringList Theme::withAccentChoice(const QStringList &list, const QString &code, const QString &choice) const
{
    return theme::withEntry(list, code, choice);
}

QString Theme::userColour(const QStringList &list, const QString &code, const QString &role) const
{
    theme::Choice choice;
    choice.colours = list;
    const QColor picked = theme::colourOf(choice, code, role);
    if (picked.isValid())
        return picked.name(QColor::HexRgb).toUpper();
    for (const QVariant &p : picks(code))
        if (p.toMap().value(QStringLiteral("role")) == role)
            return p.toMap().value(QStringLiteral("defaultColour")).toString();
    return {};
}

QStringList Theme::withUserColour(const QStringList &list, const QString &code, const QString &role,
                                  const QString &colour) const
{
    return theme::withEntry(list, code + QLatin1Char('/') + role, colour);
}

QStringList Theme::withoutUserColours(const QStringList &list, const QString &code) const
{
    QStringList out = list;
    out.removeIf([&code](const QString &item) { return item.startsWith(code + QLatin1Char('/')); });
    return out;
}

qreal Theme::fontSize(int reference) const
{
    const QFont font = QGuiApplication::font();
    const qreal body = font.pixelSize() > 0 ? font.pixelSize() : font.pointSizeF() * 96.0 / 72.0;
    return body * reference / 13.0;
}

QVariantMap Theme::rolesOf(const QString &code, const QVariantMap &values) const
{
    if (!theme::find(code))
        return {};
    const auto choice = theme::choiceFrom([&values](const char *id) { return values.value(QLatin1String(id)); });
    const theme::Roles roles = theme::resolve(code, choice);
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
