#include "hikari/app/plugin_panel_api.h"

#include "plugin_variant.h"
#include "theme.h"

#include <QJSEngine>
#include <QPointer>

namespace hikari::app {

namespace {

api::Json json(const QJSValue &value)
{
    return value.isUndefined() ? api::Json() : fromVariant(value.toVariant(QJSValue::ConvertJSObjects));
}

}

PluginPanelApi::PluginPanelApi(QJSEngine *engine, Hooks hooks, QVariantMap settings, QString locale, QObject *parent)
    : QObject(parent), m_engine(engine), m_hooks(std::move(hooks)), m_settings(std::move(settings)),
      m_locale(locale.isEmpty() ? QStringLiteral("en") : std::move(locale))
{
    ui::theme::onChanged(this, [this] { emit themeChanged(); });
}

QVariantMap PluginPanelApi::theme() const
{
    const ui::theme::Roles &r = ui::theme::current().roles;
    return {{QStringLiteral("text"), r.text},       {QStringLiteral("muted"), r.muted}, {QStringLiteral("accent"), r.accent},
            {QStringLiteral("panel"), r.panel},     {QStringLiteral("field"), r.field}, {QStringLiteral("line"), r.line},
            {QStringLiteral("warning"), r.warning}, {QStringLiteral("danger"), r.danger}};
}

QJSValue PluginPanelApi::call(const QString &operation, const QJSValue &params)
{
    QJSValue deferred = m_engine->evaluate(QStringLiteral(
        "(function () { const d = {}; d.promise = new Promise((resolve, reject) => { d.resolve = resolve; d.reject = reject }); return d })()"));
    api::Json body = json(params);
    if (body.isNull())
        body = api::Json::object();
    m_hooks.call(operation.toStdString(), body, [self = QPointer(this), deferred](api::Result result) mutable {
        if (!self)
            return;
        if (result) {
            deferred.property(QStringLiteral("resolve")).call({self->m_engine->toScriptValue(toVariant(*result))});
            return;
        }
        QJSValue error = self->m_engine->newErrorObject(QJSValue::GenericError, QString::fromStdString(result.error().message));
        error.setProperty(QStringLiteral("code"), QString::fromStdString(result.error().code));
        deferred.property(QStringLiteral("reject")).call({error});
    });
    return deferred.property(QStringLiteral("promise"));
}

void PluginPanelApi::send(const QJSValue &data)
{
    m_hooks.send(json(data));
}

void PluginPanelApi::setSetting(const QString &id, const QJSValue &value)
{
    if (const auto refused = m_hooks.setSetting(id.toStdString(), json(value)))
        m_engine->throwError(QString::fromStdString(*refused));
}

void PluginPanelApi::setSettings(QVariantMap settings)
{
    if (settings == m_settings)
        return;
    m_settings = std::move(settings);
    emit settingsChanged();
}

void PluginPanelApi::receive(const api::Json &data)
{
    emit received(toVariant(data));
}

}
