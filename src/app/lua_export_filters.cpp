#include "hikari/app/lua_export_filters.h"

#include "hikari/app/automation_shell.h"
#include "hikari/application/automation_registry.h"

#include <memory>

namespace hikari::app {

using application::ScriptStatus;
using backends::LuaScriptHost;

std::optional<LuaExportFilters::Target> LuaExportFilters::find(const std::string &scriptFile,
                                                               const std::string &filterName) const
{
    for (const ScriptStatus &script : m_shell.scripts()) {
        if (script.state != ScriptStatus::State::Ready && script.state != ScriptStatus::State::Running)
            continue;
        if (application::scriptFileName(script.path) != scriptFile)
            continue;
        const auto &filters = script.info.filters;
        for (std::size_t i = 0; i < filters.size(); ++i)
            if (filters[i].name == filterName)
                return Target{script.path, static_cast<int>(i)};
    }
    return std::nullopt;
}

bool LuaExportFilters::available(const std::string &scriptFile, const std::string &filterName) const
{
    return find(scriptFile, filterName).has_value();
}

bool LuaExportFilters::run(const std::string &scriptFile, const std::string &filterName,
                           const application::MacroSnapshot &snapshot, const application::DialogRequest &settings,
                           Done done)
{
    const auto target = find(scriptFile, filterName);
    if (!target || m_shell.busy())
        return false;
    LuaScriptHost *host = m_shell.manager().host(target->path);
    if (!host)
        return false;
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = QObject::connect(
        host, &LuaScriptHost::finished, host,
        [host, connection, done](LuaScriptHost::RunOutcome outcome, const QString &message) {
            QObject::disconnect(*connection);
            if (outcome == LuaScriptHost::RunOutcome::Ok && host->lastResult()) {
                done(*host->lastResult());
                return;
            }
            done(std::unexpected((message.isEmpty() ? host->lastError() : message).toStdString()));
        });
    if (!m_shell.manager().runFilter(target->path, target->index, snapshot, settings)) {
        QObject::disconnect(*connection);
        return false;
    }
    return true;
}

}
