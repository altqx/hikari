#pragma once

#include "hikari/application/node_catalogue.h"
#include "hikari/json/json.h"

#include <QString>
#include <QVariant>

namespace hikari::ui::node_fields {

QVariant variantOf(const json::Json &j);
json::Json jsonOf(const QVariant &v);
bool numericKind(application::FieldKind kind);
// A field as the user typed it, in the type its kind takes.
json::Json fieldJson(const application::FieldSpec *spec, const QVariant &value);
QString kindName(application::FieldKind kind);

}
