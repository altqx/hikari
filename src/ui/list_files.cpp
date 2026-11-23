#include "list_files.h"

#include "toml_file.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QTimer>

#include <algorithm>
#include <charconv>
#include <map>

namespace hikari::ui {

namespace {

namespace fs = std::filesystem;
using application::visual::AllTagsSetting;
using application::visual::ShapePreset;
using Catalog = application::FontCatalogs::Catalog;

fs::path pathOf(const QString &path)
{
    return fs::path(path.toStdU16String());
}

std::string utf8(std::u16string_view s)
{
    return QString::fromStdU16String(std::u16string(s)).toStdString();
}

std::u16string utf16(std::string_view s)
{
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size())).toStdU16String();
}

std::string utf8(std::u8string_view s)
{
    return {reinterpret_cast<const char *>(s.data()), s.size()};
}

std::u8string u8(std::string_view s)
{
    return {reinterpret_cast<const char8_t *>(s.data()), s.size()};
}

toml::value<double> number(float value)
{
    char text[32];
    const auto written = std::to_chars(text, text + sizeof text, value);
    double out = value;
    std::from_chars(text, written.ptr, out);
    return toml::value<double>(out);
}

template <std::size_t N> void putChoice(toml::table &item, std::string_view key, int value, const std::string_view (&names)[N])
{
    if (value >= 0 && value < int(N))
        item.insert(key, std::string(names[value]));
    else
        item.insert(key, std::int64_t(value));
}

class Fields {
public:
    Fields(const toml::table &item, std::string_view array, std::size_t index)
        : m_item(item), m_array(array), m_index(index)
    {
    }
    const std::optional<ParseFailure> &failure() const { return m_failure; }

    std::string text(std::string_view key)
    {
        const toml::node *node = m_item.get(key);
        if (!node || m_failure)
            return {};
        if (const auto *s = node->as_string())
            return s->get();
        fail(*node, key, "is not text");
        return {};
    }
    bool flag(std::string_view key)
    {
        const toml::node *node = m_item.get(key);
        if (!node || m_failure)
            return false;
        if (const auto *b = node->as_boolean())
            return b->get();
        fail(*node, key, "is not true or false");
        return false;
    }
    float real(std::string_view key, float fallback) { return real(m_item.get(key), key, fallback); }
    float real(const toml::node *node, std::string_view key, float fallback)
    {
        if (!node || m_failure)
            return fallback;
        if (const auto *d = node->as_floating_point())
            return static_cast<float>(d->get());
        if (const auto *i = node->as_integer())
            return static_cast<float>(i->get());
        fail(*node, key, "is not a number");
        return fallback;
    }
    std::int64_t whole(std::string_view key, std::int64_t fallback, std::int64_t min, std::int64_t max)
    {
        const toml::node *node = m_item.get(key);
        if (!node || m_failure)
            return fallback;
        if (const auto *i = node->as_integer(); i && i->get() >= min && i->get() <= max)
            return i->get();
        fail(*node, key, "is not a whole number from " + std::to_string(min) + " to " + std::to_string(max));
        return fallback;
    }
    template <std::size_t N>
    std::int64_t choice(std::string_view key, std::int64_t fallback, const std::string_view (&names)[N],
                        std::int64_t min, std::int64_t max)
    {
        const toml::node *node = m_item.get(key);
        if (!node || m_failure)
            return fallback;
        if (const auto *s = node->as_string()) {
            for (std::size_t i = 0; i < N; ++i)
                if (s->get() == names[i])
                    return std::int64_t(i);
            std::string known;
            for (const auto name : names)
                known += (known.empty() ? "\"" : ", \"") + std::string(name) + "\"";
            fail(*node, key, "is not one of " + known);
            return fallback;
        }
        return whole(key, fallback, min, max);
    }
    const toml::array *array(std::string_view key)
    {
        const toml::node *node = m_item.get(key);
        if (!node || m_failure)
            return nullptr;
        if (const auto *a = node->as_array())
            return a;
        fail(*node, key, "is not a list");
        return nullptr;
    }
    void fail(const toml::node &node, std::string_view key, std::string_view what)
    {
        if (m_failure)
            return;
        const auto &place = node.source().begin;
        m_failure = ParseFailure{static_cast<int>(place.line), static_cast<int>(place.column),
                                 std::string(m_array) + " " + std::to_string(m_index + 1) + ": \"" + std::string(key) +
                                     "\" " + std::string(what) + "."};
    }

private:
    const toml::table &m_item;
    std::string_view m_array;
    std::size_t m_index;
    std::optional<ParseFailure> m_failure;
};

