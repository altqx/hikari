#include "node_field_values.h"

#include <cmath>
#include <cstdint>

namespace hikari::ui::node_fields {

using json::Json;

QVariant variantOf(const Json &j)
{
    switch (j.type()) {
    case Json::Type::Bool:
        return j.asBool();
    case Json::Type::Int:
    case Json::Type::Double:
        return j.asNumber();
    case Json::Type::String:
        return QString::fromStdString(j.asString());
    case Json::Type::Array: {
        QVariantList out;
        for (const Json &e : j.asArray())
            out << variantOf(e);
        return out;
    }
    case Json::Type::Object: {
        QVariantMap out;
        for (const Json::Member &m : j.asObject())
            out.insert(QString::fromStdString(m.key), variantOf(m.value));
        return out;
    }
    default:
        return {};
    }
}

Json jsonOf(const QVariant &v)
{
    switch (v.typeId()) {
    case QMetaType::Bool:
        return Json(v.toBool());
    case QMetaType::Int:
    case QMetaType::LongLong:
    case QMetaType::UInt:
    case QMetaType::ULongLong:
    case QMetaType::Double:
    case QMetaType::Float:
        return Json(v.toDouble());
    case QMetaType::QString:
        return Json(v.toString().toStdString());
    case QMetaType::QVariantList:
    case QMetaType::QStringList: {
        Json out = Json::array();
        for (const QVariant &e : v.toList())
            out.push(jsonOf(e));
        return out;
    }
    case QMetaType::QVariantMap: {
        Json out = Json::object();
        const QVariantMap map = v.toMap();
        for (auto it = map.begin(); it != map.end(); ++it)
            out[it.key().toStdString()] = jsonOf(it.value());
        return out;
    }
    default:
        return v.isNull() ? Json() : Json(v.toString().toStdString());
    }
}

bool numericKind(application::FieldKind kind)
{
    using enum application::FieldKind;
    return kind == Number || kind == Integer || kind == Alpha || kind == Time;
}

Json fieldJson(const application::FieldSpec *spec, const QVariant &value)
{
    if (spec && numericKind(spec->kind) && value.typeId() == QMetaType::QString) {
        bool ok = false;
        const double d = value.toString().trimmed().toDouble(&ok);
        if (!ok)
            return value.toString().trimmed().isEmpty() ? Json() : Json(value.toString().toStdString());
        return spec->kind == application::FieldKind::Integer ? Json(static_cast<std::int64_t>(std::llround(d))) : Json(d);
    }
    if (spec && spec->kind == application::FieldKind::Integer && value.canConvert<double>() &&
        value.typeId() != QMetaType::QString)
        return Json(static_cast<std::int64_t>(std::llround(value.toDouble())));
    return jsonOf(value);
}

QString kindName(application::FieldKind kind)
{
    using enum application::FieldKind;
    switch (kind) {
    case Number: return QStringLiteral("Number");
    case Integer: return QStringLiteral("Integer");
    case Text: return QStringLiteral("Text");
    case Bool: return QStringLiteral("Bool");
    case Colour: return QStringLiteral("Colour");
    case Alpha: return QStringLiteral("Alpha");
    case Time: return QStringLiteral("Time");
    case Point: return QStringLiteral("Point");
    case Choice: return QStringLiteral("Choice");
    case List: return QStringLiteral("List");
    case Ports: return QStringLiteral("Ports");
    case Graph: return QStringLiteral("Graph");
    }
    return {};
}

}
