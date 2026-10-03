#pragma once

// Y1/Y2: the Style manager (legacy GLOBAL_OPEN_STYLE_MANAGER: StyleStore,
// StyleChange, Styles and the style catalogs of config.cpp at 20d647c4).
// The same list operations serve the Document's Styles and a catalog's.

#include "hikari/application/edit_session.h"
#include "hikari/core/style.h"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

using StyleList = std::vector<core::StyleValues>;

// Styles::Compare / CopyChanges field bits (styles.h).
namespace style_field {
inline constexpr int FontName = 1, FontSize = 2, Bold = 4, Italic = 8, Underline = 16, StrikeOut = 32,
                     Primary = 64, Secondary = 128, Outline = 1 << 8, Shadow = 1 << 9, OutlineWidth = 1 << 10,
                     ShadowWidth = 1 << 11, ScaleX = 1 << 12, ScaleY = 1 << 13, Angle = 1 << 14, Spacing = 1 << 15,
                     BorderStyle = 1 << 16, Alignment = 1 << 17, MarginLeft = 1 << 18, MarginRight = 1 << 19,
                     MarginVertical = 1 << 20, Encoding = 1 << 21;
} // namespace style_field
// The fields that differ (Styles::Compare).
int compareStyles(const core::StyleValues &a, const core::StyleValues &b);
// `target` takes the `fields` of `changed` (Styles::CopyChanges).
core::StyleValues copyStyleChanges(core::StyleValues target, const core::StyleValues &changed, int fields);

// Legacy Styles(): Default, Garamond 40, white, outline blue, 2/2, alignment 2, margins 20.
core::StyleValues defaultStyle(std::u8string name = u8"Default");
// "New Style", "New Style1", ... the first free name (OnAssNew / OnStoreNew).
std::u8string newStyleName(const StyleList &styles, std::u8string_view base = u8"New Style");

// Answers to "Style named \"%s\" already exists. Replace?".
enum class Replace { Yes, YesToAll, No, Cancel };
using AskReplace = std::function<Replace(const std::u8string &name)>;
// OnAddToStore / OnAddToAss / LoadStylesS: each Style is added, or replaces
// the first of its name when the answer says so. With `cancelStops` (the
// Add to storage / Add to ASS buttons) Cancel ends the transfer; otherwise
// (Load, Add to all open ASS files) it answers No for every later name.
// Returns the rows of the Styles added or replaced (the legacy selection).
std::vector<std::size_t> transferStyles(StyleList &into, const StyleList &styles, const AskReplace &ask,
                                        bool cancelStops = true);

enum class StyleMove { ToStart, Up, Down, ToEnd };
// OnStyleMove: the selected rows (ascending) move together, clamped at the
// ends; returns their new rows.
std::vector<std::size_t> moveStyles(StyleList &styles, std::vector<std::size_t> rows, StyleMove move);
// Sort by name (locale collation; bytes when empty). Legacy std::sort is
// not stable; equal names keep their order here.
using NameCompare = std::function<int(std::u8string_view, std::u8string_view)>;
void sortStyles(StyleList &styles, const NameCompare &compare = {});

// StyleStore::ChangeStyle for an editor commit. `row` is the edited Style
// (nullopt: a new or copied Style, added at the end). Refused, with the
// legacy "Style named \"%s\" already exists.", when a new Style's name is
// taken or an edit renames to a taken name. With `fields` (the multi-edit
// "Change all selected styles?" answered Yes) the other `selected` rows
// take those fields too. Returns the row of the committed Style.
struct StyleEdit {
    std::optional<std::size_t> row;
    core::StyleValues style;
    std::u8string oldName; // the name the editor opened with
    std::vector<std::size_t> selected;
    int fields = 0;        // 0: no multi-edit
};
std::expected<std::size_t, std::u8string> commitStyle(StyleList &styles, const StyleEdit &edit);

// --- The Document's Styles ("Style editing" steps) ---------------------

// Refused for a Document without ASS Styles (line formats, SSA layouts).
std::optional<StyleList> documentStyles(const EditSession &session);
// Replaces the Document's Styles with `styles`; `origin[i]` is the row a
// Style had before (nullopt for a new one), so unchanged ones keep their
// bytes. With `rename` the Lines in `rename->first` take `rename->second`
// (legacy "Style name modified. Do you want to change all instances to the
// new name?"). Nothing happens when nothing changes.
std::expected<void, CommandRefusal>
setDocumentStyles(EditSession &session, const StyleList &styles, const std::vector<std::optional<std::size_t>> &origin,
                  std::optional<std::pair<std::u8string, std::u8string>> rename = std::nullopt);

// OnCleanStyles: removes the Styles no Line uses (never TLMode Style).
struct CleanResult {
    std::vector<std::u8string> used, deleted;
};
std::expected<CleanResult, CommandRefusal> cleanDocumentStyles(EditSession &session);

// --- Style catalogs (Y2) -------------------------------------------------

// A catalog file (<settings>/Catalog/<name>.sty): "Style: " lines; written
// UTF-8 with a BOM, each Style as Styles::GetRaw with CRLF.
StyleList readCatalog(std::string_view bytes);
std::string writeCatalog(const StyleList &styles);

class StyleCatalogs {
public:
    explicit StyleCatalogs(std::filesystem::path directory);
    // The catalog names (the .sty files; legacy creates Default when none).
    const std::vector<std::u8string> &names() const { return m_names; }
    const std::u8string &current() const { return m_current; }
    StyleList &styles() { return m_styles; }
    const StyleList &styles() const { return m_styles; }
    // Saves the current catalog first when changed, then loads another.
    bool choose(const std::u8string &name);
    // NewCatalog: an empty catalog, made current (written on the next save).
    bool create(const std::u8string &name);
    // Deletes a catalog other than Default; the one before it becomes current.
    bool remove(const std::u8string &name);
    void markChanged() { m_changed = true; }
    bool save();

private:
    void load(const std::u8string &name);
    std::filesystem::path m_dir;
    std::vector<std::u8string> m_names;
    std::u8string m_current;
    StyleList m_styles;
    bool m_changed = false;
};

} // namespace hikari::application
