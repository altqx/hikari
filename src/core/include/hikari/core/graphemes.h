#pragma once

// Extended grapheme clusters (UAX #29) as PCRE2's \X finds them.

#include <cstddef>
#include <string_view>

namespace hikari::core {

// Where the grapheme starting at `at` ends (past `at`; text.size() at the
// end). A unit PCRE2 can't read (a lone surrogate) is a grapheme of its own.
std::size_t graphemeEnd(std::u16string_view text, std::size_t at);

} // namespace hikari::core
