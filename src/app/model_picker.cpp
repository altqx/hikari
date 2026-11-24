#include "hikari/app/model_picker.h"

#include "quick_pick_model.h"

#include <QCoreApplication>
#include <QVariantMap>

#include <algorithm>

namespace hikari::app {
namespace {

// MUST KILL: the quick pick greys a row only when it has a reason, and "Loading models…" has none to show.
const QString kNoReason = QStringLiteral(" ");

class Items {
public:
    explicit Items(const ModelPickerRequest &request) : m_request(request) {}

    void group(const QString &name) { m_group = name; }

    void model(const QString &id, const QString &label, const QString &description, const QString &value,
               bool alwaysShow)
    {
        QVariantMap item{{QStringLiteral("id"), id},
                         {QStringLiteral("label"), label},
                         {QStringLiteral("description"), description},
                         {QStringLiteral("value"), value},
                         {QStringLiteral("group"), m_group},
                         {QStringLiteral("alwaysShow"), alwaysShow}};
        if (value == m_request.current) {
            item.insert(QStringLiteral("icon"), QStringLiteral("karaoke-check"));
            item.insert(QStringLiteral("tooltip"), QCoreApplication::translate("HikariSub.Agent", "current model"));
        }
        m_items.push_back(item);
    }

    void disabled(const QString &id, const QString &label, const QString &reason)
    {
        m_items.push_back(QVariantMap{{QStringLiteral("id"), id},
                                      {QStringLiteral("label"), label},
                                      {QStringLiteral("disabled"), reason},
                                      {QStringLiteral("group"), m_group}});
    }

    void action(const QString &id, const QString &label, const QString &action, const QString &key,
                const QString &icon = {})
    {
        m_items.push_back(QVariantMap{{QStringLiteral("id"), id},
                                      {QStringLiteral("label"), label},
                                      {QStringLiteral("action"), action},
                                      {QStringLiteral("key"), key},
                                      {QStringLiteral("icon"), icon},
                                      {QStringLiteral("group"), m_group}});
    }

    QVariantList take() { return std::move(m_items); }

private:
    const ModelPickerRequest &m_request;
    QString m_group;
    QVariantList m_items;
};

QString agentDefaultText()
{
    return QCoreApplication::translate("HikariSub.Agent", "Agent default");
}

QString idUnlessName(const ModelPickerModel &model)
{
    return model.id == model.name ? QString() : model.id;
}

bool matchesGroup(const QString &filter, const ModelPickerGroup &group)
{
    return !filter.trimmed().isEmpty() && ui::fuzzyMatch(filter, group.name).has_value();
}

void addRecent(const ModelPickerRequest &request, Items &items)
{
    std::vector<std::pair<const ModelPickerModel *, QString>> offered;
    for (const ModelPickerGroup &group : request.groups) {
        if (!group.blockedBy.isEmpty())
            continue;
        if (group.agentDefault)
            offered.emplace_back(&*group.agentDefault, agentDefaultText());
        for (const ModelPickerModel &model : group.models)
            offered.emplace_back(&model, model.name);
    }
    items.group(QCoreApplication::translate("HikariSub.Agent", "Recent"));
    for (const QString &value : request.recent) {
        const auto it = std::ranges::find(offered, value, [](const auto &entry) { return entry.first->value; });
        if (it != offered.end())
            items.model(QStringLiteral("recent\t") + value, it->second, it->first->source, value, false);
    }
}

void addGroup(const ModelPickerRequest &request, const ModelPickerGroup &group, Items &items)
{
    items.group(group.name);
    if (!group.blockedBy.isEmpty()) {
        items.action(QStringLiteral("blocked\t") + group.key, group.blockedBy, QStringLiteral("settings"), group.blockedRow);
        return;
    }
    const bool alwaysShow = matchesGroup(request.filter, group);
    if (group.agentDefault)
        items.model(group.agentDefault->value, agentDefaultText(), {}, group.agentDefault->value, alwaysShow);
    for (const ModelPickerModel &model : group.models)
        items.model(model.value, model.name, idUnlessName(model), model.value, alwaysShow);
    if (group.loading && group.models.empty())
        items.disabled(QStringLiteral("loading\t") + group.key,
                       QCoreApplication::translate("HikariSub.Agent", "Loading models…"), kNoReason);
    if (group.failure && !group.loading) {
        items.disabled(QStringLiteral("failed\t") + group.key,
                       QCoreApplication::translate("HikariSub.Agent", "Couldn’t load models"), *group.failure);
        items.action(QStringLiteral("retry\t") + group.key, QCoreApplication::translate("HikariSub.Agent", "Try again"),
                     QStringLiteral("retry"), group.key, QStringLiteral("refresh"));
    }
}

}

QVariantList modelPickerItems(const ModelPickerRequest &request)
{
    Items items(request);
    if (request.emptyChoice)
        items.model(QStringLiteral("empty"), *request.emptyChoice, {}, QString(), false);
    if (request.filter.trimmed().isEmpty() && !request.recent.isEmpty())
        addRecent(request, items);
    for (const ModelPickerGroup &group : request.groups)
        addGroup(request, group, items);
    if (request.settingsRow) {
        items.group(QString());
        items.action(QStringLiteral("settings"), QCoreApplication::translate("HikariSub.Agent", "Settings › AI…"),
                     QStringLiteral("settings"), QStringLiteral("ai"), QStringLiteral("settings"));
    }
    return items.take();
}

}
