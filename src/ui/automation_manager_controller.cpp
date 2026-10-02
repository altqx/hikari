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
                                 {QStringLiteral("macros"), macros}};
        m_busy = m_busy || s.state == ScriptStatus::State::Running;
    }
    emit changed();
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
