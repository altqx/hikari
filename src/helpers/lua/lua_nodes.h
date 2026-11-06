#pragma once

#include "hikari/backends/helper_endpoint.h"

#include <cstddef>

namespace hikari::lua_nodes {

// Runs a RunNodes request: every row in fresh globals holding only the node environment and its
// inputs, in a Lua state of its own that ends with the request.
void run(backends::helper::Reader &in, backends::helper::Responder &r, std::size_t payloadSize);

}
