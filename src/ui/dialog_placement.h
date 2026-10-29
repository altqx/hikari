#pragma once

#include <QList>
#include <QMargins>
#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <optional>

#include "settings_store.h"

class QScreen;
class QWindow;

namespace hikari::ui {

namespace dialogplacement {

inline constexpr const char *kSetting = "workspace.dialogGeometry";
inline constexpr int kDrop = 20;
inline constexpr int kMargin = 20;
inline constexpr int kGrip = 64;

QRect centred(const QRect &opener, QSize size, const QRect &available, const QMargins &margins = {});

std::optional<QRect> restored(const QRect &saved, QSize size, QSize minimum, bool resizable,
                              const QList<QRect> &screens, const QMargins &margins = {});

std::optional<QRect> find(const QStringList &rows, const QString &id);
QStringList replaced(QStringList rows, const QString &id, const QRect &geometry);

}

class DialogPlacement : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickWindow *window READ window WRITE setWindow NOTIFY windowChanged FINAL)
    Q_PROPERTY(QString dialogId READ dialogId WRITE setDialogId NOTIFY dialogIdChanged FINAL)
    Q_PROPERTY(hikari::ui::SettingsStore *settings READ settings WRITE setSettings NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool resizable READ resizable WRITE setResizable NOTIFY resizableChanged FINAL)
    Q_PROPERTY(QSize room READ room NOTIFY roomChanged FINAL)
public:
    explicit DialogPlacement(QObject *parent = nullptr);

    QQuickWindow *window() const { return m_window; }
    void setWindow(QQuickWindow *window);
    QString dialogId() const { return m_dialogId; }
    void setDialogId(const QString &id);
    SettingsStore *settings() const { return m_settings; }
    void setSettings(SettingsStore *settings);
    bool resizable() const { return m_resizable; }
    void setResizable(bool resizable);
    QSize room() const { return m_room; }

    Q_INVOKABLE QWindow *opener() const;
    Q_INVOKABLE void place(QWindow *opener, QQuickItem *content);
    Q_INVOKABLE void refit();
    Q_INVOKABLE void showWithoutActivating(bool quiet);
    Q_INVOKABLE void unminimize();
    Q_INVOKABLE void remember();
    Q_INVOKABLE static bool inPopup(QQuickItem *item);
    Q_INVOKABLE static void settle(QQuickItem *item);

    static void overrideWayland(std::optional<bool> wayland);
    static void setOpenedHook(std::function<void(const QString &dialogId)> hook);

signals:
    void windowChanged();
    void dialogIdChanged();
    void settingsChanged();
    void resizableChanged();
    void roomChanged();

private:
    // MUST KILL: fitRoom must run before the geometry is set; the window's minimum size follows m_room
    void fitRoom(QScreen *on, const QMargins &margins);
    QSize natural() const;

    QPointer<QQuickWindow> m_window;
    QPointer<QQuickItem> m_content;
    QString m_dialogId;
    QPointer<SettingsStore> m_settings;
    bool m_resizable = false;
    QSize m_room{16777215, 16777215}; // QWINDOWSIZE_MAX
    QWindow::Visibility m_shown = QWindow::Windowed;
};

}
