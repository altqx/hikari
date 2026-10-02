#pragma once

// The automation manager tool's presenter (L1): scripts in load order, their
// macros in registration order with the legacy filename:ordinal alias, load,
// reload, unload and run. A reload is visible: the script's state goes back
// to loading while its top level runs again in a new helper.

#include "hikari/application/automation.h"

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace hikari::ui {

class AutomationManagerController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application composition")
    // One map per script: path, fileName, name, description, state, error,
    // generation, macros (maps: ordinal, name, description, alias).
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

signals:
    void changed();

private:
    void refresh();

    application::AutomationServicePort &m_service;
    QVariantList m_scripts;
    bool m_busy = false;
};

} // namespace hikari::ui
