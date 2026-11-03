#pragma once

#include "hikari/core/document.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
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
// The last value of `key`, the one `line.extra` gives.
std::optional<std::u8string> extradataValue(const std::vector<Extradata> &extra, std::u8string_view key);
// `extra` holding `value` as the one entry of `key`, after its other entries; unchanged when it already does.
std::vector<Extradata> withExtradata(std::vector<Extradata> extra, std::u8string_view key, std::u8string value);
std::vector<Extradata> withoutExtradata(std::vector<Extradata> extra, std::u8string_view key);

// LineRecord::generatedFrom in the file: a generated Line and its karaoke Line hold the same token,
// "1;" and eight base32 letters.
inline constexpr std::u8string_view kKaraSourceKey = u8"_hikari_kara_source";
inline constexpr std::u8string_view kKaraFromKey = u8"_hikari_kara_from";
// Each Line holding a kKaraFromKey token is generated from the first Line holding it as kKaraSourceKey.
void linkGeneratedLines(const std::vector<LineRecord *> &lines);
std::u8string randomKaraToken();
// The tokens for every Line's generatedFrom. A karaoke Line keeps the token it holds unless an earlier
// Line holds it too; otherwise it gets `newToken()`, retried until no Line holds it.
void markGeneratedLines(Document &document, const std::function<std::u8string()> &newToken = randomKaraToken);

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