template <class Item, class Read>
ListRead<Item> readItems(const toml::array &items, std::string_view array, Read read)
{
    std::vector<Item> out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto *item = items[i].as_table();
        if (!item) {
            const auto &place = items[i].source().begin;
            return ParseFailure{static_cast<int>(place.line), static_cast<int>(place.column),
                                std::string(array) + " " + std::to_string(i + 1) + " is not a table."};
        }
        Fields fields(*item, array, i);
        Item value = read(fields);
        if (fields.failure())
            return *fields.failure();
        out.push_back(std::move(value));
    }
    return out;
}

template <class Item> std::optional<ParseFailure> failureOf(const ListRead<Item> &read)
{
    if (const auto *failure = std::get_if<ParseFailure>(&read))
        return *failure;
    return std::nullopt;
}

constexpr std::string_view kFromCursor[] = {"widthAndHeight", "keepAspectRatio", "widthOnly"};
constexpr std::string_view kScaling[] = {"points", "scale"};

constexpr std::string_view kPlacing[] = {"cursor", "textStart", "clipAnimation"};
constexpr std::string_view kChange[] = {"add",          "set",
                                        "multiply",     "multiplyAdd",
                                        "gradientCharsUp", "gradientCharsDown",
                                        "gradientLinesUp", "gradientLinesDown"};
constexpr std::int64_t kByte = 255;

}

std::string listFileText(const ListFormat &format, toml::array items)
{
    toml::table table;
    table.insert(format.array, std::move(items));
    return formatTomlFile(table, ListFile::kVersion);
}

const ListFormat kShapesFormat{"shape",
                               "name",
                               {"name", "drawing", "fromCursor", "scaling"},
                               [](const toml::array &items) { return failureOf(shapesFromToml(items)); }};

ListRead<ShapePreset> shapesFromToml(const toml::array &items)
{
    return readItems<ShapePreset>(items, "shape", [](Fields &f) {
        ShapePreset shape;
        shape.name = utf16(f.text("name"));
        shape.shape = utf16(f.text("drawing"));
        shape.mode = int(f.choice("fromCursor", 0, kFromCursor, 0, kByte));
        shape.scalingMode = int(f.choice("scaling", 0, kScaling, 0, kByte));
        return shape;
    });
}

toml::array shapesToToml(const std::vector<ShapePreset> &shapes)
{
    toml::array out;
    for (const auto &shape : shapes) {
        toml::table item;
        item.insert("name", utf8(shape.name));
        item.insert("drawing", utf8(shape.shape));
        putChoice(item, "fromCursor", shape.mode, kFromCursor);
        putChoice(item, "scaling", shape.scalingMode, kScaling);
        out.push_back(std::move(item));
    }
    return out;
}

const ListFormat kTagsFormat{"tag",
                             "name",
                             {"name", "tag", "min", "max", "step", "decimals", "values", "placing", "change"},
                             [](const toml::array &items) { return failureOf(tagsFromToml(items)); }};

ListRead<AllTagsSetting> tagsFromToml(const toml::array &items)
{
    return readItems<AllTagsSetting>(items, "tag", [](Fields &f) {
        AllTagsSetting tag;
        tag.name = utf16(f.text("name"));
        tag.tag = utf16(f.text("tag"));
        tag.rangeMin = f.real("min", tag.rangeMin);
        tag.rangeMax = f.real("max", tag.rangeMax);
        tag.step = f.real("step", tag.step);
        tag.digitsAfterDot = static_cast<unsigned char>(f.whole("decimals", 0, 0, kByte));
        if (const toml::array *values = f.array("values")) {
            if (values->empty() || values->size() > tag.values.size())
                f.fail(*values, "values", "does not hold 1 to 4 numbers");
            for (std::size_t i = 0; i < values->size() && i < tag.values.size(); ++i)
                tag.values[i] = f.real(values->get(i), "values", 0.f);
            tag.numOfValues = static_cast<unsigned char>(std::clamp<std::size_t>(values->size(), 1, tag.values.size()));
        }
        tag.mode = static_cast<unsigned char>(f.choice("placing", 0, kPlacing, 0, kByte));
        tag.tagMode = int(f.choice("change", 0, kChange, INT32_MIN, INT32_MAX));
        return tag;
    });
}

