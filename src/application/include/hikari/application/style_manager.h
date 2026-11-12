#pragma once

#include "hikari/application/clash_choice.h"
#include "hikari/application/edit_session.h"
#include "hikari/application/tiers.h"
#include "hikari/core/style.h"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <absl/container/flat_hash_set.h>
#include <utility>
#include <vector>

namespace hikari::application {

using StyleList = std::vector<core::StyleValues>;

namespace style_field {
inline constexpr int FontName = 1, FontSize = 2, Bold = 4, Italic = 8, Underline = 16, StrikeOut = 32,
                     Primary = 64, Secondary = 128, Outline = 1 << 8, Shadow = 1 << 9, OutlineWidth = 1 << 10,
                     ShadowWidth = 1 << 11, ScaleX = 1 << 12, ScaleY = 1 << 13, Angle = 1 << 14, Spacing = 1 << 15,
                     BorderStyle = 1 << 16, Alignment = 1 << 17, MarginLeft = 1 << 18, MarginRight = 1 << 19,
                     MarginVertical = 1 << 20, Encoding = 1 << 21;
}
// The style_field bits that differ.
int compareStyles(const core::StyleValues &a, const core::StyleValues &b);
core::StyleValues copyStyleChanges(core::StyleValues target, const core::StyleValues &changed, int fields);

core::StyleValues defaultStyle(std::u8string name = u8"Default");
// "New Style", "New Style1", ...: the first free name.
std::u8string newStyleName(const StyleList &styles, std::u8string_view base = u8"New Style");

enum class Replace { Yes, YesToAll, No, Cancel };
using AskReplace = std::function<Replace(const std::u8string &name)>;
// Adds each Style, or replaces the first of its name when `ask` says so. Cancel ends the transfer
// with `cancelStops`, else answers No for every later name. Returns the rows added or replaced.
std::vector<std::size_t> transferStyles(StyleList &into, const StyleList &styles, const AskReplace &ask,
                                        bool cancelStops = true);

// Name and its 0-based occurrence among the incoming Styles, clash or not.
using ClashKey = std::pair<std::u8string, int>;
using ClashChoices = std::map<ClashKey, ClashChoice>;
// A Style whose name is taken (in `into` or earlier in `styles`) follows its clash's choice; without
// one it is left out. A taken new name gets "<new> (2)"... Returns the rows added or replaced.
std::vector<std::size_t> transferStyles(StyleList &into, const StyleList &styles, const ClashChoices &choices);
// "<name> (2)", "<name> (3)"...: the first that names no Style.
std::u8string freeStyleName(const StyleList &styles, std::u8string_view name);
using StyleNames = absl::flat_hash_set<std::u8string>;
StyleNames namesOf(const StyleList &styles);
std::u8string freeStyleName(const StyleNames &names, std::u8string_view name);

enum class StyleMove { ToStart, Up, Down, ToEnd };
// `rows` ascending; they move together, clamped at the ends. Returns their new rows.
std::vector<std::size_t> moveStyles(StyleList &styles, std::vector<std::size_t> rows, StyleMove move);
// Stable; `compare` empty: byte order.
using NameCompare = std::function<int(std::u8string_view, std::u8string_view)>;
void sortStyles(StyleList &styles, const NameCompare &compare = {});

// `row` nullopt: a new Style, added at the end. Refused with the name when a new Style's name is
// taken or an edit renames to a taken name. With `fields` the other `selected` rows take those
// fields too. Returns the committed Style's row.
struct StyleEdit {
    std::optional<std::size_t> row;
    core::StyleValues style;
    std::u8string oldName; // as the editor opened it
    std::vector<std::size_t> selected;
    int fields = 0;        // 0: no multi-edit
};
std::expected<std::size_t, std::u8string> commitStyle(StyleList &styles, const StyleEdit &edit);

// nullopt for a Document without ASS Styles.
std::optional<StyleList> documentStyles(const EditSession &session);
// `origin[i]` is a Style's previous row (nullopt: new), so unchanged ones keep their bytes. With
// `rename` the Lines in `rename->first` take `rename->second`. srv3 style options follow their Style:
// renamed with it, removed with it, and copied to `copy->second` from `copy->first`. No step when
// nothing changes.
std::expected<void, CommandRefusal>
setDocumentStyles(EditSession &session, const StyleList &styles, const std::vector<std::optional<std::size_t>> &origin,
                  std::optional<std::pair<std::u8string, std::u8string>> rename = std::nullopt,
                  std::optional<std::pair<std::u8string, std::u8string>> copy = std::nullopt);

// Removes Styles no Line uses, never the TLMode Style.
struct CleanResult {
    std::vector<std::u8string> used, deleted;
};
std::expected<CleanResult, CommandRefusal> cleanDocumentStyles(EditSession &session);

// .sty catalog: "Style: " lines; written UTF-8 with a BOM and CRLF.
StyleList readCatalog(std::string_view bytes);
std::string writeCatalog(const StyleList &styles);

class StyleCatalogs {
public:
    explicit StyleCatalogs(std::filesystem::path directory);
    // The Project's catalogs folder, made on its first write; empty for none. Saves the current
    // catalog first, then shows the same name, the Project's copy first.
    void setProjectDirectory(std::filesystem::path directory);
    const std::filesystem::path &projectDirectory() const { return m_projectDir; }
    const std::filesystem::path &directory(Tier tier) const { return tier == Tier::Project ? m_projectDir : m_dir; }
    // Mine always has a Default catalog.
    const std::vector<std::u8string> &names(Tier tier) const { return tier == Tier::Project ? m_projectNames : m_names; }
    // Every name once, sorted.
    std::vector<std::u8string> names() const;
    std::vector<TieredName> rows() const { return hideByName(m_projectNames, m_names); }
    const std::u8string &current() const { return m_current; }
    Tier currentTier() const { return m_currentTier; }
    // A Mine catalog that the Project's hides: shown, never written.
    bool readOnly() const;
    StyleList &styles() { return m_styles; }
    const StyleList &styles() const { return m_styles; }
    // Saves the current catalog first when changed, then loads another; the Project's copy first
    // without a tier.
    bool choose(const std::u8string &name);
    bool choose(const std::u8string &name, Tier tier);
    // Made current; written on the next save. Refused for a name the tier has, or a Mine name the
    // Project hides.
    bool create(const std::u8string &name, Tier tier = Tier::Mine);
    // Deletes a catalog other than Mine's Default; the one before it becomes current.
    bool remove(const std::u8string &name);
    bool remove(const std::u8string &name, Tier tier);
    // Writes the catalog into the other tier; returns the name it landed under, none when skipped.
    std::optional<std::u8string> copy(const std::u8string &name, Tier from, const ClashChoice &clash = {});
    // Copy, then remove the source; Mine's Default only copies.
    std::optional<std::u8string> move(const std::u8string &name, Tier from, const ClashChoice &clash = {});
    void markChanged() { m_changed = true; }
    // Reads another catalog from its file without making it current; nullopt when either is missing.
    std::optional<core::StyleValues> find(const std::u8string &catalog, const std::u8string &style) const;
    std::optional<StyleList> list(const std::u8string &catalog) const;
    std::optional<StyleList> list(const std::u8string &catalog, Tier tier) const;
    bool save();

private:
    std::filesystem::path file(const std::u8string &name, Tier tier) const;
    std::vector<std::u8string> &namesIn(Tier tier) { return tier == Tier::Project ? m_projectNames : m_names; }
    Tier resolve(const std::u8string &name) const;
    void load(const std::u8string &name, Tier tier);
    void loadDefault();
    std::filesystem::path m_dir;
    std::filesystem::path m_projectDir;
    std::vector<std::u8string> m_names;
    std::vector<std::u8string> m_projectNames;
    std::u8string m_current;
    Tier m_currentTier = Tier::Mine;
    StyleList m_styles;
    bool m_changed = false;
};

}
