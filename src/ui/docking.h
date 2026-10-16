#pragma once

// D1: Hikari's adapter around the docking engine (KDDockWidgets, QtQuick
// frontend; docs/qt/docking.md). The engine is process-wide and serves one
// QML engine; nothing outside this adapter talks to it directly.
//
// D3: the chrome follows MuseScore 4 (docs/research/musescore-docking.md):
// one header row per panel group (DockTabBar.qml draws a lone panel's title
// bar and the tab bars), a "⋯" menu instead of float and close buttons,
// 1-pixel separators, borderless floating tool windows and an accent drop
// highlight (DockDropHighlight.qml) over the area a drop would take.

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

// Binds the docking engine to the QML engine that is about to load the shell.
// Call it before loading Main. The production shell has one engine for its
// lifetime. When an earlier engine has been destroyed (tests create one per
// case) the docking engine is re-initialized for the new one; while an
// earlier engine still lives, the call does nothing and returns false.
// The engine draws Hikari's headers (DockTabBar.qml, DockTitleBar.qml),
// separators, group frames, floating windows and drop highlight.
bool attachDocking(QQmlEngine &engine);

// Keeps every floating panel window's header on an available screen
// (docs/qt/docking.md: removed monitors return floating groups to the main
// screen). On Wayland the compositor places windows and nothing is moved.
// Returns how many windows were moved.
int keepFloatingPanelsOnScreen();

// D1: has every floating panel window's layout fill the window's layout
// area again after a restore (the engine leaves it the area's height under
// a title bar the window no longer shows). Returns how many it resized.
int fitFloatingLayouts();

// D3: has every panel group take its panels' current minimum sizes
// (Docking.setMinimumSize) again. A restored layout brings the sizes it was
// saved with, which can be below them; the engine then grows the groups.
void refreshDockConstraints();

// D3 sizes (docs/research/musescore-docking.md §7): the header row, the gap
// between a tab bar and the content, and the floating window's drawn shadow
// (every platform: the window is borderless and transparent around it).
// X11 without a compositing manager shows no transparency, so the shadow's
// margin would come out black: there the shadow is 0 (native gate, D3).
namespace dockchrome {
inline constexpr int kHeaderHeight = 35;
inline constexpr int kTabGap = 12;
// The width of the band along a floating window's edge that resizes it:
// the shadow, or this much inside the frame where there is no shadow.
inline constexpr int kResizeGrip = 4;
int floatingShadow();
// Whether the window system composites (transparent windows show what is
// behind them): false only on X11 without a compositing manager.
bool windowSystemComposites();
// Tests: pretend the window system does or does not composite (nullopt:
// ask it again).
void overrideCompositing(std::optional<bool> composites);

// A floating panel's window keeps its size in device-independent pixels
// when its screen's scale changes (a live change of the display scale on
// Windows, 100 to 125 %): the window system may leave the window at its
// pixel size, which at the new scale is smaller than the panel (native gate:
// the floating Line editor kept 639 x 198 pixels at 125 % and its content
// was cut off). Once the change has settled (kScaleSettleMs after Qt's last
// device pixel ratio change for the window) the window takes back the size
// it had before it, at least its panels' minimum size and at most its
// screen's available size.
inline constexpr int kScaleSettleMs = 250;
// A floating panel's window whose screen goes away (a monitor unplugged or
// switched off, Windows' "PC screen only") is placed whole on the main
// window's screen at its size in device-independent pixels from before the
// loss (at most that screen's available size), kScaleSettleMs after the
// window system last moved or resized it; for kScreenLossWatchMs after the
// loss the window is placed again if the window system moves it again
// (Windows gate, 2026-10-15: the window came back with only 173 px of it on
// the remaining screen). Not on Wayland, where the compositor places windows.
inline constexpr int kScreenLossWatchMs = 2000;
// The window system's own scale for `window` (Windows: GetDpiForWindow /
// 96), which can change before Qt's devicePixelRatio does: a resize the
// window system makes for the new scale before Qt takes the new scale is
// not taken as the size to keep. nullopt where it is not known (other
// platforms, no native window).
std::optional<qreal> nativeScale(QWindow *window);
// Tests: answers nativeScale instead of the window system (nullptr: the
// window system again).
void overrideNativeScale(std::function<std::optional<qreal>(QWindow *)> scale);
} // namespace dockchrome

