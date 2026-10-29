#include "automation_dialog_controller.h"

#include <QCoreApplication>
#include <QColor>
#include <QCryptographicHash>
#include <QLocale>

#include <algorithm>
#include <cmath>

namespace hikari::ui {

using application::DialogResult;
using application::DialogValueType;

namespace {

constexpr int kGridCells = 256;

bool isColour(const std::string &kind)
{
    return kind == "color" || kind == "coloralpha";
}

// #RRGGBB, or #TTRRGGBB with TT the ASS transparency (255 - alpha).
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

QVariant shownOf(const application::DialogControl &c, const application::DialogValue &value)
{
    if (const auto *s = std::get_if<std::string>(&value))
        return isColour(c.kind) ? QVariant(colourOf(QString::fromStdString(*s))) : QVariant(QString::fromStdString(*s));
    if (const auto *i = std::get_if<int>(&value))
        return std::clamp(*i, c.intMin, c.intMax);
    if (const auto *d = std::get_if<double>(&value))
        return std::clamp(*d, c.numberMin, c.numberMax);
    if (const auto *b = std::get_if<bool>(&value))
        return *b;
    return {};
}

application::DialogValue valueFrom(const application::DialogControl &c, const QVariant &v,
                                   const application::DialogValue &fallback)
{
    switch (application::dialogValueType(c.kind)) {
    case DialogValueType::None:
        return fallback;
    case DialogValueType::Text:
        if (isColour(c.kind) && v.canConvert<QColor>() && v.metaType().id() == QMetaType::QColor)
            return legacyHexOf(v.value<QColor>(), c.kind == "coloralpha").toStdString();
        return v.toString().toStdString();
    case DialogValueType::Integer: {
        bool ok = false;
        const double d = v.toString().replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
        if (ok && std::isfinite(d))
            return static_cast<int>(std::clamp(d, double(c.intMin), double(c.intMax)));
        return fallback;
    }
    case DialogValueType::Number: {
        bool ok = false;
        const double d = v.toString().replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
        if (ok && std::isfinite(d))
            return std::clamp(d, c.numberMin, c.numberMax);
        return fallback;
    }
    case DialogValueType::Boolean:
        return v.toBool();
    }
    return fallback;
}

}

AutomationDialogController::AutomationDialogController(QObject *parent) : QObject(parent) {}

void AutomationDialogController::present(const QString &title, const application::DialogRequest &request,
                                         Reply reply, const QString &source, Shown shown)
{
    m_request = request;
    m_reply = std::move(reply);
    m_title = title;
    m_memoryKey = shown.memoryKey;
    m_notice = shown.notice;
    m_shown = application::initialDialogResult(request);
    if (shown.values)
        for (std::size_t i = 0; i < m_shown.values.size() && i < shown.values->values.size(); ++i)
            if (const auto v = application::dialogValueOf(request.controls[i], shown.values->values[i]))
                m_shown.values[i] = *v;
    m_controls.clear();
    m_columns = 1;
    for (std::size_t at = 0; at < request.controls.size(); ++at) {
        application::DialogControl c = request.controls[at];
        const application::DialogValue &value = m_shown.values[at];
        if (const auto *s = std::get_if<std::string>(&value))
            c.text = *s;
        else if (const auto *i = std::get_if<int>(&value))
            c.intValue = *i;
        else if (const auto *d = std::get_if<double>(&value))
            c.number = *d;
        else if (const auto *b = std::get_if<bool>(&value))
            c.checked = *b;
        QStringList items;
        for (const auto &item : c.items)
            items << QString::fromStdString(item);
        m_controls << QVariantMap{
            {QStringLiteral("kind"), QString::fromStdString(c.kind)},
            {QStringLiteral("name"), QString::fromStdString(c.name)},
            {QStringLiteral("hint"), QString::fromStdString(c.hint)},
            {QStringLiteral("x"), std::clamp(c.x, 0, kGridCells - 1)},
            {QStringLiteral("y"), std::clamp(c.y, 0, kGridCells - 1)},
            {QStringLiteral("width"), std::clamp(c.width, 1, kGridCells)},
            {QStringLiteral("height"), std::clamp(c.height, 1, kGridCells)},
            {QStringLiteral("label"), QString::fromStdString(c.label)},
            {QStringLiteral("text"), QString::fromStdString(c.text)},
            {QStringLiteral("intValue"), c.intValue},
            {QStringLiteral("intMin"), c.intMin},
            {QStringLiteral("intMax"), c.intMax},
            {QStringLiteral("number"), std::clamp(c.number, c.numberMin, c.numberMax)},
            {QStringLiteral("color"), isColour(c.kind) ? colourOf(QString::fromStdString(c.text)) : QColor()},
            {QStringLiteral("numberMin"), c.numberMin},
            {QStringLiteral("numberMax"), c.numberMax},
            {QStringLiteral("step"), c.step},
            {QStringLiteral("checked"), c.checked},
            {QStringLiteral("items"), items},
        };
        m_columns = std::max(m_columns, std::clamp(c.x, 0, kGridCells - 1) + std::clamp(c.width, 1, kGridCells));
    }
    m_buttons.clear();
    if (request.buttons.empty())
        m_buttons << QCoreApplication::translate("HikariSub.Automation", "OK")
                  << QCoreApplication::translate("HikariSub.Automation", "Cancel");
    for (const auto &b : request.buttons)
        m_buttons << QString::fromStdString(b);
    // Aegisub's default button is the last ok, yes or save one (each makes itself wx's default).
    m_enterButton = 0;
    for (std::size_t i = 0; i < request.buttonIds.size() && i < request.buttons.size(); ++i) {
        const std::string &id = request.buttonIds[i];
        if (id == "ok" || id == "yes" || id == "save")
            m_enterButton = static_cast<int>(i);
    }
    m_dialogId.clear();
    if (!source.isEmpty()) {
        QCryptographicHash hash(QCryptographicHash::Sha1);
        hash.addData(source.toUtf8());
        for (const auto &c : request.controls) {
            hash.addData(QByteArrayView("\n"));
            hash.addData(QByteArray::fromStdString(c.kind + '\t' + c.name));
        }
        m_dialogId = QStringLiteral("automation.") + QString::fromLatin1(hash.result().toHex().left(16));
    }
    emit changed();
    emit memoryChanged();
}

void AutomationDialogController::withdraw()
{
    if (!m_reply)
        return;
    m_reply = nullptr;
    emit changed();
}

application::DialogResult AutomationDialogController::resultOf(int pressed, const QVariantList &values) const
{
    DialogResult result = m_shown;
    result.pressed = pressed >= 0 && pressed < m_buttons.size() ? pressed : -1;
    for (std::size_t i = 0; i < m_request.controls.size() && i < static_cast<std::size_t>(values.size()); ++i)
        result.values[i] = valueFrom(m_request.controls[i], values[static_cast<qsizetype>(i)], m_shown.values[i]);
    return result;
}

void AutomationDialogController::finish(int pressed, const QVariantList &values)
{
    if (!m_reply)
        return;
    DialogResult result = resultOf(pressed, values);
    Reply reply = std::move(m_reply);
    m_reply = nullptr;
    emit changed();
    reply(std::move(result));
}

bool AutomationDialogController::hasLastUsed() const
{
    return remembers() && m_memory->lastAnswer(m_memoryKey).has_value();
}

QStringList AutomationDialogController::presets() const
{
    return remembers() ? m_memory->presets(m_memoryKey) : QStringList();
}

QVariantList AutomationDialogController::shownValues(const application::NamedDialogValues &values) const
{
    QVariantList out;
    for (const auto &c : m_request.controls) {
        const auto it = c.name.empty() ? values.end() : values.find(c.name);
        const auto v = it != values.end() ? application::acceptedDialogValue(c, it->second) : std::nullopt;
        out << (v ? shownOf(c, *v) : QVariant());
    }
    return out;
}

QVariantList AutomationDialogController::lastUsedValues() const
{
    const auto last = remembers() ? m_memory->lastAnswer(m_memoryKey) : std::nullopt;
    return last ? shownValues(last->answered) : QVariantList();
}

QVariantList AutomationDialogController::presetValues(const QString &name) const
{
    const auto preset = remembers() ? m_memory->preset(m_memoryKey, name) : std::nullopt;
    return preset ? shownValues(*preset) : QVariantList();
}

QVariantList AutomationDialogController::scriptValues() const
{
    QVariantList out;
    const DialogResult script = application::initialDialogResult(m_request);
    for (std::size_t i = 0; i < m_request.controls.size(); ++i)
        out << shownOf(m_request.controls[i], script.values[i]);
    return out;
}

bool AutomationDialogController::differs(int index, const QVariant &value) const
{
    if (index < 0 || static_cast<std::size_t>(index) >= m_request.controls.size())
        return false;
    const auto &c = m_request.controls[static_cast<std::size_t>(index)];
    if (application::dialogValueType(c.kind) == DialogValueType::None)
        return false;
    const auto script = application::initialDialogResult(m_request).values[static_cast<std::size_t>(index)];
    return valueFrom(c, value, script) != valueFrom(c, shownOf(c, script), script);
}

QString AutomationDialogController::scriptValueText(int index) const
{
    if (index < 0 || static_cast<std::size_t>(index) >= m_request.controls.size())
        return {};
    const auto &c = m_request.controls[static_cast<std::size_t>(index)];
    const auto value = valueFrom(c, shownOf(c, application::initialDialogResult(m_request).values[static_cast<std::size_t>(index)]), {});
    if (const auto *s = std::get_if<std::string>(&value))
        return s->empty() ? QCoreApplication::translate("HikariSub.Automation", "empty value") : QString::fromStdString(*s);
    if (const auto *i = std::get_if<int>(&value))
        return QString::number(*i);
    if (const auto *d = std::get_if<double>(&value))
        return QString::number(*d, 'g', QLocale::FloatingPointShortest);
    if (const auto *b = std::get_if<bool>(&value))
        return *b ? QCoreApplication::translate("HikariSub.Automation", "ticked")
                  : QCoreApplication::translate("HikariSub.Automation", "not ticked");
    return {};
}

void AutomationDialogController::savePreset(const QString &name, const QVariantList &values)
{
    if (!remembers() || name.trimmed().isEmpty())
        return;
    m_memory->savePreset(m_memoryKey, name.trimmed(), application::namedDialogValues(m_request, resultOf(-1, values)));
    emit memoryChanged();
}

void AutomationDialogController::deletePreset(const QString &name)
{
    if (!remembers())
        return;
    m_memory->deletePreset(m_memoryKey, name);
    emit memoryChanged();
}

}
