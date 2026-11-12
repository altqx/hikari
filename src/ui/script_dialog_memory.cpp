#include "script_dialog_memory.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace hikari::ui {

using application::DialogValue;
using application::LastDialogAnswer;
using application::NamedDialogValues;
using application::Tier;

namespace {

constexpr const char *kAnswers = "automation.dialogAnswers";
constexpr const char *kPresets = "automation.dialogPresets";

QJsonObject toJson(const NamedDialogValues &values)
{
    QJsonObject out;
    for (const auto &[name, value] : values) {
        const QString key = QString::fromStdString(name);
        if (const auto *s = std::get_if<std::string>(&value))
            out.insert(key, QString::fromStdString(*s));
        else if (const auto *i = std::get_if<int>(&value))
            out.insert(key, *i);
        else if (const auto *d = std::get_if<double>(&value))
            out.insert(key, *d);
        else if (const auto *b = std::get_if<bool>(&value))
            out.insert(key, *b);
    }
    return out;
}

// JSON numbers come back as doubles; application::dialogValueOf makes a whole one an integer.
NamedDialogValues fromJson(const QJsonObject &object)
{
    NamedDialogValues out;
    for (auto it = object.begin(); it != object.end(); ++it) {
        const std::string name = it.key().toStdString();
        if (it->isString())
            out.insert_or_assign(name, it->toString().toStdString());
        else if (it->isDouble())
            out.insert_or_assign(name, it->toDouble());
        else if (it->isBool())
            out.insert_or_assign(name, it->toBool());
    }
    return out;
}

std::optional<std::pair<qsizetype, QJsonDocument>> findRow(const QStringList &rows, const QString &key)
{
    const QString prefix = key + QLatin1Char('\t');
    for (qsizetype i = 0; i < rows.size(); ++i)
        if (rows[i].startsWith(prefix))
            return std::pair{i, QJsonDocument::fromJson(rows[i].mid(prefix.size()).toUtf8())};
    return std::nullopt;
}

QString rowOf(const QString &key, const QJsonDocument &json)
{
    return key + QLatin1Char('\t') + QString::fromUtf8(json.toJson(QJsonDocument::Compact));
}

}

QString ScriptDialogMemory::keyOf(const QString &script, const application::DialogRequest &request)
{
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(script.toUtf8());
    for (const auto &c : request.controls) {
        hash.addData(QByteArrayView("\n"));
        hash.addData(QByteArray::fromStdString(c.kind + '\t' + c.name));
    }
    return QString::fromLatin1(hash.result().toHex().left(16));
}

bool ScriptDialogMemory::fillLastAnswers() const
{
    return m_settings && m_settings->boolean("automation.fillLastAnswers");
}

std::optional<LastDialogAnswer> ScriptDialogMemory::lastAnswer(const QString &key) const
{
    if (!m_settings)
        return std::nullopt;
    const auto row = findRow(m_settings->list(kAnswers), key);
    if (!row || !row->second.isObject())
        return std::nullopt;
    const QJsonObject o = row->second.object();
    return LastDialogAnswer{fromJson(o.value(QLatin1String("sent")).toObject()),
                            fromJson(o.value(QLatin1String("answered")).toObject()),
                            o.value(QLatin1String("button")).toInt(-1)};
}

void ScriptDialogMemory::remember(const QString &key, const LastDialogAnswer &answer)
{
    if (!m_settings)
        return;
    QStringList rows = m_settings->list(kAnswers);
    if (const auto row = findRow(rows, key))
        rows.removeAt(row->first);
    const QJsonObject o{{QLatin1String("sent"), toJson(answer.sent)},
                        {QLatin1String("answered"), toJson(answer.answered)},
                        {QLatin1String("button"), answer.pressed}};
    rows.prepend(rowOf(key, QJsonDocument(o)));
    while (rows.size() > kKeptAnswers)
        rows.removeLast();
    m_settings->set(kAnswers, rows);
    m_settings->sync();
}

void ScriptDialogMemory::setProjectRoot(const QString &root)
{
    if (root != m_project.root())
        m_project = PresetsFile(root);
}

TieredPresets ScriptDialogMemory::tiered(const QString &key) const
{
    TieredPresets out;
    const auto arrayIn = [&key](const QStringList &rows) {
        const auto row = findRow(rows, key);
        return row ? row->second.array() : QJsonArray();
    };
    if (m_settings)
        out.of(Tier::Mine) = presetsOf(arrayIn(m_settings->list(kPresets)));
    out.of(Tier::Project) = presetsOf(arrayIn(m_project.list(kPresets)));
    return out;
}

void ScriptDialogMemory::store(const QString &key, const TieredPresets &presets, Tier tier)
{
    const auto withRow = [&](QStringList rows) {
        const QJsonArray list = jsonOf(presets.of(tier));
        const auto row = findRow(rows, key);
        if (list.isEmpty() && row)
            rows.removeAt(row->first);
        else if (row)
            rows[row->first] = rowOf(key, QJsonDocument(list));
        else if (!list.isEmpty())
            rows << rowOf(key, QJsonDocument(list));
        return rows;
    };
    if (tier == Tier::Project) {
        if (const QStringList before = m_project.list(kPresets), after = withRow(before); after != before)
            m_project.set(kPresets, after);
        return;
    }
    if (!m_settings)
        return;
    m_settings->set(kPresets, withRow(m_settings->list(kPresets)));
    m_settings->sync();
}

QStringList ScriptDialogMemory::presets(const QString &key) const
{
    QStringList names;
    for (const auto &row : tiered(key).rows())
        if (!row.hidden)
            names << QString::fromUtf8(reinterpret_cast<const char *>(row.name.data()), qsizetype(row.name.size()));
    return names;
}

QVariantList ScriptDialogMemory::presetRows(const QString &key) const
{
    return tiered(key).rowMaps();
}

std::optional<NamedDialogValues> ScriptDialogMemory::preset(const QString &key, const QString &name,
                                                            std::optional<Tier> tier) const
{
    const auto presets = tiered(key);
    if (const auto *p = presets.preset(name, tier))
        return fromJson(p->values);
    return std::nullopt;
}

void ScriptDialogMemory::savePreset(const QString &key, const QString &name, const NamedDialogValues &values, Tier tier)
{
    if (name.isEmpty() || (tier == Tier::Project && !hasProject()) || (tier == Tier::Mine && !m_settings))
        return;
    auto presets = tiered(key);
    presets.save(tier, {name, toJson(values)});
    store(key, presets, tier);
}

void ScriptDialogMemory::deletePreset(const QString &key, const QString &name, std::optional<Tier> tier)
{
    auto presets = tiered(key);
    const auto at = presets.find(name, tier);
    if (!at || presets.hidden(name, *at) || !presets.remove(*at, name))
        return;
    store(key, presets, *at);
}

std::optional<QString> ScriptDialogMemory::presetClash(const QString &key, const QString &name, Tier from) const
{
    return tiered(key).clash(name, from);
}

std::optional<QString> ScriptDialogMemory::sendPreset(const QString &key, const QString &name, Tier from,
                                                      const application::ClashChoice &choice, bool move)
{
    if (!hasProject() || !m_settings)
        return std::nullopt;
    auto presets = tiered(key);
    const auto landed = presets.send(name, from, choice, move);
    if (!landed)
        return std::nullopt;
    store(key, presets, otherTierOf(from));
    if (move)
        store(key, presets, from);
    return landed;
}

}
