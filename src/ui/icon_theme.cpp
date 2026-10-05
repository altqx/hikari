#include "icon_theme.h"

#include "settings_store.h"

#include "hikari/application/settings.h"

#include <QAccessibilityHints>
#include <QEvent>
#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPalette>
#include <QIcon>
#include <QPixmap>
#include <QStyleHints>
#include <QSvgRenderer>
#include <QWindow>
#include <QtQml/qqmlengine.h>

#include <algorithm>
#include <cmath>

namespace hikari::ui {

namespace icons {

namespace {

struct Entry {
    QString label;
    bool mirror = false;
};

struct Manifest {
    QStringList roles;
    QHash<QString, Entry> entries;
};

const Manifest &manifest()
{
    static const Manifest loaded = [] {
        Manifest m;
        QFile file(QStringLiteral(":/qt/qml/Hikari/Ui/icons/manifest.json"));
        if (!file.open(QIODevice::ReadOnly))
            return m;
        const auto icons = QJsonDocument::fromJson(file.readAll()).object().value(QLatin1String("icons")).toArray();
        for (const auto &value : icons) {
            const auto icon = value.toObject();
            const QString role = icon.value(QLatin1String("role")).toString();
            m.roles << role;
            m.entries.insert(role, {icon.value(QLatin1String("label")).toString(), icon.value(QLatin1String("mirror")).toBool()});
        }
        return m;
    }();
    return loaded;
}

int index(Appearance appearance)
{
    return static_cast<int>(appearance);
}

} // namespace

QString appearanceName(Appearance appearance)
{
    switch (appearance) {
    case Appearance::Light:
        return QStringLiteral("light");
    case Appearance::Dark:
        return QStringLiteral("dark");
    case Appearance::HighContrast:
        return QStringLiteral("highContrast");
    }
    return QStringLiteral("light");
}

std::optional<Appearance> appearanceFromName(const QString &name)
{
    for (const auto appearance : kAppearances)
        if (appearanceName(appearance) == name)
            return appearance;
    return std::nullopt;
}

std::string_view settingId(Appearance appearance, Slot slot)
{
    return application::kIconColourSettings[index(appearance)][static_cast<int>(slot)];
}

QColor defaultColour(Appearance appearance, Slot slot)
{
    const auto *setting = application::findSetting(settingId(appearance, slot));
    if (!setting)
        return {};
    return QColor(QString::fromStdString(std::get<std::string>(setting->defaultValue)));
}

std::array<QColor, 4> surfaces(Appearance appearance)
{
    // visual-language.md, "Semantic appearance tokens": bg, panel, raised, field.
    switch (appearance) {
    case Appearance::Light:
        return {QColor(QRgb(0xE5E9EC)), QColor(QRgb(0xF9FAFB)), QColor(QRgb(0xEDF0F3)), QColor(QRgb(0xFFFFFF))};
    case Appearance::Dark:
        return {QColor(QRgb(0x171B20)), QColor(QRgb(0x20262D)), QColor(QRgb(0x29313A)), QColor(QRgb(0x171D24))};
    case Appearance::HighContrast:
        return {QColor(QRgb(0x000000)), QColor(QRgb(0x080808)), QColor(QRgb(0x151515)), QColor(QRgb(0x000000))};
    }
    return {};
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

QString resourcePath(const QString &role)
{
    return QStringLiteral(":/qt/qml/Hikari/Ui/icons/") + role + QStringLiteral(".svg");
}

QStringList roles()
{
    return manifest().roles;
}

bool exists(const QString &role)
{
    return manifest().entries.contains(role);
}

QString label(const QString &role)
{
    return manifest().entries.value(role).label;
}

bool mirrors(const QString &role)
{
    return manifest().entries.value(role).mirror;
}

QByteArray source(const QString &role)
{
    static QHash<QString, QByteArray> cache;
    if (!exists(role))
        return {};
    auto it = cache.find(role);
    if (it == cache.end()) {
        QFile file(resourcePath(role));
        it = cache.insert(role, file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray());
    }
    return *it;
}

QByteArray tint(const QByteArray &svg, const QColor &colour, const QColor &accent)
{
    static const QByteArray kCurrent("currentColor");
    static const QByteArray kAccentOpen("<g id=\"accent\">");
    const QByteArray base = colour.name(QColor::HexRgb).toLatin1();
    const QByteArray layer = accent.name(QColor::HexRgb).toLatin1();
    QByteArray out = svg;
    // The accent group's strokes inherit its own stroke; its explicit paints
    // are currentColor, which is the accent there.
    if (const auto start = out.indexOf(kAccentOpen); start >= 0) {
        const auto end = out.indexOf("</g>", start);
        if (end > start) {
            QByteArray group = out.mid(start + kAccentOpen.size(), end - start - kAccentOpen.size());
            group.replace(kCurrent, layer);
            out = out.left(start) + "<g id=\"accent\" stroke=\"" + layer + "\">" + group + out.mid(end);
        }
    }
    out.replace(kCurrent, base);
    return out;
}

QImage render(const QString &role, QSize pixels, const QColor &colour, const QColor &accent)
{
    QImage image(pixels, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QSvgRenderer renderer(tint(source(role), colour, accent));
    if (!renderer.isValid())
        return image;
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter, QRectF(QPointF(0, 0), QSizeF(pixels)));
    return image;
}

} // namespace icons

namespace {

// What every IconTheme follows: the profile's colours, the application
// palette and the platform's contrast preference.
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

