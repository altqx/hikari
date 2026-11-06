#include "hikari/backends/ai_config_store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace hikari::backends::ai {

namespace app = hikari::application::ai;

namespace {

QJsonObject capabilitiesJson(const app::Capabilities &c)
{
    return {{QStringLiteral("tools"), c.tools}, {QStringLiteral("vision"), c.vision}, {QStringLiteral("video"), c.video}};
}

app::Capabilities capabilitiesFrom(const QJsonObject &o)
{
    return {.tools = o.value(QStringLiteral("tools")).toBool(),
            .vision = o.value(QStringLiteral("vision")).toBool(),
            .video = o.value(QStringLiteral("video")).toBool()};
}

}

app::Config JsonConfigStore::load()
{
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    app::Config config;
    for (const QJsonValue &v : root.value(QStringLiteral("providers")).toArray()) {
        const QJsonObject o = v.toObject();
        const auto preset = app::presetFromId(o.value(QStringLiteral("preset")).toString().toStdString());
        const auto wire = app::wireFromId(o.value(QStringLiteral("wire")).toString().toStdString());
        const std::string id = o.value(QStringLiteral("id")).toString().toStdString();
        if (!preset || !wire || id.empty())
            continue;
        app::Provider p;
        p.id = id;
        p.name = o.value(QStringLiteral("name")).toString().toStdString();
        p.preset = *preset;
        p.wire = *wire;
        p.baseUrl = o.value(QStringLiteral("baseUrl")).toString().toStdString();
        p.needsKey = o.value(QStringLiteral("needsKey")).toBool(true);
        p.signedIn = o.value(QStringLiteral("signedIn")).toBool(false);
        p.defaults = capabilitiesFrom(o.value(QStringLiteral("defaults")).toObject());
        for (const QJsonValue &mv : o.value(QStringLiteral("models")).toArray()) {
            const QJsonObject m = mv.toObject();
            app::Model model;
            model.id = m.value(QStringLiteral("id")).toString().toStdString();
            model.name = m.value(QStringLiteral("name")).toString().toStdString();
            if (m.contains(QStringLiteral("capabilities")))
                model.capabilities = capabilitiesFrom(m.value(QStringLiteral("capabilities")).toObject());
            if (m.contains(QStringLiteral("price"))) {
                const QJsonObject price = m.value(QStringLiteral("price")).toObject();
                model.price = app::Price{.input = price.value(QStringLiteral("input")).toDouble(),
                                         .output = price.value(QStringLiteral("output")).toDouble()};
                if (price.contains(QStringLiteral("cachedInput")))
                    model.price->cachedInput = price.value(QStringLiteral("cachedInput")).toDouble();
                if (price.contains(QStringLiteral("cacheWrite")))
                    model.price->cacheWrite = price.value(QStringLiteral("cacheWrite")).toDouble();
                if (price.contains(QStringLiteral("cacheWriteHour")))
                    model.price->cacheWriteHour = price.value(QStringLiteral("cacheWriteHour")).toDouble();
            }
            if (m.contains(QStringLiteral("contextTokens")))
                model.contextTokens = m.value(QStringLiteral("contextTokens")).toInteger();
            p.models.push_back(std::move(model));
        }
        const QJsonObject overrides = o.value(QStringLiteral("capabilityOverrides")).toObject();
        for (auto it = overrides.begin(); it != overrides.end(); ++it)
            p.capabilityOverrides[it.key().toStdString()] = capabilitiesFrom(it.value().toObject());
        config.providers.push_back(std::move(p));
    }
    const QJsonObject roles = root.value(QStringLiteral("roles")).toObject();
    for (auto it = roles.begin(); it != roles.end(); ++it) {
        const auto role = app::roleFromId(it.key().toStdString());
        const QJsonObject ref = it.value().toObject();
        if (role)
            config.roles[*role] = {ref.value(QStringLiteral("provider")).toString().toStdString(),
                                   ref.value(QStringLiteral("model")).toString().toStdString()};
    }
    return config;
}

bool JsonConfigStore::save(const app::Config &config)
{
    QJsonArray providers;
    for (const app::Provider &p : config.providers) {
        QJsonArray models;
        for (const app::Model &m : p.models) {
            QJsonObject model{{QStringLiteral("id"), QString::fromStdString(m.id)},
                              {QStringLiteral("name"), QString::fromStdString(m.name)}};
            if (m.capabilities)
                model.insert(QStringLiteral("capabilities"), capabilitiesJson(*m.capabilities));
            if (m.price) {
                QJsonObject price{{QStringLiteral("input"), m.price->input}, {QStringLiteral("output"), m.price->output}};
                if (m.price->cachedInput)
                    price.insert(QStringLiteral("cachedInput"), *m.price->cachedInput);
                if (m.price->cacheWrite)
                    price.insert(QStringLiteral("cacheWrite"), *m.price->cacheWrite);
                if (m.price->cacheWriteHour)
                    price.insert(QStringLiteral("cacheWriteHour"), *m.price->cacheWriteHour);
                model.insert(QStringLiteral("price"), price);
            }
            if (m.contextTokens)
                model.insert(QStringLiteral("contextTokens"), static_cast<qint64>(*m.contextTokens));
            models.append(model);
        }
        QJsonObject overrides;
        for (const auto &[model, capabilities] : p.capabilityOverrides)
            overrides.insert(QString::fromStdString(model), capabilitiesJson(capabilities));
        providers.append(QJsonObject{
            {QStringLiteral("id"), QString::fromStdString(p.id)},
            {QStringLiteral("name"), QString::fromStdString(p.name)},
            {QStringLiteral("preset"), QString::fromUtf8(app::presetId(p.preset))},
            {QStringLiteral("wire"), QString::fromUtf8(app::wireId(p.wire))},
            {QStringLiteral("baseUrl"), QString::fromStdString(p.baseUrl)},
            {QStringLiteral("needsKey"), p.needsKey},
            {QStringLiteral("signedIn"), p.signedIn},
            {QStringLiteral("defaults"), capabilitiesJson(p.defaults)},
            {QStringLiteral("models"), models},
            {QStringLiteral("capabilityOverrides"), overrides},
        });
    }
    QJsonObject roles;
    for (const auto &[role, ref] : config.roles)
        roles.insert(QString::fromUtf8(app::roleId(role)),
                     QJsonObject{{QStringLiteral("provider"), QString::fromStdString(ref.providerId)},
                                 {QStringLiteral("model"), QString::fromStdString(ref.modelId)}});
    const QJsonObject root{{QStringLiteral("version"), 1},
                           {QStringLiteral("providers"), providers},
                           {QStringLiteral("roles"), roles}};
    if (!QDir().mkpath(QFileInfo(m_path).absolutePath()))
        return false;
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.commit();
}

}
