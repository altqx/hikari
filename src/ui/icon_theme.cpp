#include "icon_theme.h"

#include "settings_store.h"
#include "theme.h"

#include <QCoreApplication>
#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QIcon>
#include <QPixmap>
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

    const PluginIconTheme *active() const
    {
        const QString id = settings ? settings->value(QLatin1String(kThemeSetting)).toString() : QString();
        const auto it = std::ranges::find(themes, id, &PluginIconTheme::id);
        return it == themes.end() ? nullptr : &*it;
    }

    void bump()
    {
        ++generation;
        emit changed();
    }

    std::vector<PluginIconTheme> themes;
    QHash<QString, QByteArray> own;
    QPointer<SettingsStore> settings;
    int generation = 0;

signals:
    void changed();

private:
    using QObject::QObject;
};

constexpr QLatin1StringView kOwnPrefix("own:");
constexpr QLatin1StringView kPluginPrefix("plugin:");

}

void installTheme(PluginIconTheme theme)
{
    auto &list = Hub::get().themes;
    const auto it = std::ranges::find(list, theme.id, &PluginIconTheme::id);
    if (it != list.end() && it->title == theme.title && it->plugin == theme.plugin && it->icons == theme.icons)
        return;
    if (it != list.end())
        *it = std::move(theme);
    else
        list.push_back(std::move(theme));
    Hub::get().bump();
}

void uninstallTheme(const QString &id)
{
    Hub &hub = Hub::get();
    if (std::erase_if(hub.themes, [&id](const PluginIconTheme &t) { return t.id == id; }) == 0)
        return;
    if (hub.settings && hub.settings->value(QLatin1String(kThemeSetting)).toString() == id)
        hub.settings->setValue(QLatin1String(kThemeSetting), QLatin1String(kBuiltinTheme));
    hub.bump();
}

const std::vector<PluginIconTheme> &themes()
{
    return Hub::get().themes;
}

void setOwnIcons(QHash<QString, QByteArray> icons)
{
    Hub &hub = Hub::get();
    if (icons == hub.own)
        return;
    hub.own = std::move(icons);
    hub.bump();
}

QString pluginIconRole(const QString &plugin, const QString &icon)
{
    return icon.startsWith(kOwnPrefix) ? kPluginPrefix + plugin + QLatin1Char(':') + icon.mid(kOwnPrefix.size()) : icon;
}

void useSettings(SettingsStore *settings)
{
    Hub &hub = Hub::get();
    if (hub.settings)
        QObject::disconnect(hub.settings, nullptr, &hub, nullptr);
    hub.settings = settings;
    if (settings)
        QObject::connect(settings, &SettingsStore::changed, &hub, [&hub](const QString &id) {
            if (id == QLatin1String(kThemeSetting))
                hub.bump();
        });
    hub.bump();
}

int generation()
{
    return Hub::get().generation;
}

void onChanged(QObject *context, std::function<void()> slot)
{
    QObject::connect(&Hub::get(), &Hub::changed, context, std::move(slot));
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
    return manifest().entries.contains(role) || Hub::get().own.contains(role);
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
    if (const auto own = Hub::get().own.constFind(role); own != Hub::get().own.cend())
        return *own;
    if (!exists(role))
        return {};
    if (const PluginIconTheme *theme = Hub::get().active())
        if (const auto icon = theme->icons.constFind(role); icon != theme->icons.cend())
            return *icon;
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

}

IconTheme::IconTheme(QObject *parent) : QObject(parent)
{
    theme::onChanged(this, [this] { refresh(); });
    icons::onChanged(this, [this] {
        for (const auto &[window, role] : std::as_const(m_windows))
            if (window)
                applyWindowIcon(window, role);
        emit changed();
    });
    refresh();
}

QVariantList IconTheme::themes() const
{
    QVariantList out;
    for (const icons::PluginIconTheme &t : icons::themes())
        out << QVariantMap{{QStringLiteral("id"), t.id}, {QStringLiteral("title"), t.title}, {QStringLiteral("plugin"), t.plugin},
                           {QStringLiteral("covers"), t.icons.size()}};
    return out;
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
    return QUrl(QStringLiteral("image://%1/%2/%3/%4/%5/%6")
                    .arg(QLatin1String(IconImageProvider::kId), role, hex(colour), hex(accent),
                         mirrored ? QStringLiteral("1") : QStringLiteral("0"), QString::number(icons::generation())));
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
    QIcon icon;
    for (const int side : {16, 20, 24, 32, 40, 48, 64})
        icon.addPixmap(QPixmap::fromImage(icons::render(role, QSize(side, side), normal(), accent())));
    window->setIcon(icon);
}

std::array<QColor, 3> IconTheme::colours()
{
    const auto &roles = theme::current().roles;
    return {roles.text, roles.accent, roles.disabled};
}

void IconTheme::refresh()
{
    const auto next = colours();
    if (next == m_colours)
        return;
    m_colours = next;
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
    if (parts.size() != 5 || !icons::exists(parts[0])) {
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

TintedSvg::TintedSvg(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    icons::onChanged(this, [this] { rebuild(); });
}

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

}

#include "icon_theme.moc"
