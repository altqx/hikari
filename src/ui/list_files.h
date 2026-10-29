#pragma once

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

class QLockFile;

namespace hikari::ui {

using application::ParseFailure;

struct ListFormat {
    std::string_view array;
    std::string_view name;
    std::vector<std::string_view> keys;
    std::function<std::optional<ParseFailure>(const toml::array &)> check;
};

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
std::optional<std::vector<application::FontCatalogs::Catalog>> fontCatalogsFromText(std::string_view text);

std::optional<std::vector<application::visual::ShapePreset>> readLegacyShapes(const std::filesystem::path &path);
std::optional<std::vector<application::visual::AllTagsSetting>> readLegacyTags(const std::filesystem::path &path);
std::optional<std::vector<application::FontCatalogs::Catalog>> readLegacyFontCatalogs(const std::filesystem::path &path);

class ListFile final : public std::enable_shared_from_this<ListFile> {
public:
    static constexpr std::int64_t kVersion = 1;
    using Convert = std::function<std::optional<toml::array>(const std::filesystem::path &legacy)>;
    struct Report {
        std::function<void(const std::filesystem::path &legacy, const std::filesystem::path &file)> converted;
        std::function<void(const application::FileProblem &)> problem;
        std::function<void(const application::FileProblem &)> replaced;
    };

    static std::shared_ptr<ListFile> make(QString path, QString legacy, const ListFormat &format, Convert convert,
                                          Report report = {});
    ~ListFile();

    const QString &path() const { return m_path; }
    const QString &legacyPath() const { return m_legacy; }

    void convert();
    std::optional<toml::array> read();
    enum class Write { Written, Waiting, Failed };
    Write write(toml::array items);
    bool flush();

    ListFile(QString path, QString legacy, const ListFormat &format, Convert convert, Report report);

private:
    std::optional<ParseFailure> parse(std::string_view text, toml::table &into) const;
    toml::table merged(toml::array items, toml::table onDisk) const;
    std::unique_ptr<QLockFile> lockFolder() const;
    void convertLocked();
    Write writePending();
    void retryLater();
    void problem(application::FileProblem problem);

    QString m_path;
    QString m_legacy;
    const ListFormat &m_format;
    Convert m_convert;
    Report m_report;
    std::optional<toml::array> m_pending;
    std::optional<toml::array> m_converted;
    bool m_deferred = false;
    // MUST KILL: m_writable false also makes read() return the defaults
    bool m_writable = true;
    bool m_retryQueued = false;
    bool m_reportedUnreadable = false;
};

}
