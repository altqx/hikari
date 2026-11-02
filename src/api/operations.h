#pragma once

#include "hikari/api/api.h"

#include <string_view>

namespace hikari::api::detail {

// Aborts on malformed text: schemas are compiled-in literals the tests check.
Json schema(std::string_view text);

// A handler that answers before it returns.
using SyncHandler = std::function<Result(const Context &, const Json &params)>;
Handler now(SyncHandler handler);

void addDefinitions(Registry &registry);
void addDocumentOperations(Registry &registry);
void addApplicationOperations(Registry &registry);

}