    QPointer<SettingsStore> settings;
    std::optional<icons::Appearance> forced;

    void use(SettingsStore *store)
    {
        if (settings)
            disconnect(settings, nullptr, this, nullptr);
        settings = store;
        if (store)
            connect(store, &SettingsStore::changed, this, [this](const QString &id) {
                if (id.startsWith(QLatin1String("icons.")))
                    emit changed();
            });
        emit changed();
    }

signals:
    void changed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == QCoreApplication::instance() && event->type() == QEvent::ApplicationPaletteChange)
            emit changed();
        return QObject::eventFilter(watched, event);
    }

private:
    explicit Hub(QObject *parent) : QObject(parent)
    {
        if (auto *app = QCoreApplication::instance())
            app->installEventFilter(this);
        if (auto *hints = QGuiApplication::styleHints()) {
            connect(hints, &QStyleHints::colorSchemeChanged, this, &Hub::changed);
            connect(hints->accessibility(), &QAccessibilityHints::contrastPreferenceChanged, this, &Hub::changed);
        }
    }
};

} // namespace

IconTheme::IconTheme(QObject *parent) : QObject(parent)
{
    connect(&Hub::get(), &Hub::changed, this, &IconTheme::refresh);
    refresh();
}

IconTheme *IconTheme::create(QQmlEngine *engine, QJSEngine *)
{
    if (!engine->imageProvider(QLatin1String(IconImageProvider::kId)))
        engine->addImageProvider(QLatin1String(IconImageProvider::kId), new IconImageProvider);
    return new IconTheme(engine);
}

QUrl IconTheme::image(const QString &role, const QColor &colour, const QColor &accent, bool mirrored) const
{
    const auto hex = [](const QColor &c) { return c.name(QColor::HexRgb).mid(1); };
    return QUrl(QStringLiteral("image://%1/%2/%3/%4/%5")
                    .arg(QLatin1String(IconImageProvider::kId), role, hex(colour), hex(accent),
                         mirrored ? QStringLiteral("1") : QStringLiteral("0")));
}

void IconTheme::setWindowIcon(QObject *object, const QString &role)
{
    auto *window = qobject_cast<QWindow *>(object);
    if (!window || !icons::exists(role))
        return;
    m_windows.removeIf([window](const auto &entry) { return !entry.first || entry.first == window; });
    m_windows.append({window, role});
    applyWindowIcon(window, role);
}

void IconTheme::applyWindowIcon(QWindow *window, const QString &role) const
{
    // Drawn at each size a title bar or task switcher asks for, at once.
    QIcon icon;
    for (const int side : {16, 20, 24, 32, 40, 48, 64})
        icon.addPixmap(QPixmap::fromImage(icons::render(role, QSize(side, side), normal(), accent())));
    window->setIcon(icon);
}

icons::Appearance IconTheme::currentAppearance()
{
    if (Hub::get().forced)
        return *Hub::get().forced;
    if (const auto *hints = QGuiApplication::styleHints();
        hints && hints->accessibility()->contrastPreference() == Qt::ContrastPreference::HighContrast)
        return icons::Appearance::HighContrast;
    return QGuiApplication::palette().color(QPalette::Window).lightnessF() < 0.5 ? icons::Appearance::Dark
                                                                                  : icons::Appearance::Light;
}

void IconTheme::useSettings(SettingsStore *settings)
{
    Hub::get().use(settings);
}

void IconTheme::forceAppearance(std::optional<icons::Appearance> appearance)
{
    Hub::get().forced = appearance;
    emit Hub::get().changed();
}

QColor IconTheme::colour(icons::Appearance appearance, icons::Slot slot)
{
    // A colour the user saved in the profile (icons.<appearance>.<slot>)
    // still wins until the theme model replaces those settings. The registry's
    // own parser reads the value, the same one applySettings validates with:
    // "#RRGGBB", or "#RRGGBBAA" (QColor would read nine digits as #AARRGGBB).
    // Anything it rejects is ignored. The tint paints the colour opaque
    // (icons::tint writes #RRGGBB).
    if (const auto &settings = Hub::get().settings) {
        const char *id = icons::settingId(appearance, slot).data();
        if (settings->contains(id)) {
            const QString stored = settings->text(id);
            if (const auto argb = application::parseSettingColour(stored.toStdString()))
                return QColor::fromRgba(QRgb(*argb));
        }
    }
    return paletteColour(slot);
}

