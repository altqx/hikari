#pragma once

// K1: the in-house vector icon set (docs/qt/ux/icons.md). Each UI icon is a
// monochrome SVG on a 16-unit grid painted with currentColor, with at most
// one accent layer (<g id="accent">). The set is tinted at run time from the
// theme layer (K2, theme.h), live: the theme's text colour, its accent (the
// accent layer, and the whole icon while hovered or pressed) and its
// disabled colour. There are no icon colour settings: in high contrast the
// "Text and icons" and accent pickers recolour them with the text.

#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QQuickImageProvider>
#include <QQuickPaintedItem>
#include <QUrl>
#include <QWindow>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <memory>
#include <optional>
#include <string_view>

class QQmlEngine;
class QJSEngine;
class QSvgRenderer;

namespace hikari::ui {

namespace icons {

// The set's manifest (src/ui/icons/manifest.json) and its SVG files, from the
// QML module's resources.
QString resourcePath(const QString &role);
QStringList roles();
bool exists(const QString &role);
// The legacy tooltip or menu text the icon's action carries (the default
// accessible name of an icon-only control).
QString label(const QString &role);
// A directional icon, mirrored in right-to-left layouts (media and data
// symbols are not).
bool mirrors(const QString &role);
// The icon's SVG text, empty for an unknown role.
QByteArray source(const QString &role);
// The SVG with currentColor resolved: the accent layer in `accent`, the rest
// in `colour`.
QByteArray tint(const QByteArray &svg, const QColor &colour, const QColor &accent);
// The icon drawn into a transparent image of `pixels` (premultiplied ARGB).
QImage render(const QString &role, QSize pixels, const QColor &colour, const QColor &accent);

} // namespace icons

// The theme's icon colours, for QML (the IconTheme singleton; one per
// engine, all following the theme layer).
class IconTheme : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QColor normal READ normal NOTIFY changed FINAL)
    Q_PROPERTY(QColor accent READ accent NOTIFY changed FINAL)
    Q_PROPERTY(QColor active READ active NOTIFY changed FINAL)
    Q_PROPERTY(QColor disabled READ disabled NOTIFY changed FINAL)
public:
    // (Not default-constructible, so the engine makes it through create().)
    explicit IconTheme(QObject *parent);
    static IconTheme *create(QQmlEngine *engine, QJSEngine *js);

    QColor normal() const { return m_colours[0]; }
    QColor accent() const { return m_colours[1]; }
    QColor active() const { return m_colours[2]; }
    QColor disabled() const { return m_colours[3]; }

    Q_INVOKABLE bool exists(const QString &role) const { return icons::exists(role); }
    Q_INVOKABLE bool mirrors(const QString &role) const { return icons::mirrors(role); }
    Q_INVOKABLE QString label(const QString &role) const { return icons::label(role); }
    Q_INVOKABLE QStringList roles() const { return icons::roles(); }
    // The icon as an image source, for the controls that draw one (menu
    // items, tab buttons): drawn by the engine's IconImageProvider in
    // `colour` with its accent layer in `accent`, flipped when `mirrored`.
    Q_INVOKABLE QUrl image(const QString &role, const QColor &colour, const QColor &accent, bool mirrored) const;
    // The icon as `window`'s icon (legacy dialogs' SetIcon), in the current
    // appearance's colours and kept in them while the window lives.
    Q_INVOKABLE void setWindowIcon(QObject *window, const QString &role);

    // The theme's icon colours: its text, accent, the hover/pressed colour
    // (the accent) and disabled colour.
    static std::array<QColor, 4> colours();

signals:
    void changed();

private:
    void refresh();
    void applyWindowIcon(QWindow *window, const QString &role) const;

    std::array<QColor, 4> m_colours;
    QList<std::pair<QPointer<QWindow>, QString>> m_windows;
};

// "image://hikari-icon/<role>/<RRGGBB>/<RRGGBB accent>/<0|1 mirrored>": the
// icon drawn at the requested size, which the image item asks in the
// device's pixels (its source size times the window's scale), so the vectors
// are drawn again at 150% and 200% rather than scaled up. IconTheme adds it to
// its engine.
class IconImageProvider : public QQuickImageProvider {
public:
    static constexpr const char *kId = "hikari-icon";
    IconImageProvider();
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};

// The painted icon: an SVG of the set with its two colours, crisp at any
// device pixel ratio (it paints vectors into the item's texture at the
// window's scale). Icon.qml binds the colours to IconTheme.
class TintedSvg : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString iconRole READ iconRole WRITE setIconRole NOTIFY iconRoleChanged FINAL)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged FINAL)
    Q_PROPERTY(QColor accentColor READ accentColor WRITE setAccentColor NOTIFY accentColorChanged FINAL)
    Q_PROPERTY(bool mirrored READ mirrored WRITE setMirrored NOTIFY mirroredChanged FINAL)
    Q_PROPERTY(bool valid READ valid NOTIFY iconRoleChanged FINAL)
public:
    explicit TintedSvg(QQuickItem *parent = nullptr);
    ~TintedSvg() override;

    QString iconRole() const { return m_role; }
    void setIconRole(const QString &role);
    QColor color() const { return m_colour; }
    void setColor(const QColor &colour);
    QColor accentColor() const { return m_accent; }
    void setAccentColor(const QColor &colour);
    bool mirrored() const { return m_mirrored; }
    void setMirrored(bool mirrored);
    bool valid() const { return icons::exists(m_role); }

    void paint(QPainter *painter) override;

signals:
    void iconRoleChanged();
    void colorChanged();
    void accentColorChanged();
    void mirroredChanged();

private:
    void rebuild();

    QString m_role;
    QColor m_colour = Qt::black;
    QColor m_accent = Qt::black;
    bool m_mirrored = false;
    std::unique_ptr<QSvgRenderer> m_renderer;
};

} // namespace hikari::ui