toml::array tagsToToml(const std::vector<AllTagsSetting> &tags)
{
    toml::array out;
    for (const auto &tag : tags) {
        toml::table item;
        item.insert("name", utf8(tag.name));
        item.insert("tag", utf8(tag.tag));
        item.insert("min", number(tag.rangeMin));
        item.insert("max", number(tag.rangeMax));
        item.insert("step", number(tag.step));
        item.insert("decimals", std::int64_t(tag.digitsAfterDot));
        toml::array values;
        for (std::size_t i = 0; i < std::clamp<std::size_t>(tag.numOfValues, 1, tag.values.size()); ++i)
            values.push_back(number(tag.values[i]));
        item.insert("values", std::move(values));
        putChoice(item, "placing", tag.mode, kPlacing);
        putChoice(item, "change", tag.tagMode, kChange);
        out.push_back(std::move(item));
    }
    return out;
}

const ListFormat kFontCatalogsFormat{"catalog",
                                     "name",
                                     {"name", "fonts"},
                                     [](const toml::array &items) { return failureOf(fontCatalogsFromToml(items)); }};

ListRead<Catalog> fontCatalogsFromToml(const toml::array &items)
{
    return readItems<Catalog>(items, "catalog", [](Fields &f) {
        Catalog catalog;
        catalog.name = utf16(f.text("name"));
        if (const toml::array *fonts = f.array("fonts"))
            for (const auto &font : *fonts) {
                const auto *name = font.as_string();
                if (!name) {
                    f.fail(font, "fonts", "holds something that is not text");
                    break;
                }
                auto family = utf16(name->get());
                if (std::find(catalog.fonts.begin(), catalog.fonts.end(), family) == catalog.fonts.end())
                    catalog.fonts.push_back(std::move(family));
            }
        return catalog;
    });
}

toml::array fontCatalogsToToml(const std::vector<Catalog> &catalogs)
{
    toml::array out;
    for (const auto &catalog : catalogs) {
        toml::table item;
        item.insert("name", utf8(catalog.name));
        toml::array fonts;
        for (const auto &font : catalog.fonts)
            fonts.push_back(utf8(font));
        item.insert("fonts", std::move(fonts));
        out.push_back(std::move(item));
    }
    return out;
}

std::optional<std::vector<Catalog>> fontCatalogsFromText(std::string_view text)
{
    auto parsed = parseTomlFile(text, ListFile::kVersion);
    const auto *table = std::get_if<toml::table>(&parsed);
    if (!table)
        return std::nullopt;
    const toml::node *items = table->get(kFontCatalogsFormat.array);
    if (!items)
        return std::vector<Catalog>();
    if (!items->is_array())
        return std::nullopt;
    auto read = fontCatalogsFromToml(*items->as_array());
    if (auto *catalogs = std::get_if<std::vector<Catalog>>(&read))
        return std::move(*catalogs);
    return std::nullopt;
}

namespace {

std::optional<std::u16string> legacyToolText(const fs::path &path)
{
    auto bytes = application::readOwnedFile(path);
    if (!bytes)
        return std::nullopt;
    if (bytes->starts_with("\xEF\xBB\xBF"))
        bytes->erase(0, 3);
    QString text = QString::fromUtf8(bytes->data(), static_cast<qsizetype>(bytes->size()));
    // Windows text-mode reads turned CRLF into LF.
#ifdef _WIN32
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
#endif
    return text.toStdU16String();
}

}

std::optional<std::vector<ShapePreset>> readLegacyShapes(const fs::path &path)
{
    const auto text = legacyToolText(path);
    if (!text)
        return std::nullopt;
    return application::visual::parseShapePresets(*text);
}

std::optional<std::vector<AllTagsSetting>> readLegacyTags(const fs::path &path)
{
    const auto text = legacyToolText(path);
    if (!text)
        return std::nullopt;
    return application::visual::parseAllTags(*text);
}

std::optional<std::vector<Catalog>> readLegacyFontCatalogs(const fs::path &path)
{
    if (!application::readOwnedFile(path))
        return std::nullopt;
    std::vector<Catalog> out;
    if (const auto text = application::readFontCatalogFile(path))
        out = application::FontCatalogs::parse(*text);
    std::erase_if(out, [](const Catalog &c) { return c.name.empty(); });
    return out;
}

std::shared_ptr<ListFile> ListFile::make(QString path, QString legacy, const ListFormat &format, Convert convert,
                                         Report report)
{
    return std::make_shared<ListFile>(std::move(path), std::move(legacy), format, std::move(convert),
                                      std::move(report));
}

ListFile::ListFile(QString path, QString legacy, const ListFormat &format, Convert convert, Report report)
    : m_disk{std::move(path), std::move(legacy), &format, std::move(convert)}, m_report(std::move(report))
{
}

ListFile::~ListFile()
{
    if (m_inFlight && m_queue)
        m_queue->finishWrites();
    m_writeHere = true;
    if (m_pending)
        writePending();
}

