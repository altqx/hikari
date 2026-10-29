#pragma once

#include <functional>
#include <optional>

class QWindow;

namespace hikari::ui::titlebars {

bool themed(const QWindow *window);

// Qt sets the frame again when the application palette changes, so callers set it again then.
void setDark(QWindow *window, bool dark);

std::optional<bool> isDark(QWindow *window);

using Setter = std::function<void(QWindow *window, bool dark)>;
void overrideSetter(Setter setter);

}
