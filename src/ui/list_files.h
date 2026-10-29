#pragma once

// CF2 (#489): the user's lists in the user folder, TOML files of `version =
// 1` and one array of tables (owned_file.h):
// - rules.toml, Fix minor errors' rules: [[rule]] description, find,
//   replace, enabled and the options matchCase, lowerCase, upperCase,
//   keepCase, onlyInTags, onlyInText (written when true);
// - shapes.toml, the drawing's shape presets: [[shape]] name, drawing,
//   fromCursor ("widthAndHeight", "keepAspectRatio", "widthOnly") and
//   scaling ("points", "scale");
// - tags.toml, the all-tags tool's definitions: [[tag]] name, tag, min,
//   max, step, decimals, values (1 to 4), placing ("cursor", "textStart",
//   "clipAnimation") and change ("add", "set", "multiply", "multiplyAdd",
//   "gradientCharsUp", "gradientCharsDown", "gradientLinesUp",
//   "gradientLinesDown");
// - catalogs/fonts.toml and its autosaves fonts.autosave-N.toml, the Font
//   catalogs: [[catalog]] name and fonts.
// A choice legacy could hold without a name here (a shape mode of 7) is
// written as its number. Each list replaces a legacy text file in the same
// folder (Rules.txt, ShapesSettings.txt, AllTagsSettings.txt,
// FontCatalogs.txt, FontCatalogsAutosave<N>.txt), converted once: the old
// file becomes <name>.old and is never written again.

#include "hikari/application/all_tags.h"
#include "hikari/application/font_catalogs.h"
#include "hikari/application/misspell_replacer.h"
#include "hikari/application/owned_file.h"
#include "hikari/application/shape_presets.h"

#include <toml++/toml.hpp>

#include <QString>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

namespace hikari::ui {

using application::ParseFailure;

// What a list file holds: its array's name, the key that names an item
// (an unknown key goes back on the item of the same name when the list is
// written) and the keys this version writes.
struct ListFormat {
    std::string_view array;
    std::string_view name;
    std::vector<std::string_view> keys;
    // Where the items fail to read, nothing when they read.
    std::function<std::optional<ParseFailure>(const toml::array &)> check;
};

// A list file's text: `version = 1`, then the items.
std::string listFileText(const ListFormat &format, toml::array items);

template <class Item> using ListRead = std::variant<std::vector<Item>, ParseFailure>;

extern const ListFormat kRulesFormat;
ListRead<application::ReplacerRule> rulesFromToml(const toml::array &items);
toml::array rulesToToml(const std::vector<application::ReplacerRule> &rules);

extern const ListFormat kShapesFormat;
ListRead<application::visual::ShapePreset> shapesFromToml(const toml::array &items);
toml::array shapesToToml(const std::vector<application::visual::ShapePreset> &shapes);

extern const ListFormat kTagsFormat;
ListRead<application::visual::AllTagsSetting> tagsFromToml(const toml::array &items);
toml::array tagsToToml(const std::vector<application::visual::AllTagsSetting> &tags);

extern const ListFormat kFontCatalogsFormat;
ListRead<application::FontCatalogs::Catalog> fontCatalogsFromToml(const toml::array &items);
toml::array fontCatalogsToToml(const std::vector<application::FontCatalogs::Catalog> &catalogs);
// The catalogs of a fonts.toml's text; nothing when it is not one (the
// font catalog window's Load reads a legacy file then).
std::optional<std::vector<application::FontCatalogs::Catalog>> fontCatalogsFromText(std::string_view text);

// The legacy files' readers, as legacy read them: nothing when the file
// cannot be read. Rules.txt's reader (its encoding is guessed) is the
// app's.
std::optional<std::vector<application::visual::ShapePreset>> readLegacyShapes(const std::filesystem::path &path);
std::optional<std::vector<application::visual::AllTagsSetting>> readLegacyTags(const std::filesystem::path &path);
std::optional<std::vector<application::FontCatalogs::Catalog>> readLegacyFontCatalogs(const std::filesystem::path &path);

// One list file. Reading converts the legacy file first, once; a file that
// does not read is kept as <name>.broken and the list takes its defaults.
// Writing is atomic, under a lock in the file's folder: while another
// HikariSub holds it the list waits and is tried again shortly (and when
// the ListFile goes). Between two HikariSubs the last list written wins.
class ListFile final : public std::enable_shared_from_this<ListFile> {
public:
    static constexpr std::int64_t kVersion = 1;
    // The legacy file's items, nothing when it cannot be read.
    using Convert = std::function<std::optional<toml::array>(const std::filesystem::path &legacy)>;
    struct Report {
        std::function<void(const std::filesystem::path &legacy, const std::filesystem::path &file)> converted;
        std::function<void(const application::FileProblem &)> problem;
    };

    static std::shared_ptr<ListFile> make(QString path, QString legacy, const ListFormat &format, Convert convert,
                                          Report report = {});
    ~ListFile();

    const QString &path() const { return m_path; }
    const QString &legacyPath() const { return m_legacy; }

    // The legacy file converted when there is no list file yet. A legacy
    // file that cannot be read is not, and the list is not written this
    // session (so the next start tries again).
    void convert();
    // The file's items: nothing when there is none, or when it (or the
    // legacy file) did not read: the list's defaults.
    std::optional<toml::array> read();
    // Writes `items` over the file (its unknown keys kept).
    enum class Write { Written, Waiting, Failed };
    Write write(toml::array items);
    // A list waiting for the lock written now; false when it is still waiting.
    bool flush();

    ListFile(QString path, QString legacy, const ListFormat &format, Convert convert, Report report);

private:
    std::optional<ParseFailure> parse(std::string_view text, toml::table &into) const;
    toml::table mergedWithFile(toml::array items) const;
    Write writePending();
    void retryLater();
    void problem(application::FileProblem problem);

    QString m_path;
    QString m_legacy;
    const ListFormat &m_format;
    Convert m_convert;
    Report m_report;
    std::optional<toml::array> m_pending;
    // The conversion's items when their file could not be written: the
    // next write converts.
    std::optional<toml::array> m_converted;
    bool m_writable = true;
    bool m_retryQueued = false;
    bool m_reportedUnreadable = false;
};

} // namespace hikari::ui