// Engine views and panel chrome the shell's QML needs (DockTabBar.qml,
// DockFloatingWindow.qml, Main.qml).
class Docking : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // The free part of a floating panel's header moves its window through
    // the window system (startSystemMove) while its title or tabs stay the
    // engine's drag: on Wayland and X11 (platformUsesSystemMove). On
    // Windows, offscreen and minimal the whole header is the engine's drag.
    // Tests set it on the offscreen platform (setSystemMove).
    Q_PROPERTY(bool systemMove READ systemMove NOTIFY systemMoveChanged FINAL)
    // The width of the floating window's drawn shadow.
    Q_PROPERTY(int floatingShadow READ floatingShadow CONSTANT FINAL)
    Q_PROPERTY(int resizeGrip READ resizeGrip CONSTANT FINAL)
    // Bumped when a panel's header description changes (setPanelHeader).
    Q_PROPERTY(int headerRevision READ headerRevision NOTIFY headersChanged FINAL)
    // The panel whose menu is open (Main.qml sets it), so its "⋯" button
    // shows as pressed.
    Q_PROPERTY(QString openMenu READ openMenu WRITE setOpenMenu NOTIFY openMenuChanged FINAL)
    // Bumped when the window system starts or stops drawing a frame or a
    // title bar on a floating panel's window (windowSystemFrame,
    // windowSystemTitle).
    Q_PROPERTY(int windowTitleRevision READ windowTitleRevision NOTIFY windowSystemTitlesChanged FINAL)
