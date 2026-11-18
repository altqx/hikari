#pragma once

#include "hikari/application/all_tags.h"
#include "hikari/application/font_catalogs.h"
#include "hikari/application/owned_file.h"
#include "hikari/application/shape_presets.h"

#include "file_write_queue.h"

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

    const QString &path() const { return m_disk.path; }
    const QString &legacyPath() const { return m_disk.legacy; }

    void convert();
    std::optional<toml::array> read();
    enum class Write { Written, Waiting, Failed };
    /// Waiting while the folder is locked (written later) and, with a queue, while the write is in the background.
    Write write(toml::array items);
    /// Writes what is still waiting before returning.
    bool flush();
    /// Later writes run on `queue`, which must outlive this file; `written` hears each one's result.
    void writeInBackground(FileWriteQueue &queue, std::function<void(Write)> written);

    ListFile(QString path, QString legacy, const ListFormat &format, Convert convert, Report report);

private:
    struct Disk {
        QString path;
        QString legacy;
        const ListFormat *format;
        Convert convert;
        std::optional<ParseFailure> parse(std::string_view text, toml::table &into) const;
        toml::table merged(toml::array items, toml::table onDisk) const;
        std::unique_ptr<QLockFile> lockFolder() const;
    };
    struct State {
        std::optional<toml::array> converted;
        bool deferred = false;
        // MUST KILL: writable false also makes read() return the defaults
        bool writable = true;
        bool reportedUnreadable = false;
    };
    struct Event {
        enum Kind { Converted, Problem, Replaced } kind;
        application::FileProblem problem;
    };
    struct Attempt {
        toml::array items;
        State state;
        std::vector<Event> events;
        Write result = Write::Failed;
    };
    static void convertLocked(const Disk &disk, State &state, std::vector<Event> &events);
    static void attempt(const Disk &disk, Attempt &attempt);
    Write writePending();
    Write finish(Attempt attempt);
    void startInBackground();
    void finishInBackground(Attempt attempt);
    void waitForBackground();
    void report(const std::vector<Event> &events);
    void retryLater();

    Disk m_disk;
    Report m_report;
    State m_state;
    std::optional<toml::array> m_pending;
    bool m_retryQueued = false;
    FileWriteQueue *m_queue = nullptr;
    std::function<void(Write)> m_written;
    bool m_inFlight = false;
    bool m_writeHere = false;
};

}
