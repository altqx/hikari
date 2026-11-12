#pragma once

#include "hikari/app/automation_reset.h"

#include <QObject>
#include <QVariantList>

#include <memory>

namespace hikari::app {

class AutomationShell;
class ScriptPackages;

// The one-time popup after an automation folder reset, and its Reinstall N scripts.
class AutomationResetNotice : public QObject {
    Q_OBJECT
    // Whether the popup shows at this start.
    Q_PROPERTY(bool due READ due CONSTANT)
    // "moved" or "failed".
    Q_PROPERTY(QString outcome READ outcome CONSTANT)
    Q_PROPERTY(QString backup READ backup CONSTANT)
    Q_PROPERTY(QStringList moved READ moved CONSTANT)
    Q_PROPERTY(QString reason READ reason CONSTANT)
    // Each: {nameSpace, name, command} for a script now built in, command its translated name.
    Q_PROPERTY(QVariantList builtIn READ builtIn CONSTANT)
    // Each: {nameSpace, name, state, error}, state as ScriptPackages::entries gives it.
    Q_PROPERTY(QVariantList scripts READ scripts NOTIFY changed)
    // The scripts not yet asked for: what Reinstall N scripts queues.
    Q_PROPERTY(int reinstallCount READ reinstallCount NOTIFY changed)
    Q_PROPERTY(bool reinstalling READ reinstalling NOTIFY changed)
public:
    // `bundledAutoload` is the shipped Autoload folder.
    AutomationResetNotice(AutomationReset reset, AutomationShell &shell, const QString &bundledAutoload,
                          QObject *parent = nullptr);
    ~AutomationResetNotice() override;

    bool due() const;
    QString outcome() const;
    QString backup() const;
    QStringList moved() const { return m_reset.moved; }
    QString reason() const { return m_reset.reason; }
    QVariantList builtIn() const { return m_builtIn; }
    QVariantList scripts() const;
    int reinstallCount() const;
    bool reinstalling() const { return m_reinstalling; }

    Q_INVOKABLE void reinstall();
    Q_INVOKABLE void retry(const QString &nameSpace);
    Q_INVOKABLE bool openBackup() const;

signals:
    void changed();

private:
    AutomationReset m_reset;
    QVariantList m_builtIn;
    std::unique_ptr<ScriptPackages> m_packages;
    bool m_reinstalling = false;
};

}
