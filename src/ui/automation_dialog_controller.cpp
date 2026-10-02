#include "automation_dialog_controller.h"

#include <algorithm>
#include <cmath>

namespace hikari::ui {

using application::DialogResult;
using application::DialogValueType;

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
            {QStringLiteral("number"), c.number},
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
            result.values[i] = v.toString().toStdString();
            break;
        case DialogValueType::Integer: {
            bool ok = false;
            const double d = v.toDouble(&ok);
            if (ok && std::isfinite(d))
                result.values[i] = static_cast<int>(std::clamp(d, double(c.intMin), double(c.intMax)));
            break;
        }
        case DialogValueType::Number: {
            bool ok = false;
            const double d = v.toDouble(&ok);
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
