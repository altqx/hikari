#pragma once

#include "hikari/api/json.h"

#include <optional>
#include <string>

namespace hikari::api {

// A JSON Schema 2020-12 subset: type, properties, required, additionalProperties,
// items, enum, const, minimum, maximum, minLength, maxLength, minItems, maxItems,
// oneOf, anyOf, $ref ("#/$defs/<name>", resolved in `defs`), plus the
// annotations description, title and default. "integer" accepts only an Int.
struct SchemaFailure {
    std::string path; // JSON pointer into the value
    std::string message;
};

std::optional<SchemaFailure> validate(const Json &schema, const Json &value, const Json &defs);

// The first problem with `schema` itself: a keyword outside the subset, a
// malformed keyword value or a $ref that does not resolve. nullopt when sound.
std::optional<std::string> checkSchema(const Json &schema, const Json &defs);

}
