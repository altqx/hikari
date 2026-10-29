#include "number_field_mouse.h"

#include "hikari/application/editor_fields.h"

#include <cmath>
#include <limits>

namespace hikari::ui {

namespace {

application::NumberFieldRange rangeOf(double from, double to)
{
    return {static_cast<std::int64_t>(from), static_cast<std::int64_t>(to)};
}

}

double NumberFieldMouse::value(const QString &text) const
{
    const auto v = application::numberFieldValue(text.toStdU16String());
    return v ? static_cast<double>(*v) : std::numeric_limits<double>::quiet_NaN();
}

QVariantMap NumberFieldMouse::wheel(double value, int notches, double from, double to) const
{
    auto v = static_cast<std::int64_t>(value);
    const auto text = application::wheelNumberField(v, notches, rangeOf(from, to));
    return {{QStringLiteral("value"), static_cast<double>(v)},
            {QStringLiteral("text"), text ? QString::fromStdU16String(*text) : QString()}};
}

QVariantMap NumberFieldMouse::dragMove(const QVariantMap &drag, int x, int y, double from, double to) const
{
    application::NumberFieldDrag state{static_cast<std::int64_t>(drag.value(QStringLiteral("value")).toDouble()),
                                       drag.value(QStringLiteral("x")).toInt(), drag.value(QStringLiteral("y")).toInt()};
    const auto text = application::dragNumberField(state, x, y, rangeOf(from, to));
    return {{QStringLiteral("drag"), QVariantMap{{QStringLiteral("value"), static_cast<double>(state.value)},
                                                 {QStringLiteral("x"), state.x},
                                                 {QStringLiteral("y"), state.y}}},
            {QStringLiteral("text"), text ? QString::fromStdU16String(*text) : QString()}};
}

}
