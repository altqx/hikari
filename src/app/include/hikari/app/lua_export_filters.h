#pragma once

#include "hikari/application/export_steps.h"

#include <QPointer>

namespace hikari::app {

class AutomationShell;
}

namespace hikari::backends {
class LuaScriptHost;
}

namespace hikari::app {

// Export as's Lua steps, run by the loaded scripts' helpers while no macro runs.
class LuaExportFilters : public application::ExportFilterPort {
public:
    explicit LuaExportFilters(AutomationShell &shell) : m_shell(shell) {}

    bool available(const std::string &scriptFile, const std::string &filterName) const override;
    bool run(const std::string &scriptFile, const std::string &filterName, const application::MacroSnapshot &snapshot,
             const application::DialogRequest &settings, Done done) override;
    bool configure(const std::string &scriptFile, const std::string &filterName,
                   const application::MacroSnapshot &snapshot, DialogDone done) override;
    void stop() override;

private:
    template <class Start, class Finish> bool start(const std::string &scriptFile, const std::string &filterName,
                                                    Start begin, Finish finish);
    struct Target {
        std::string path;
        int index = -1;
    };
    std::optional<Target> find(const std::string &scriptFile, const std::string &filterName) const;

    AutomationShell &m_shell;
    QPointer<backends::LuaScriptHost> m_running;
};

}
