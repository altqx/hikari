#pragma once

#include "hikari/api/json.h"

#include <string>

namespace hikari::api {

// {schema, version, $defs, operations, events, nodes}: the whole API as
// --export-docs writes it to api.json.
Json apiReference();

// `reference` as Markdown, for authors: what docs/api-reference.md holds and --export-docs writes to api.md.
std::string referenceMarkdown(const Json &reference);

// LuaLS `---@meta` annotations for the `hikari` global and every operation: hikari.lua.
std::string luaAnnotations(const Json &reference);

// TypeScript types for every operation, event and program-plugin protocol message: hikari.d.ts.
std::string typeScriptTypes(const Json &reference);

}
