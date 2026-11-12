#pragma once

#include "hikari/api/host.h"
#include "hikari/api/json.h"

#include <QObject>
#include <QString>

#include <functional>
#include <optional>

namespace hikari::app {

class AutomationShell;

// Saves AI-written Lua the user approved to the AI scripts folder, loads it apart from Autoload and the
// Document's scripts, runs one macro as an ordinary Automation run and unloads it.
class AiScripts final : public QObject, public api::AutomationHost {
    Q_OBJECT

public:
    AiScripts(AutomationShell &shell, QString folder, std::function<bool()> hasDocument, QObject *parent = nullptr);

    api::LuaStart run(const api::LuaScript &script, std::function<void(api::LuaRun)> done) override;
    // The DependencyControl modules `script` asks for: [{module, version?, installed?}], installed holding the
    // version found, or "" when it has none on record.
    api::Json needs(const std::string &script) const;
    QString folder() const { return m_folder; }

private:
    struct Pending {
        std::string path;
        std::string macro;
        std::function<void(api::LuaRun)> done;
        bool started = false;
    };
    void scriptsChanged();
    void finish(std::string outcome, QString message);

    AutomationShell &m_shell;
    QString m_folder;
    std::function<bool()> m_hasDocument;
    std::optional<Pending> m_pending;
};

}
