#pragma once

// The automation manager tool's presenter (L1): scripts in load order, their
// macros in registration order with the legacy filename:ordinal alias, load,
// reload, unload and run. A reload is visible: the script's state goes back
// to loading while its top level runs again in a new helper.

#include "hikari/application/automation.h"

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <string>

namespace hikari::ui {

class AutomationManagerController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application composition")
    // One map per script: path, fileName, name, description, state, error,
    // generation, forceStopOffered, inDocument (AM1: one of the Document
    // scripts, the rest being autoload's), enabled (#397: false for an
    // autoload script turned off, listed with state "disabled"), macros
    // (maps: ordinal, name, description, alias).
    Q_PROPERTY(QVariantList scripts READ scripts NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
public:
    explicit AutomationManagerController(application::AutomationServicePort &service, QObject *parent = nullptr);

    QVariantList scripts() const { return m_scripts; }
    bool busy() const { return m_busy; }

    Q_INVOKABLE void load(const QString &path);
    Q_INVOKABLE bool reload(const QString &path);
    Q_INVOKABLE void unload(const QString &path);
    Q_INVOKABLE bool run(const QString &path, int ordinal);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE bool forceStop(const QString &path);
    // #397: turns an autoload script on (it loads) or off (it unloads and
    // stays off at the next start).
    Q_INVOKABLE void setEnabled(const QString &path, bool enabled);

    // AM1: which loaded scripts are the Document scripts (the Script Info's
    // "Automation Scripts" and Load script); the panel lists them apart.
    void setDocumentScript(std::function<bool(const std::string &)> isDocumentScript);
    // #397: the autoload scripts turned off (paths, not loaded), and how one
    // is turned on or off.
    void setAutoload(std::function<QStringList()> disabled, std::function<void(const QString &, bool)> setEnabled);
    // Reads the scripts again (the list changes with every load, reload and
    // unload by itself).
    void refresh();

signals:
    void changed();

private:
    application::AutomationServicePort &m_service;
    std::function<bool(const std::string &)> m_isDocumentScript;
    std::function<QStringList()> m_disabled;
    std::function<void(const QString &, bool)> m_setEnabled;
    QVariantList m_scripts;
    bool m_busy = false;
};

} // namespace hikari::ui
