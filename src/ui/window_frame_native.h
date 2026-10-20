#pragma once

// SH1: the window system's side of MainWindowFrame (window_frame.h). On
// Windows (window_frame_win.cpp) the frame MuseScore 4 draws: the window
// keeps its caption styles, so Windows still gives it the drop shadow, Aero
// Snap, Win+arrows and the system menu, and the whole window is client area
// (WM_NCCALCSIZE); WM_NCHITTEST hands the title bar's free parts, the
// maximise button and the application icon to the window system
// (MainWindowFrame::hitAt). Elsewhere Qt's frameless window is the whole of
// it, and there is no native frame.

#include <QPointF>

#include <memory>

class QQuickWindow;

namespace hikari::ui {
class MainWindowFrame;
}

namespace hikari::ui::windowframe {

class NativeFrame {
public:
    virtual ~NativeFrame() = default;
    // The frame is ours (true) or the system's; called again whenever Qt
    // may have set the window's styles (its flags or its native window).
    virtual void apply(bool custom) = 0;
    // The system menu at `position` (window coordinates).
    virtual bool showWindowMenu(QPointF position) = 0;
};

// The native side for `frame`'s `window`; nullptr where there is none.
std::unique_ptr<NativeFrame> createNativeFrame(MainWindowFrame *frame, QQuickWindow *window);

} // namespace hikari::ui::windowframe
