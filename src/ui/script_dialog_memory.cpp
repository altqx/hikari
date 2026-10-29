#include "script_dialog_memory.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace hikari::ui {

using application::DialogValue;
using application::LastDialogAnswer;
using application::NamedDialogValues;

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

QStringList ScriptDialogMemory::presets(const QString &key) const
{
    QStringList names;
    if (!m_settings)
        return names;
    if (const auto row = findRow(m_settings->list(kPresets), key))
        for (const auto &p : row->second.array())
            names << p.toObject().value(QLatin1String("name")).toString();
    return names;
}

std::optional<NamedDialogValues> ScriptDialogMemory::preset(const QString &key, const QString &name) const
{
    if (!m_settings)
        return std::nullopt;
    if (const auto row = findRow(m_settings->list(kPresets), key))
        for (const auto &p : row->second.array())
            if (p.toObject().value(QLatin1String("name")).toString() == name)
                return fromJson(p.toObject().value(QLatin1String("values")).toObject());
    return std::nullopt;
}

void ScriptDialogMemory::savePreset(const QString &key, const QString &name, const NamedDialogValues &values)
{
    if (!m_settings || name.isEmpty())
        return;
    QStringList rows = m_settings->list(kPresets);
    const auto row = findRow(rows, key);
    QJsonArray list = row ? row->second.array() : QJsonArray();
    const QJsonObject preset{{QLatin1String("name"), name}, {QLatin1String("values"), toJson(values)}};
    qsizetype at = 0;
    while (at < list.size() && list[at].toObject().value(QLatin1String("name")).toString() != name)
        ++at;
    if (at < list.size())
        list[at] = preset;
    else
        list.append(preset);
    if (row)
        rows[row->first] = rowOf(key, QJsonDocument(list));
    else
        rows << rowOf(key, QJsonDocument(list));
    m_settings->set(kPresets, rows);
    m_settings->sync();
}

void ScriptDialogMemory::deletePreset(const QString &key, const QString &name)
{
    if (!m_settings)
        return;
    QStringList rows = m_settings->list(kPresets);
    const auto row = findRow(rows, key);
    if (!row)
        return;
    QJsonArray list = row->second.array();
    for (qsizetype i = list.size() - 1; i >= 0; --i)
        if (list[i].toObject().value(QLatin1String("name")).toString() == name)
            list.removeAt(i);
    if (list.isEmpty())
        rows.removeAt(row->first);
    else
        rows[row->first] = rowOf(key, QJsonDocument(list));
    m_settings->set(kPresets, rows);
    m_settings->sync();
}

}
