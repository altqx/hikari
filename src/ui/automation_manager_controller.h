#pragma once

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
    Q_INVOKABLE void setEnabled(const QString &path, bool enabled);

    void setDocumentScript(std::function<bool(const std::string &)> isDocumentScript);
    void setAutoload(std::function<QStringList()> disabled, std::function<void(const QString &, bool)> setEnabled);
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

}
