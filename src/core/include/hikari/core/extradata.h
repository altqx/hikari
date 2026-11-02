#pragma once

#include "hikari/core/document.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

namespace hikari::core {

// "Data: 3,_aegi_folddata,e0;0;1": the value is "e" inline-escaped or "u" uuencoded, as Aegisub reads it.
std::optional<ExtradataEntry> parseExtradataLine(std::u8string_view line);
// Aegisub's choice: uuencoded when escaping would grow the value more than uuencoding.
std::u8string extradataLine(const ExtradataEntry &entry);

struct ExtradataPrefix {
    std::vector<std::uint32_t> ids;
    std::size_t length = 0;
};
// The "{=1=2}" block at the very start of `text`; nullopt without one.
std::optional<ExtradataPrefix> extradataPrefix(std::u8string_view text);
std::u8string extradataPrefixText(const std::vector<std::uint32_t> &ids);

// The values `ids` name, in their order; unknown ids are skipped.
std::vector<Extradata> resolveExtradata(const std::vector<ExtradataEntry> &table,
                                        const std::vector<std::uint32_t> &ids);
// One value per key, sorted by key, a later entry winning: what Aegisub gives a script as `line.extra`.
std::vector<Extradata> extradataByKey(const std::vector<Extradata> &extra);

// The ids each Line is saved with, and the entries a save adds. A Line keeps its loaded ids while they
// still name its values; otherwise each value reuses an equal entry or gets a new id above all others.
struct ExtradataPlan {
    std::map<LineId, std::vector<std::uint32_t>> ids;
    std::vector<ExtradataEntry> added;
};
ExtradataPlan planExtradata(const Document &document);

// The editor's own clipboard format next to copied rows: one line per copied Line, "key:value" pairs
// separated by ',', both escaped as Data values are.
std::u8string extradataClipboard(const std::vector<std::vector<Extradata>> &rows);
std::vector<std::vector<Extradata>> parseExtradataClipboard(std::u8string_view text);

}
