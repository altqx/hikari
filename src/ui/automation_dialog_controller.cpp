#include "automation_dialog_controller.h"

#include <QColor>

#include <algorithm>
#include <cmath>

namespace hikari::ui {

using application::DialogResult;
using application::DialogValueType;

namespace {

bool isColour(const std::string &kind)
{
    return kind == "color" || kind == "coloralpha";
}

// The helper's legacy hex: #RRGGBB, or #TTRRGGBB with TT = ASS transparency.
QColor colourOf(const QString &legacy)
{
    const QString hex = legacy.mid(1);
    bool ok = false;
    const uint v = hex.toUInt(&ok, 16);
    if (!ok)
        return QColor(Qt::black);
    if (hex.size() == 8)
        return QColor(int((v >> 16) & 0xff), int((v >> 8) & 0xff), int(v & 0xff), 255 - int(v >> 24));
    return QColor(int((v >> 16) & 0xff), int((v >> 8) & 0xff), int(v & 0xff));
}

QString legacyHexOf(const QColor &c, bool alpha)
{
    const int transparency = 255 - c.alpha();
    if (alpha && transparency)
        return QString::asprintf("#%02X%02X%02X%02X", transparency, c.red(), c.green(), c.blue());
    return QString::asprintf("#%02X%02X%02X", c.red(), c.green(), c.blue());
}

} // namespace

AutomationDialogController::AutomationDialogController(QObject *parent) : QObject(parent) {}

void AutomationDialogController::present(const QString &title, const application::DialogRequest &request,
                                         Reply reply)
{
    m_request = request;
    m_reply = std::move(reply);
    m_title = title;
    m_controls.clear();
    m_columns = 1;
    for (const auto &c : request.controls) {
        QStringList items;
        for (const auto &item : c.items)
            items << QString::fromStdString(item);
        m_controls << QVariantMap{
            {QStringLiteral("kind"), QString::fromStdString(c.kind)},
            {QStringLiteral("name"), QString::fromStdString(c.name)},
            {QStringLiteral("hint"), QString::fromStdString(c.hint)},
            {QStringLiteral("x"), std::max(0, c.x)},
            {QStringLiteral("y"), std::max(0, c.y)},
            {QStringLiteral("width"), std::max(1, c.width)},
            {QStringLiteral("height"), std::max(1, c.height)},
            {QStringLiteral("label"), QString::fromStdString(c.label)},
            {QStringLiteral("text"), QString::fromStdString(c.text)},
            {QStringLiteral("intValue"), c.intValue},
            {QStringLiteral("intMin"), c.intMin},
            {QStringLiteral("intMax"), c.intMax},
            // Legacy NumCtrl clamps the shown value to its range.
            {QStringLiteral("number"), std::clamp(c.number, c.numberMin, c.numberMax)},
            {QStringLiteral("color"), isColour(c.kind) ? colourOf(QString::fromStdString(c.text)) : QColor()},
            {QStringLiteral("numberMin"), c.numberMin},
            {QStringLiteral("numberMax"), c.numberMax},
            {QStringLiteral("checked"), c.checked},
            {QStringLiteral("items"), items},
        };
        m_columns = std::max(m_columns, std::max(0, c.x) + std::max(1, c.width));
    }
    m_buttons.clear();
    if (request.buttons.empty())
        m_buttons << tr("OK") << tr("Cancel");
    for (const auto &b : request.buttons)
        m_buttons << QString::fromStdString(b);
    emit changed();
}

void AutomationDialogController::withdraw()
{
    if (!m_reply)
        return;
    m_reply = nullptr;
    emit changed();
}

void AutomationDialogController::finish(int pressed, const QVariantList &values)
{
    if (!m_reply)
        return;
    DialogResult result = application::initialDialogResult(m_request);
    result.pressed = pressed >= 0 && pressed < m_buttons.size() ? pressed : -1;
    for (std::size_t i = 0; i < m_request.controls.size() && i < static_cast<std::size_t>(values.size()); ++i) {
        const auto &c = m_request.controls[i];
        const QVariant &v = values[static_cast<qsizetype>(i)];
        switch (application::dialogValueType(c.kind)) {
        case DialogValueType::None:
            break;
        case DialogValueType::Text:
            if (isColour(c.kind) && v.canConvert<QColor>() && v.metaType().id() == QMetaType::QColor)
                result.values[i] = legacyHexOf(v.value<QColor>(), c.kind == "coloralpha").toStdString();
            else
                result.values[i] = v.toString().toStdString();
            break;
        case DialogValueType::Integer: {
            bool ok = false;
            // Legacy NumCtrl: a comma is a decimal point; the value is clamped
            // and an integer truncates toward zero; unparseable text keeps
            // the value it had.
            const double d = v.toString().replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
            if (ok && std::isfinite(d))
                result.values[i] = static_cast<int>(std::clamp(d, double(c.intMin), double(c.intMax)));
            break;
        }
        case DialogValueType::Number: {
            bool ok = false;
            const double d = v.toString().replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
            if (ok && std::isfinite(d))
                result.values[i] = std::clamp(d, c.numberMin, c.numberMax);
            break;
        }
        case DialogValueType::Boolean:
            result.values[i] = v.toBool();
            break;
        }
    }
    Reply reply = std::move(m_reply);
    m_reply = nullptr;
    emit changed();
    reply(std::move(result));
}

} // namespace hikari::ui
