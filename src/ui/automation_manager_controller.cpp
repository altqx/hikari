#include "automation_manager_controller.h"

#include <QFileInfo>

namespace hikari::ui {

using application::ScriptStatus;

namespace {

QString stateName(ScriptStatus::State s)
{
    switch (s) {
    case ScriptStatus::State::Loading: return QStringLiteral("loading");
    case ScriptStatus::State::Ready: return QStringLiteral("ready");
    case ScriptStatus::State::Running: return QStringLiteral("running");
    case ScriptStatus::State::LoadFailed: return QStringLiteral("failed");
    case ScriptStatus::State::Unavailable: return QStringLiteral("unavailable");
    }
    return {};
}

} // namespace

AutomationManagerController::AutomationManagerController(application::AutomationServicePort &service, QObject *parent)
    : QObject(parent), m_service(service)
{
    m_service.setObserver([this] { refresh(); });
    refresh();
}

void AutomationManagerController::refresh()
{
    m_scripts.clear();
    m_busy = false;
    for (const auto &s : m_service.scripts()) {
        const QString path = QString::fromStdString(s.path);
        const QString fileName = QFileInfo(path).fileName();
        QVariantList macros;
        for (std::size_t i = 0; i < s.info.macros.size(); ++i)
            macros << QVariantMap{{QStringLiteral("ordinal"), int(i)},
                                  {QStringLiteral("name"), QString::fromStdString(s.info.macros[i].name)},
                                  {QStringLiteral("description"), QString::fromStdString(s.info.macros[i].description)},
                                  {QStringLiteral("alias"), fileName + QLatin1Char(':') + QString::number(i)}};
        m_scripts << QVariantMap{{QStringLiteral("path"), path},
                                 {QStringLiteral("fileName"), fileName},
                                 {QStringLiteral("name"), QString::fromStdString(s.info.name)},
                                 {QStringLiteral("description"), QString::fromStdString(s.info.description)},
                                 {QStringLiteral("state"), stateName(s.state)},
                                 {QStringLiteral("error"), QString::fromStdString(s.error)},
                                 {QStringLiteral("generation"), qulonglong(s.generation)},
                                 {QStringLiteral("forceStopOffered"), s.forceStopOffered},
                                 {QStringLiteral("inDocument"), m_isDocumentScript && m_isDocumentScript(s.path)},
                                 {QStringLiteral("enabled"), true},
                                 {QStringLiteral("macros"), macros}};
        m_busy = m_busy || s.state == ScriptStatus::State::Running;
    }
    if (m_disabled)
        for (const QString &path : m_disabled())
            m_scripts << QVariantMap{{QStringLiteral("path"), path},
                                     {QStringLiteral("fileName"), QFileInfo(path).fileName()},
                                     {QStringLiteral("name"), QString()},
                                     {QStringLiteral("description"), QString()},
                                     {QStringLiteral("state"), QStringLiteral("disabled")},
                                     {QStringLiteral("error"), QString()},
                                     {QStringLiteral("generation"), qulonglong(0)},
                                     {QStringLiteral("forceStopOffered"), false},
                                     {QStringLiteral("inDocument"), false},
                                     {QStringLiteral("enabled"), false},
                                     {QStringLiteral("macros"), QVariantList()}};
    emit changed();
}

void AutomationManagerController::setDocumentScript(std::function<bool(const std::string &)> isDocumentScript)
{
    m_isDocumentScript = std::move(isDocumentScript);
    refresh();
}

void AutomationManagerController::setAutoload(std::function<QStringList()> disabled,
                                              std::function<void(const QString &, bool)> setEnabled)
{
    m_disabled = std::move(disabled);
    m_setEnabled = std::move(setEnabled);
    refresh();
}

void AutomationManagerController::setEnabled(const QString &path, bool enabled)
{
    if (m_setEnabled)
        m_setEnabled(path, enabled);
    refresh();
}

void AutomationManagerController::load(const QString &path)
{
    m_service.load(path.toStdString());
}

bool AutomationManagerController::reload(const QString &path)
{
    return m_service.reload(path.toStdString());
}

void AutomationManagerController::unload(const QString &path)
{
    m_service.unload(path.toStdString());
}

bool AutomationManagerController::run(const QString &path, int ordinal)
{
    return m_service.run(path.toStdString(), ordinal);
}

void AutomationManagerController::cancel()
{
    m_service.cancel();
}

} // namespace hikari::ui

namespace hikari::ui {

bool AutomationManagerController::forceStop(const QString &path)
{
    return m_service.forceStop(path.toStdString());
}

} // namespace hikari::ui
