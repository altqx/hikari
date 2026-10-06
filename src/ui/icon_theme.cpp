#include "icon_theme.h"

#include "theme.h"

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

} // namespace

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

IconTheme::IconTheme(QObject *parent) : QObject(parent)
{
    theme::onChanged(this, [this] { refresh(); });
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

std::array<QColor, 4> IconTheme::colours()
{
    const auto &roles = theme::current().roles;
    return {roles.text, roles.accent, roles.accent, roles.disabled};
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

