#pragma once

#include <QMetaObject>

namespace hikari::ui {
class MainWindowFrame;
}

// On Windows the window menu is TrackPopupMenu, whose own loop blocks the test until the menu closes.
class WindowMenusClosed {
public:
    explicit WindowMenusClosed(hikari::ui::MainWindowFrame *frame);
    ~WindowMenusClosed();
    WindowMenusClosed(const WindowMenusClosed &) = delete;
    WindowMenusClosed &operator=(const WindowMenusClosed &) = delete;

private:
    QMetaObject::Connection m_connection;
};
