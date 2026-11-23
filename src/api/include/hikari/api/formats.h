#pragma once

#include "hikari/api/host.h"
#include "hikari/api/json.h"

#include "hikari/core/document.h"

#include <expected>
#include <span>
#include <string>

namespace hikari::api {

// HikariSub's own formats, in the order formats.list gives them.
std::span<const FormatInfo> builtInFormats();
// Built-in formats first, then the plugins'.
std::vector<FormatInfo> allFormats(const Host &host);

// A new ASS Document from an importer's {lines, styles, info}: Lines in insert_line's fields, Styles in
// set_style's fields with their name, Script Info as text by key. The default Style stays unless Styles come.
// The error names what doesn't fit.
std::expected<core::Document, std::string> documentFromImport(const Json &imported);

}
