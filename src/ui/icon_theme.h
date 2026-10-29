#pragma once

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

QString resourcePath(const QString &role);
QStringList roles();
bool exists(const QString &role);
QString label(const QString &role);
bool mirrors(const QString &role);
QByteArray source(const QString &role);
QByteArray tint(const QByteArray &svg, const QColor &colour, const QColor &accent);
QImage render(const QString &role, QSize pixels, const QColor &colour, const QColor &accent);

}

class IconTheme : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QColor normal READ normal NOTIFY changed FINAL)
    Q_PROPERTY(QColor accent READ accent NOTIFY changed FINAL)
    Q_PROPERTY(QColor disabled READ disabled NOTIFY changed FINAL)
public:
    explicit IconTheme(QObject *parent);
    static IconTheme *create(QQmlEngine *engine, QJSEngine *js);

    QColor normal() const { return m_colours[0]; }
    QColor accent() const { return m_colours[1]; }
    QColor disabled() const { return m_colours[2]; }

    Q_INVOKABLE bool exists(const QString &role) const { return icons::exists(role); }
    Q_INVOKABLE bool mirrors(const QString &role) const { return icons::mirrors(role); }
    Q_INVOKABLE QString label(const QString &role) const { return icons::label(role); }
    Q_INVOKABLE QStringList roles() const { return icons::roles(); }
    Q_INVOKABLE QUrl image(const QString &role, const QColor &colour, const QColor &accent, bool mirrored) const;
    Q_INVOKABLE void setWindowIcon(QObject *window, const QString &role);

    static std::array<QColor, 3> colours();

signals:
    void changed();

private:
    void refresh();
    void applyWindowIcon(QWindow *window, const QString &role) const;

    std::array<QColor, 3> m_colours;
    QList<std::pair<QPointer<QWindow>, QString>> m_windows;
};

class IconImageProvider : public QQuickImageProvider {
public:
    static constexpr const char *kId = "hikari-icon";
    IconImageProvider();
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};

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

}
