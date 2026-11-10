#pragma once

#include "hikari/api/api.h"

#include <QJSValue>
#include <QObject>
#include <QVariantMap>

#include <functional>
#include <optional>
#include <string>

class QJSEngine;

namespace hikari::app {

// A QML panel's `api`, the only supported way from a Full trust plugin's QML into HikariSub (docs/plugins.md).
class PluginPanelApi : public QObject {
    Q_OBJECT
    // The plugin's settings by id, as hikari.settings.get gives them.
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
    // text, muted, accent, panel, field, line, warning and danger.
    Q_PROPERTY(QVariantMap theme READ theme NOTIFY themeChanged)
    // HikariSub's language: "en", "ko", "pl", …
    Q_PROPERTY(QString locale READ locale CONSTANT)

public:
    struct Hooks {
        std::function<void(const std::string &operation, const api::Json &params, std::function<void(api::Result)> done)> call;
        std::function<void(api::Json data)> send;
        // Why the manifest refuses the value, or nothing once it is kept.
        std::function<std::optional<std::string>(const std::string &setting, const api::Json &value)> setSetting;
    };

    PluginPanelApi(QJSEngine *engine, Hooks hooks, QVariantMap settings, QString locale, QObject *parent = nullptr);

    QVariantMap settings() const { return m_settings; }
    QVariantMap theme() const;
    QString locale() const { return m_locale; }

    // A Promise of the operation's result, made as the plugin; it rejects with an Error that has a `code`.
    Q_INVOKABLE QJSValue call(const QString &operation, const QJSValue &params = QJSValue());
    // A {kind = "message", data} event for the plugin's handler of this panel.
    Q_INVOKABLE void send(const QJSValue &data);
    // Throws when the manifest's rules refuse the value.
    Q_INVOKABLE void setSetting(const QString &id, const QJSValue &value);

    void setSettings(QVariantMap settings);
    void receive(const api::Json &data);

signals:
    // hikari.panels.post from the plugin's helper.
    void received(const QVariant &data);
    void settingsChanged();
    void themeChanged();

private:
    QJSEngine *m_engine;
    Hooks m_hooks;
    QVariantMap m_settings;
    QString m_locale;
};

}