std::optional<ParseFailure> ListFile::Disk::parse(std::string_view text, toml::table &into) const
{
    auto parsed = parseTomlFile(text, kVersion);
    if (auto *failure = std::get_if<ParseFailure>(&parsed))
        return *failure;
    auto &table = std::get<toml::table>(parsed);
    if (const toml::node *items = table.get(format->array)) {
        if (!items->is_array()) {
            const auto &place = items->source().begin;
            return ParseFailure{static_cast<int>(place.line), static_cast<int>(place.column),
                                "\"" + std::string(format->array) + "\" is not a list of tables."};
        }
        if (auto failure = format->check(*items->as_array()))
            return failure;
    }
    into = std::move(table);
    return std::nullopt;
}

void ListFile::report(const std::vector<Event> &events)
{
    for (const Event &event : events) {
        switch (event.kind) {
        case Event::Converted:
            if (m_report.converted)
                m_report.converted(pathOf(m_disk.legacy), pathOf(m_disk.path));
            break;
        case Event::Problem:
            if (m_report.problem)
                m_report.problem(event.problem);
            break;
        case Event::Replaced:
            if (m_report.replaced)
                m_report.replaced(event.problem);
            break;
        }
    }
}

std::unique_ptr<QLockFile> ListFile::Disk::lockFolder() const
{
    const QString folder = QFileInfo(path).absolutePath();
    QDir().mkpath(folder);
    auto lock = std::make_unique<QLockFile>(folder + QStringLiteral("/.settings.lock"));
    if (!lock->tryLock(2000) && lock->error() == QLockFile::LockFailedError)
        return nullptr;
    return lock;
}

void ListFile::convert()
{
    waitForBackground();
    m_state.deferred = false;
    if (m_disk.legacy.isEmpty() || m_state.converted || QFileInfo::exists(m_disk.path) ||
        !QFileInfo::exists(m_disk.legacy))
        return;
    const auto lock = m_disk.lockFolder();
    m_state.deferred = !lock;
    if (!lock)
        return;
    std::vector<Event> events;
    convertLocked(m_disk, m_state, events);
    report(events);
}

void ListFile::convertLocked(const Disk &disk, State &state, std::vector<Event> &events)
{
    state.deferred = false;
    if (disk.legacy.isEmpty() || state.converted)
        return;
    std::optional<toml::array> items;
    bool unreadable = false;
    const auto conversion = application::convertOnce(pathOf(disk.path), pathOf(disk.legacy), [&] {
        if (disk.convert)
            items = disk.convert(pathOf(disk.legacy));
        if (!items) {
            unreadable = true;
            return false;
        }
        return application::writeFileAtomically(pathOf(disk.path), listFileText(*disk.format, *items),
                                                pathOf(disk.legacy));
    });
    if (conversion == application::Conversion::Converted) {
        state.writable = true;
        events.push_back({Event::Converted, {}});
    } else if (unreadable) {
        state.writable = false;
        if (!std::exchange(state.reportedUnreadable, true))
            events.push_back({Event::Problem, {pathOf(disk.legacy), {}, {0, 0, "It could not be read."}}});
    } else if (conversion == application::Conversion::Failed) {
        state.converted = std::move(items);
    }
}

std::optional<toml::array> ListFile::read()
{
    convert();
    if (!m_state.writable)
        return std::nullopt;
    if (m_pending)
        return *m_pending;
    if (m_state.converted)
        return *m_state.converted;
    if (m_state.deferred)
        return m_disk.convert ? m_disk.convert(pathOf(m_disk.legacy)) : std::nullopt;
    toml::table table;
    const auto load = application::loadOwnedFile(pathOf(m_disk.path),
                                                 [&](std::string_view text) { return m_disk.parse(text, table); });
    if (load.state == application::FileState::Missing)
        return std::nullopt;
    if (load.problem) {
        m_state.writable = !load.problem->keptAs.empty();
        report({{Event::Problem, *load.problem}});
        return std::nullopt;
    }
    if (auto *items = table[m_disk.format->array].as_array())
        return std::move(*items);
    return toml::array();
}

