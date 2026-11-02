#pragma once

#include "hikari/api/json.h"

namespace hikari::api {

// {schema, version, $defs, operations, events, nodes}: the whole API as
// --export-docs writes it to api.json.
Json apiReference();

}
