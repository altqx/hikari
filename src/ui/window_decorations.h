#pragma once

// On Wayland FramelessWindowHint only stops Qt asking for a frame; the compositor is asked via xdg-decoration.

#include <QObject>
#include <QStringList>

#include <optional>

class QWindow;

namespace hikari::ui::windowdecorations {

void requestNone(QWindow *window);
void release(QWindow *window);

QStringList compositorGlobalNames();

enum class SystemFrame {
    None,
    Border,
    TitleBar,
};

// Hyprland answers server_side to every window but draws only a border.
SystemFrame serverSideFrame(const QStringList &globals);

SystemFrame systemFrame(const QWindow *window);
bool hasSystemTitleBar(const QWindow *window);

class Notifier : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
signals:
    void changed(QWindow *window);
};
Notifier *notifier();

void overrideSystemFrame(QWindow *window, std::optional<SystemFrame> frame);

}