QColor IconTheme::paletteColour(icons::Slot slot)
{
    // The active theme's palette: its text colour, its accent (the accent
    // layer, and the whole icon while hovered or pressed) and its disabled
    // text colour.
    const QPalette palette = QGuiApplication::palette();
    switch (slot) {
    case icons::Slot::Normal:
        return palette.color(QPalette::Active, QPalette::WindowText);
    case icons::Slot::Accent:
    case icons::Slot::Active:
        return palette.color(QPalette::Active, QPalette::Accent);
    case icons::Slot::Disabled:
        return palette.color(QPalette::Disabled, QPalette::WindowText);
    }
    return palette.color(QPalette::Active, QPalette::WindowText);
}

QString IconTheme::defaultColour(const QString &settingId) const
{
    for (const auto appearance : icons::kAppearances)
        for (const auto slot : icons::kSlots)
            if (settingId == QLatin1String(icons::settingId(appearance, slot)))
                return icons::defaultColour(appearance, slot).name(QColor::HexRgb).toUpper();
    return {};
}

QStringList IconTheme::settingIds() const
{
    QStringList out;
    for (const auto appearance : icons::kAppearances)
        for (const auto slot : icons::kSlots)
            out << QString::fromLatin1(icons::settingId(appearance, slot));
    return out;
}

void IconTheme::refresh()
{
    const auto appearance = currentAppearance();
    std::array<QColor, 4> colours;
    for (std::size_t i = 0; i < icons::kSlots.size(); ++i)
        colours[i] = colour(appearance, icons::kSlots[i]);
    if (appearance == m_appearance && colours == m_colours)
        return;
    m_appearance = appearance;
    m_colours = colours;
    for (const auto &[window, role] : std::as_const(m_windows))
        if (window)
            applyWindowIcon(window, role);
    emit changed();
}

IconImageProvider::IconImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage IconImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const QStringList parts = id.split(QLatin1Char('/'));
    QSize pixels(16, 16);
    if (requestedSize.width() > 0 || requestedSize.height() > 0) {
        const int side = std::max(requestedSize.width(), requestedSize.height());
        pixels = QSize(requestedSize.width() > 0 ? requestedSize.width() : side,
                       requestedSize.height() > 0 ? requestedSize.height() : side);
    }
    if (size)
        *size = pixels;
    if (parts.size() != 4 || !icons::exists(parts[0])) {
        QImage blank(pixels, QImage::Format_ARGB32_Premultiplied);
        blank.fill(Qt::transparent);
        return blank;
    }
    const auto colour = [](const QString &hex) { return QColor(QLatin1Char('#') + hex); };
    QImage image = icons::render(parts[0], pixels, colour(parts[1]), colour(parts[2]));
    if (parts[3] == QLatin1String("1"))
        image = image.flipped(Qt::Horizontal);
    return image;
}

TintedSvg::TintedSvg(QQuickItem *parent) : QQuickPaintedItem(parent) {}

TintedSvg::~TintedSvg() = default;

void TintedSvg::setIconRole(const QString &role)
{
    if (role == m_role)
        return;
    m_role = role;
    rebuild();
    emit iconRoleChanged();
}

void TintedSvg::setColor(const QColor &colour)
{
    if (colour == m_colour)
        return;
    m_colour = colour;
    rebuild();
    emit colorChanged();
}

void TintedSvg::setAccentColor(const QColor &colour)
{
    if (colour == m_accent)
        return;
    m_accent = colour;
    rebuild();
    emit accentColorChanged();
}

void TintedSvg::setMirrored(bool mirrored)
{
    if (mirrored == m_mirrored)
        return;
    m_mirrored = mirrored;
    update();
    emit mirroredChanged();
}

void TintedSvg::rebuild()
{
    const QByteArray svg = icons::source(m_role);
    if (svg.isEmpty())
        m_renderer.reset();
    else
        m_renderer = std::make_unique<QSvgRenderer>(icons::tint(svg, m_colour, m_accent));
    update();
}

void TintedSvg::paint(QPainter *painter)
{
    if (!m_renderer || !m_renderer->isValid())
        return;
    painter->setRenderHint(QPainter::Antialiasing);
    if (m_mirrored) {
        painter->translate(width(), 0);
        painter->scale(-1, 1);
    }
    m_renderer->render(painter, QRectF(0, 0, width(), height()));
}

} // namespace hikari::ui

#include "icon_theme.moc"
