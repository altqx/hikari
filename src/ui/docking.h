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
#include <QObject>
#include <QPointer>
#include <QSize>
#include <QtQml/qqmlregistration.h>

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
} // namespace dockchrome

// Engine views and panel chrome the shell's QML needs (DockTabBar.qml,
// DockFloatingWindow.qml, Main.qml).
class Docking : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // Wayland: the free part of a floating panel's header moves its window
    // through the compositor (startSystemMove); the engine cannot place
    // windows there. Tests set it elsewhere (setSystemMove).
    Q_PROPERTY(bool systemMove READ systemMove NOTIFY systemMoveChanged FINAL)
    // The width of the floating window's drawn shadow.
    Q_PROPERTY(int floatingShadow READ floatingShadow CONSTANT FINAL)
    Q_PROPERTY(int resizeGrip READ resizeGrip CONSTANT FINAL)
    // Bumped when a panel's header description changes (setPanelHeader).
    Q_PROPERTY(int headerRevision READ headerRevision NOTIFY headersChanged FINAL)
    // The panel whose menu is open (Main.qml sets it), so its "⋯" button
    // shows as pressed.
    Q_PROPERTY(QString openMenu READ openMenu WRITE setOpenMenu NOTIFY openMenuChanged FINAL)
public:
    using QObject::QObject;

    // Whether the platform needs the compositor to move windows (Wayland).
    static bool platformNeedsSystemMove();
    bool systemMove() const { return m_systemMove; }
    // Tests: the Wayland header on another platform.
    void setSystemMove(bool on);
    static int floatingShadow() { return dockchrome::floatingShadow(); }
    static int resizeGrip() { return dockchrome::kResizeGrip; }
    int headerRevision() const { return m_revision; }
    QString openMenu() const { return m_openMenu; }
    void setOpenMenu(const QString &uniqueName);

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
    // on a header the engine does not see: Wayland's moving header).
    Q_INVOKABLE bool toggleFloating(const QString &uniqueName);
    // Has the compositor move the window holding `from` (Wayland's moving
    // header); whether the platform took it. systemMoveRequested says so to
    // tests on platforms that do not.
    Q_INVOKABLE bool startSystemMove(QQuickItem *from);

signals:
    void headersChanged();
    void systemMoveChanged();
    void systemMoveRequested(QWindow *window);
    void menuRequested(const QString &uniqueName, QQuickItem *anchor);
    void openMenuChanged();

private:
    struct Header {
        bool horizontal = false;
        QPointer<QQuickItem> toolbar;
    };
    QHash<QString, Header> m_headers;
    int m_revision = 0;
    bool m_systemMove = platformNeedsSystemMove();
    QString m_openMenu;
};

} // namespace hikari::ui