public:
    explicit Docking(QObject *parent = nullptr);

    // Whether a floating panel's header splits on the platform named
    // `platformName` (QGuiApplication::platformName(), the running one by
    // default): on Wayland and X11, not on Windows (the owner's choice of
    // 2026-10-15: Snap never takes the borderless tool window there), nor
    // on offscreen or minimal.
    static bool platformUsesSystemMove(QStringView platformName);
    static bool platformUsesSystemMove();
    bool systemMove() const { return m_systemMove; }
    // Tests: the split header, or the engine's drag over the whole header.
    void setSystemMove(bool on);
    static int floatingShadow() { return dockchrome::floatingShadow(); }
    static int resizeGrip() { return dockchrome::kResizeGrip; }
    int headerRevision() const { return m_revision; }
    QString openMenu() const { return m_openMenu; }
    void setOpenMenu(const QString &uniqueName);
    int windowTitleRevision() const { return m_windowTitleRevision; }

    // Whether the window system draws its own title bar on `window` (a
    // Wayland compositor that frames a floating panel's window although it
    // asked for no frame; window_decorations.h). Its headers then leave the
    // name to that title bar.
    Q_INVOKABLE bool windowSystemTitle(QWindow *window) const;
    // Whether the window system draws a frame of its own round `window`, a
    // title bar or only a border (Hyprland's). Its panel then draws no
    // shadow inside that frame (windowShadow).
    Q_INVOKABLE bool windowSystemFrame(QWindow *window) const;
    // The width of the shadow a floating panel's window draws round its
    // frame: floatingShadow, none inside a frame of the window system's.
    Q_INVOKABLE int windowShadow(QWindow *window) const;

    // The uniqueName of the panel of tab `index` of `tabBar` (the engine's
    // TabBarView); empty when there is none.
    Q_INVOKABLE QString dockNameAt(QObject *tabBar, int index) const;
    // The drop area view of the main window `uniqueName` (the DockingArea's).
    // On Wayland the engine drags panels with real drag-and-drop and takes a
    // drop through a QML DropArea whose dropAreaCpp property names this view.
    Q_INVOKABLE QObject *mainDropArea(const QString &uniqueName) const;
    // Grows (or with negative values shrinks) the docked panel `uniqueName`
    // at its edges, as the engine's DockWidget::resizeInLayout does; false
    // when it is not docked in a layout.
    Q_INVOKABLE bool resizeInLayout(const QString &uniqueName, int left, int top, int right, int bottom);
    // D3: the smallest size panel `uniqueName`'s content takes unclipped. The
    // engine keeps its group at least this big (plus the header), and the
    // window at least as big as the docked panels need.
    Q_INVOKABLE bool setMinimumSize(const QString &uniqueName, QSize size);
    // The panel's minimum size as the engine has it (its dock widget's).
    Q_INVOKABLE QSize minimumSize(const QString &uniqueName) const;
    // Decision 63: from now on the window holding main window `mainName`
    // (the DockingArea's uniqueName) keeps a minimum size of the docked
    // layout's minimum plus the window's own rows outside the docking area,
    // within its screen, following every panel docked, floated, shown or
    // hidden; a smaller window grows to it. False when there is no such
    // main window.
    Q_INVOKABLE bool followLayoutMinimum(const QString &mainName);
    // The docked layout's minimum size of main window `mainName` as the
    // engine computes it (its panels' minimums, headers and separators).
    Q_INVOKABLE QSize layoutMinimumSize(const QString &mainName) const;

    // D3: how a panel's header looks. A horizontal panel (the Grid, the
    // Reference tray, Search) keeps a one-tab bar even alone; `toolbar` is
    // shown in the slot right of the tabs while the panel is the current tab.
    Q_INVOKABLE void setPanelHeader(const QString &uniqueName, bool horizontal, QQuickItem *toolbar);
    Q_INVOKABLE bool isHorizontal(const QString &uniqueName) const;
    Q_INVOKABLE QQuickItem *toolbar(const QString &uniqueName) const;
    // The header's "⋯" button (or a right click on it) asks for panel
    // `uniqueName`'s menu, below `anchor`; Main.qml owns the menu.
    Q_INVOKABLE void requestMenu(const QString &uniqueName, QQuickItem *anchor);
    // Floats panel `uniqueName`, or docks it when it floats (a double-click
    // on a header the engine does not see: a floating header's free part).
    Q_INVOKABLE bool toggleFloating(const QString &uniqueName);
    // D1 Move before/after: panel `uniqueName`'s index among its group's
    // tabs and the group's number of tabs (-1 and 0 when it is in no group).
    Q_INVOKABLE int tabIndex(const QString &uniqueName) const;
    Q_INVOKABLE int tabCount(const QString &uniqueName) const;
    // Moves panel `uniqueName` `step` places along its group's tabs (toward
    // the first when negative), as its group's current tab; false when
    // there is no tab there.
    Q_INVOKABLE bool moveTab(const QString &uniqueName, int step);
    // Has the window system move the window holding `from` (the free part
    // of a floating header); whether the platform took it (X11 without a
    // window manager that moves windows, and offscreen, do not).
    // systemMoveRequested says so to tests on platforms that do not.
    Q_INVOKABLE bool startSystemMove(QQuickItem *from);
    // The main window's close, before the docking engine takes it: the
    // engine's main window filters the window's QEvent::Close and closes
    // every panel it holds, before the window's own closing handler runs.
    // `admit` (a QML function) runs first; when it returns false the close
    // is refused there and the engine never sees it, so the panels stay.
    Q_INVOKABLE void guardClose(QQuickWindow *window, const QJSValue &admit);
    // Asks the focused control for its context menu, as the keyboard does
    // (a QContextMenuEvent with the Keyboard reason to the focus window).
    // Main.qml's Shift+F10 on Windows, which keeps the key from the system.
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

} // namespace hikari::ui
