#include "window_menus_closed.h"

#include "window_frame.h"

#include <QTimer>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

WindowMenusClosed::WindowMenusClosed(hikari::ui::MainWindowFrame *frame)
{
#ifdef _WIN32
    m_connection = QObject::connect(frame, &hikari::ui::MainWindowFrame::windowMenuRequested, frame,
                                    [] { QTimer::singleShot(100, [] { EndMenu(); }); });
#else
    Q_UNUSED(frame);
#endif
}

WindowMenusClosed::~WindowMenusClosed() { QObject::disconnect(m_connection); }
