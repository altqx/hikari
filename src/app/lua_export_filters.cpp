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

template <class Start, class Finish>
bool LuaExportFilters::start(const std::string &scriptFile, const std::string &filterName, Start begin, Finish finish)
{
    const auto target = find(scriptFile, filterName);
    if (!target || m_shell.busy())
        return false;
    LuaScriptHost *host = m_shell.manager().host(target->path);
    if (!host)
        return false;
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = QObject::connect(host, &LuaScriptHost::finished, host,
                                   [host, connection, finish](LuaScriptHost::RunOutcome outcome, const QString &message) {
                                       QObject::disconnect(*connection);
                                       const std::string error =
                                           (message.isEmpty() ? host->lastError() : message).toStdString();
                                       finish(*host, outcome == LuaScriptHost::RunOutcome::Ok, error);
                                   });
    if (!begin(target->path, target->index)) {
        QObject::disconnect(*connection);
        return false;
    }
    return true;
}

bool LuaExportFilters::run(const std::string &scriptFile, const std::string &filterName,
                           const application::MacroSnapshot &snapshot, const application::DialogRequest &settings,
                           Done done)
{
    return start(
        scriptFile, filterName,
        [&](const std::string &path, int index) { return m_shell.manager().runFilter(path, index, snapshot, settings); },
        [done](const LuaScriptHost &host, bool ok, const std::string &error) {
            if (ok && host.lastResult())
                done(*host.lastResult());
            else
                done(std::unexpected(error));
        });
}

bool LuaExportFilters::configure(const std::string &scriptFile, const std::string &filterName,
                                 const application::MacroSnapshot &snapshot, DialogDone done)
{
    return start(
        scriptFile, filterName,
        [&](const std::string &path, int index) { return m_shell.manager().filterConfig(path, index, snapshot); },
        [done](const LuaScriptHost &host, bool ok, const std::string &error) {
            if (ok && host.lastDialog())
                done(*host.lastDialog());
            else
                done(std::unexpected(error));
        });
}

}
