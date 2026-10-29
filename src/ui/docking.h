#pragma once

#include <QHash>
#include <QJSValue>
#include <QObject>
#include <QPointer>
#include <QQuickWindow>
#include <QSize>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <optional>

class QQmlEngine;
class QQuickItem;
class QWindow;

namespace hikari::ui {

// KDDW is process-wide and serves one QML engine: false while an earlier engine still lives.
bool attachDocking(QQmlEngine &engine);

// Not on Wayland, where the compositor places windows.
int keepFloatingPanelsOnScreen();

int fitFloatingLayouts();

void refreshDockConstraints();

namespace dockchrome {
inline constexpr int kHeaderHeight = 35;
inline constexpr int kTabGap = 12;
inline constexpr int kResizeGrip = 4;
// 0 on X11 without a compositing manager, where the transparent margin would come out black.
int floatingShadow();
bool windowSystemComposites();
void overrideCompositing(std::optional<bool> composites);

inline constexpr int kScaleSettleMs = 250;
inline constexpr int kScreenLossWatchMs = 2000;
// Windows' scale (GetDpiForWindow / 96) can change before Qt's devicePixelRatio does; nullopt elsewhere.
std::optional<qreal> nativeScale(QWindow *window);
void overrideNativeScale(std::function<std::optional<qreal>(QWindow *)> scale);
}

class Docking : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool systemMove READ systemMove NOTIFY systemMoveChanged FINAL)
    Q_PROPERTY(int floatingShadow READ floatingShadow CONSTANT FINAL)
    Q_PROPERTY(int resizeGrip READ resizeGrip CONSTANT FINAL)
    Q_PROPERTY(int headerRevision READ headerRevision NOTIFY headersChanged FINAL)
    Q_PROPERTY(QString openMenu READ openMenu WRITE setOpenMenu NOTIFY openMenuChanged FINAL)
    Q_PROPERTY(int windowTitleRevision READ windowTitleRevision NOTIFY windowSystemTitlesChanged FINAL)
public:
    explicit Docking(QObject *parent = nullptr);

    static bool platformUsesSystemMove(QStringView platformName);
    static bool platformUsesSystemMove();
    bool systemMove() const { return m_systemMove; }
    void setSystemMove(bool on);
    static int floatingShadow() { return dockchrome::floatingShadow(); }
    static int resizeGrip() { return dockchrome::kResizeGrip; }
    int headerRevision() const { return m_revision; }
    QString openMenu() const { return m_openMenu; }
    void setOpenMenu(const QString &uniqueName);
    int windowTitleRevision() const { return m_windowTitleRevision; }

    // A Wayland compositor may frame a floating panel's window although it asked for no frame.
    Q_INVOKABLE bool windowSystemTitle(QWindow *window) const;
    Q_INVOKABLE bool windowSystemFrame(QWindow *window) const;
    Q_INVOKABLE int windowShadow(QWindow *window) const;

    Q_INVOKABLE QString dockNameAt(QObject *tabBar, int index) const;
    // On Wayland KDDW drags panels with real drag-and-drop into a QML DropArea whose dropAreaCpp names this view.
    Q_INVOKABLE QObject *mainDropArea(const QString &uniqueName) const;
    Q_INVOKABLE bool resizeInLayout(const QString &uniqueName, int left, int top, int right, int bottom);
    Q_INVOKABLE bool setMinimumSize(const QString &uniqueName, QSize size);
    Q_INVOKABLE QSize minimumSize(const QString &uniqueName) const;
    Q_INVOKABLE bool followLayoutMinimum(const QString &mainName);
    Q_INVOKABLE QSize layoutMinimumSize(const QString &mainName) const;

    // MUST KILL: `horizontal` means "keeps a one-tab bar even when alone", not an orientation
    Q_INVOKABLE void setPanelHeader(const QString &uniqueName, bool horizontal, QQuickItem *toolbar);
    Q_INVOKABLE bool isHorizontal(const QString &uniqueName) const;
    Q_INVOKABLE bool hasPlace(const QString &uniqueName) const;
    Q_INVOKABLE QQuickItem *toolbar(const QString &uniqueName) const;
    Q_INVOKABLE void requestMenu(const QString &uniqueName, QQuickItem *anchor);
    Q_INVOKABLE bool toggleFloating(const QString &uniqueName);
    Q_INVOKABLE int tabIndex(const QString &uniqueName) const;
    Q_INVOKABLE int tabCount(const QString &uniqueName) const;
    Q_INVOKABLE bool moveTab(const QString &uniqueName, int step);
    Q_INVOKABLE bool startSystemMove(QQuickItem *from);
    // KDDW's main window closes every panel on QEvent::Close before the window's own handler runs.
    Q_INVOKABLE void guardClose(QQuickWindow *window, const QJSValue &admit);
    Q_INVOKABLE void requestKeyboardContextMenu();

signals:
    void headersChanged();
    void systemMoveChanged();
    void systemMoveRequested(QWindow *window);
    void menuRequested(const QString &uniqueName, QQuickItem *anchor);
    void openMenuChanged();
    void windowSystemTitlesChanged();

private:
    struct Header {
        bool horizontal = false;
        QPointer<QQuickItem> toolbar;
    };
    QHash<QString, Header> m_headers;
    int m_revision = 0;
    bool m_systemMove = platformUsesSystemMove();
    QString m_openMenu;
    int m_windowTitleRevision = 0;
};

}
