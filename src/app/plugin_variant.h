#pragma once

#include "hikari/api/json.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QVariant>

namespace hikari::app {

inline QVariant toVariant(const api::Json &json)
{
    const QByteArray text = QByteArray::fromStdString(api::Json::array({json}).dump());
    return QJsonDocument::fromJson(text).array().at(0).toVariant();
}

inline api::Json fromVariant(const QVariant &value)
{
    const QByteArray text = QJsonDocument(QJsonArray{QJsonValue::fromVariant(value)}).toJson(QJsonDocument::Compact);
    auto parsed = api::Json::parse(text.toStdString());
    return parsed && parsed->isArray() && !parsed->asArray().empty() ? parsed->asArray().front() : api::Json();
}

}