toml::table ListFile::Disk::merged(toml::array items, toml::table onDisk) const
{
    std::map<std::string, std::vector<const toml::table *>> before;
    if (const auto *old = onDisk[format->array].as_array())
        for (const auto &node : *old)
            if (const auto *item = node.as_table())
                before[item->get(format->name) ? item->get(format->name)->value_or(std::string()) : std::string()]
                    .push_back(item);
    std::map<std::string, std::size_t> seen;
    for (auto &node : items) {
        auto *item = node.as_table();
        if (!item)
            continue;
        const std::string name = item->get(format->name) ? item->get(format->name)->value_or(std::string()) : "";
        const auto it = before.find(name);
        const std::size_t k = seen[name]++;
        if (it == before.end() || k >= it->second.size())
            continue;
        for (const auto &[key, value] : *it->second[k])
            if (std::ranges::find(format->keys, key.str()) == format->keys.end() && !item->contains(key.str()))
                item->insert(key, value);
    }
    onDisk.insert_or_assign(format->array, std::move(items));
    return onDisk;
}

ListFile::Write ListFile::write(toml::array items)
{
    m_pending = std::move(items);
    return writePending();
}

bool ListFile::flush()
{
    const bool wasHere = std::exchange(m_writeHere, true);
    waitForBackground();
    const bool done = !m_pending || writePending() != Write::Waiting;
    m_writeHere = wasHere;
    return done;
}

void ListFile::writeInBackground(FileWriteQueue &queue, std::function<void(Write)> written)
{
    m_queue = &queue;
    m_written = std::move(written);
}

ListFile::Write ListFile::writePending()
{
    if (m_queue && !m_writeHere) {
        if (!m_inFlight)
            startInBackground();
        return Write::Waiting;
    }
    Attempt next{std::move(*m_pending), m_state, {}};
    m_pending.reset();
    attempt(m_disk, next);
    return finish(std::move(next));
}

void ListFile::attempt(const Disk &disk, Attempt &a)
{
    std::unique_ptr<QLockFile> lock;
    if (a.state.writable) {
        lock = disk.lockFolder();
        if (!lock) {
            a.result = Write::Waiting;
            return;
        }
        convertLocked(disk, a.state, a.events);
    }
    a.result = Write::Failed;
    if (!a.state.writable)
        return;
    if (a.state.converted) {
        const std::string text = listFileText(*disk.format, a.items);
        const auto conversion = application::convertOnce(pathOf(disk.path), pathOf(disk.legacy), [&] {
            return application::writeFileAtomically(pathOf(disk.path), text, pathOf(disk.legacy));
        });
        if (conversion == application::Conversion::Failed) {
            a.state.converted = std::move(a.items);
            return;
        }
        a.state.converted.reset();
        if (conversion == application::Conversion::Converted) {
            a.result = Write::Written;
            return;
        }
    }
    toml::table onDisk;
    const auto load = application::loadOwnedFile(pathOf(disk.path),
                                                 [&](std::string_view text) { return disk.parse(text, onDisk); });
    if (load.problem) {
        a.state.writable = !load.problem->keptAs.empty();
        a.events.push_back({a.state.writable ? Event::Replaced : Event::Problem, *load.problem});
        if (!a.state.writable)
            return;
    }
    const std::string text = formatTomlFile(disk.merged(std::move(a.items), std::move(onDisk)), kVersion);
    if (application::writeFileAtomically(pathOf(disk.path), text))
        a.result = Write::Written;
}

ListFile::Write ListFile::finish(Attempt a)
{
    m_state = std::move(a.state);
    report(a.events);
    if (a.result == Write::Waiting) {
        if (!m_pending)
            m_pending = std::move(a.items);
        retryLater();
    }
    return a.result;
}

void ListFile::startInBackground()
{
    m_inFlight = true;
    auto next = std::make_shared<Attempt>(Attempt{std::move(*m_pending), m_state, {}});
    m_pending.reset();
    m_queue->put(m_disk.path, [disk = m_disk, next] {
        attempt(disk, *next);
        return next->result == Write::Written;
    }, [self = weak_from_this(), next](bool) {
        if (const auto file = self.lock())
            file->finishInBackground(std::move(*next));
    });
}

void ListFile::finishInBackground(Attempt a)
{
    m_inFlight = false;
    const Write result = finish(std::move(a));
    if (m_written)
        m_written(result);
    if (m_pending && result != Write::Waiting && !m_writeHere)
        writePending();
}

void ListFile::waitForBackground()
{
    while (m_inFlight && m_queue)
        m_queue->finishWrites();
}

void ListFile::retryLater()
{
    if (m_retryQueued || !QCoreApplication::instance())
        return;
    m_retryQueued = true;
    std::weak_ptr<ListFile> self = weak_from_this();
    QTimer::singleShot(1000, QCoreApplication::instance(), [self] {
        if (auto file = self.lock()) {
            file->m_retryQueued = false;
            if (file->m_pending)
                file->writePending();
        }
    });
}

}
