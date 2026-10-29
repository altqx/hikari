#pragma once

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
    // Called again whenever Qt may have reset the window's styles (its flags or its native window).
    virtual void apply(bool custom) = 0;
    virtual bool showWindowMenu(QPointF position) = 0;
};

std::unique_ptr<NativeFrame> createNativeFrame(MainWindowFrame *frame, QQuickWindow *window);

}
