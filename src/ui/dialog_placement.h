#pragma once

// DG1 (#222): where a dialog (DialogWindow.qml) opens. The first time it is
// centred on the window that opened it, 20 lower, kept 20 inside that
// screen's free area and shrunk to fit (MuseScore 4's rule,
// docs/research/musescore-shell.md §4 "Placement"); after that where it was
// left and as big as it was left, per dialog. A fixed-size dialog keeps its
// position only. A place no longer on any screen (a monitor unplugged) falls
// back to the first rule. On Wayland the compositor places windows, so only
// the size is kept.
//
// The places are one setting, "workspace.dialogGeometry": a row
// "<id>\t<x>,<y>,<width>,<height>" per dialog, in the window system's
// logical coordinates of the window without its frame.

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
// Below the opener's centre (MuseScore's DIALOG_WINDOW_FRAME_HEIGHT).
inline constexpr int kDrop = 20;
// Kept inside the screen's free area.
inline constexpr int kMargin = 20;
// A remembered place is on a screen while this much of its top edge is
// (the part the window is moved by).
inline constexpr int kGrip = 64;

// The first open: `size` centred on `opener` (its frame), kDrop lower,
// inside `available` by kMargin, shrunk to fit; `margins` is the window
// system's frame round the window (none while it draws its own).
QRect centred(const QRect &opener, QSize size, const QRect &available, const QMargins &margins = {});

// A remembered place on one of `screens` (their free areas), with the
// size it is opened at: the remembered one for a resizable dialog (no
// smaller than `minimum`), else `size`. Moved whole onto the screen it is
// on; nullopt when none of its top edge is on a screen.
std::optional<QRect> restored(const QRect &saved, QSize size, QSize minimum, bool resizable,
                              const QList<QRect> &screens, const QMargins &margins = {});

// The row for `id` in the setting's rows, and the rows with it replaced.
std::optional<QRect> find(const QStringList &rows, const QString &id);
QStringList replaced(QStringList rows, const QString &id, const QRect &geometry);

} // namespace dialogplacement

class DialogPlacement : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickWindow *window READ window WRITE setWindow NOTIFY windowChanged FINAL)
    // The dialog's id in the setting ("about", "credits", ...); empty: the
    // dialog is centred on every open and nothing is kept.
    Q_PROPERTY(QString dialogId READ dialogId WRITE setDialogId NOTIFY dialogIdChanged FINAL)
    Q_PROPERTY(hikari::ui::SettingsStore *settings READ settings WRITE setSettings NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool resizable READ resizable WRITE setResizable NOTIFY resizableChanged FINAL)
    // The most of the screen the dialog's content can take where it opens
    // (place()): its free area less the margins and the frame. The
    // dialog's natural size is its smallest only up to this.
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

    // The window a dialog opened now opens from: the one with the keyboard,
    // else the dialog's transient parent.
    Q_INVOKABLE QWindow *opener() const;
    // Sets the dialog's geometry for an open from `opener` (nullptr: the
    // screen); call it before the dialog is shown. `content` is the item
    // whose implicit size is the dialog's natural one: it is laid out at
    // the width it opens at on the screen it opens on first (wrapped text
    // takes its height there).
    Q_INVOKABLE void place(QWindow *opener, QQuickItem *content);
    // Keeps a shown dialog on its screen after its frame changed (the
    // title bar setting): the room for the new frame, and the dialog
    // moved and sized into it.
    Q_INVOKABLE void refit();
    // Whether the dialog's next show leaves the keyboard where it is (a
    // notice the program raises by itself, while another window or
    // program may be in use).
    Q_INVOKABLE void showWithoutActivating(bool quiet);
    // A minimised shown dialog back as it was before.
    Q_INVOKABLE void unminimize();
    // Keeps where the dialog is and how big (as it is hidden), unless it
    // was maximised or full screen.
    Q_INVOKABLE void remember();
    // Whether `item` is in a popup (a menu, a popover): the keyboard goes
    // back to the window's own focus item when the popup closes, not to it.
    Q_INVOKABLE static bool inPopup(QQuickItem *item);
    // Lays out `item`'s tree now, innermost first. A hidden window does not
    // polish: a language or font change since the dialog was last shown has
    // not reached its layouts' implicit size until this.
    Q_INVOKABLE static void settle(QQuickItem *item);

    // Tests: as on Wayland (only sizes are kept) or not; nullopt: the platform.
    static void overrideWayland(std::optional<bool> wayland);
    // AN1 (#404): told each dialog's id as it opens (place()); unset: no one.
    static void setOpenedHook(std::function<void(const QString &dialogId)> hook);

signals:
    void windowChanged();
    void dialogIdChanged();
    void settingsChanged();
    void resizableChanged();
    void roomChanged();

private:
    // The room on `on` for a frame of `margins`, and the content laid out
    // at the width the dialog opens at there.
    void fitRoom(QScreen *on, const QMargins &margins);
    QSize natural() const;

    QPointer<QQuickWindow> m_window;
    QPointer<QQuickItem> m_content;
    QString m_dialogId;
    QPointer<SettingsStore> m_settings;
    bool m_resizable = false;
    QSize m_room{16777215, 16777215}; // (QWINDOWSIZE_MAX)
    // How the dialog was last shown: it is hidden by the time remember() runs.
    QWindow::Visibility m_shown = QWindow::Windowed;
};

} // namespace hikari::ui
