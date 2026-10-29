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

// A float in its shortest text (20, not 20.000000), which reads back as the
// same float.
toml::value<double> number(float value)
{
    char text[32];
    const auto written = std::to_chars(text, text + sizeof text, value);
    double out = value;
    std::from_chars(text, written.ptr, out);
    return toml::value<double>(out);
}

// A choice by its name, or its number when it has none.
template <std::size_t N> void putChoice(toml::table &item, std::string_view key, int value, const std::string_view (&names)[N])
{
    if (value >= 0 && value < int(N))
        item.insert(key, std::string(names[value]));
    else
        item.insert(key, std::int64_t(value));
}

// An item's fields read in order: the first that fails stops the rest and
// says where.
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

// Each item of `items` read by `read`, which returns the item's value.
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

// ---- rules.toml

struct RuleOption {
    std::string_view key;
    int bit;
};
constexpr RuleOption kRuleOptions[] = {
    {"matchCase", application::kReplacerMatchCase},   {"lowerCase", application::kReplacerLowerCase},
    {"upperCase", application::kReplacerUpperCase},   {"keepCase", application::kReplacerUnchangedCase},
    {"onlyInTags", application::kReplacerOnlyTags}, {"onlyInText", application::kReplacerOnlyText},
};

// ---- shapes.toml

constexpr std::string_view kFromCursor[] = {"widthAndHeight", "keepAspectRatio", "widthOnly"};
constexpr std::string_view kScaling[] = {"points", "scale"};

// ---- tags.toml

constexpr std::string_view kPlacing[] = {"cursor", "textStart", "clipAnimation"};
constexpr std::string_view kChange[] = {"add",          "set",
                                        "multiply",     "multiplyAdd",
                                        "gradientCharsUp", "gradientCharsDown",
                                        "gradientLinesUp", "gradientLinesDown"};
constexpr std::int64_t kByte = 255; // legacy's unsigned char fields

} // namespace

std::string listFileText(const ListFormat &format, toml::array items)
{
    toml::table table;
    table.insert(format.array, std::move(items));
    return formatTomlFile(table, ListFile::kVersion);
}

const ListFormat kRulesFormat{"rule",
                              "description",
                              {"description", "find", "replace", "enabled", "matchCase", "lowerCase", "upperCase",
                               "keepCase", "onlyInTags", "onlyInText"},
                              [](const toml::array &items) { return failureOf(rulesFromToml(items)); }};

ListRead<application::ReplacerRule> rulesFromToml(const toml::array &items)
{
    return readItems<application::ReplacerRule>(items, "rule", [](Fields &f) {
        application::ReplacerRule rule;
        rule.description = u8(f.text("description"));
        rule.find = u8(f.text("find"));
        rule.replace = u8(f.text("replace"));
        rule.checked = f.flag("enabled");
        for (const auto &option : kRuleOptions)
            if (f.flag(option.key))
                rule.options |= option.bit;
        return rule;
    });
}

toml::array rulesToToml(const std::vector<application::ReplacerRule> &rules)
{
    toml::array out;
    for (const auto &rule : rules) {
        toml::table item;
        item.insert("description", utf8(rule.description));
        item.insert("find", utf8(rule.find));
        item.insert("replace", utf8(rule.replace));
        item.insert("enabled", rule.checked);
        for (const auto &option : kRuleOptions)
            if (rule.options & option.bit)
                item.insert(option.key, true);
        out.push_back(std::move(item));
    }
    return out;
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

// ---- The legacy files

namespace {

// LoadSettings' read of a visual tool's file: UTF-8 (the BOM dropped; the
// Windows build's text-mode read made CRLF LF).
std::optional<std::u16string> legacyToolText(const fs::path &path)
{
    auto bytes = application::readOwnedFile(path);
    if (!bytes)
        return std::nullopt;
    if (bytes->starts_with("\xEF\xBB\xBF"))
        bytes->erase(0, 3);
    QString text = QString::fromUtf8(bytes->data(), static_cast<qsizetype>(bytes->size()));
#ifdef _WIN32
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
#endif
    return text.toStdU16String();
}

} // namespace

std::optional<std::vector<ShapePreset>> readLegacyShapes(const fs::path &path)
{
    const auto text = legacyToolText(path);
    if (!text)
        return std::nullopt;
    return application::visual::parseShapePresets(*text);
}

std::optional<std::vector<AllTagsSetting>> readLegacyTags(const fs::path &path)
{
    // A file of an older version (no "HYDRA2.0") gave legacy's defaults,
    // which legacy then wrote over it.
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
    // A header with no '=' read as a catalog with no name, which nothing
    // could choose or manage.
    std::erase_if(out, [](const Catalog &c) { return c.name.empty(); });
    return out;
}

// ---- ListFile

std::shared_ptr<ListFile> ListFile::make(QString path, QString legacy, const ListFormat &format, Convert convert,
                                         Report report)
{
    return std::make_shared<ListFile>(std::move(path), std::move(legacy), format, std::move(convert),
                                      std::move(report));
}

ListFile::ListFile(QString path, QString legacy, const ListFormat &format, Convert convert, Report report)
    : m_path(std::move(path)), m_legacy(std::move(legacy)), m_format(format), m_convert(std::move(convert)),
      m_report(std::move(report))
{
}

ListFile::~ListFile()
{
    if (m_pending)
        writePending();
}

std::optional<ParseFailure> ListFile::parse(std::string_view text, toml::table &into) const
{
    auto parsed = parseTomlFile(text, kVersion);
    if (auto *failure = std::get_if<ParseFailure>(&parsed))
        return *failure;
    auto &table = std::get<toml::table>(parsed);
    if (const toml::node *items = table.get(m_format.array)) {
        if (!items->is_array()) {
            const auto &place = items->source().begin;
            return ParseFailure{static_cast<int>(place.line), static_cast<int>(place.column),
                                "\"" + std::string(m_format.array) + "\" is not a list of tables."};
        }
        if (auto failure = m_format.check(*items->as_array()))
            return failure;
    }
    into = std::move(table);
    return std::nullopt;
}

void ListFile::problem(application::FileProblem problem)
{
    if (m_report.problem)
        m_report.problem(problem);
}

void ListFile::convert()
{
    if (m_legacy.isEmpty() || m_converted)
        return;
    std::optional<toml::array> items;
    bool unreadable = false;
    const auto conversion = application::convertOnce(pathOf(m_path), pathOf(m_legacy), [&] {
        if (m_convert)
            items = m_convert(pathOf(m_legacy));
        if (!items) {
            unreadable = true;
            return false;
        }
        return application::writeFileAtomically(pathOf(m_path), listFileText(m_format, *items), pathOf(m_legacy));
    });
    if (conversion == application::Conversion::Converted) {
        m_writable = true;
        if (m_report.converted)
            m_report.converted(pathOf(m_legacy), pathOf(m_path));
    } else if (unreadable) {
        // Not written over by defaults: the next start tries again.
        m_writable = false;
        if (!std::exchange(m_reportedUnreadable, true))
            problem({pathOf(m_legacy), {}, {0, 0, "It could not be read."}});
    } else if (conversion == application::Conversion::Failed) {
        m_converted = std::move(items); // written, and the old file moved, with the next write
    }
}

std::optional<toml::array> ListFile::read()
{
    convert();
    // A legacy file that cannot be read, or a broken file that could not be
    // moved aside: the defaults, reported once.
    if (!m_writable)
        return std::nullopt;
    if (m_converted)
        return *m_converted;
    toml::table table;
    const auto load =
        application::loadOwnedFile(pathOf(m_path), [&](std::string_view text) { return parse(text, table); });
    if (load.state == application::FileState::Missing)
        return std::nullopt;
    if (load.problem) {
        m_writable = !load.problem->keptAs.empty();
        problem(*load.problem);
        return std::nullopt;
    }
    if (auto *items = table[m_format.array].as_array())
        return std::move(*items);
    return toml::array();
}

toml::table ListFile::mergedWithFile(toml::array items) const
{
    // The file as it is now keeps what this HikariSub does not know: its
    // other keys, and each item's (the k-th of a name gets the k-th's).
    toml::table out;
    if (const auto bytes = application::readOwnedFile(pathOf(m_path)))
        parse(*bytes, out);
    std::map<std::string, std::vector<const toml::table *>> before;
    if (const auto *old = out[m_format.array].as_array())
        for (const auto &node : *old)
            if (const auto *item = node.as_table())
                before[item->get(m_format.name) ? item->get(m_format.name)->value_or(std::string()) : std::string()]
                    .push_back(item);
    std::map<std::string, std::size_t> seen;
    for (auto &node : items) {
        auto *item = node.as_table();
        if (!item)
            continue;
        const std::string name = item->get(m_format.name) ? item->get(m_format.name)->value_or(std::string()) : "";
        const auto it = before.find(name);
        const std::size_t k = seen[name]++;
        if (it == before.end() || k >= it->second.size())
            continue;
        for (const auto &[key, value] : *it->second[k])
            if (std::ranges::find(m_format.keys, key.str()) == m_format.keys.end() && !item->contains(key.str()))
                item->insert(key, value);
    }
    out.insert_or_assign(m_format.array, std::move(items));
    return out;
}

ListFile::Write ListFile::write(toml::array items)
{
    m_pending = std::move(items);
    return writePending();
}

bool ListFile::flush()
{
    return !m_pending || writePending() != Write::Waiting;
}

ListFile::Write ListFile::writePending()
{
    if (!m_writable) {
        m_pending.reset();
        return Write::Failed;
    }
    const QString folder = QFileInfo(m_path).absolutePath();
    QDir().mkpath(folder);
    // One writer at a time on the folder (settings_files.h's lock); a folder
    // that cannot hold a lock is written without one.
    QLockFile lock(folder + QStringLiteral("/.settings.lock"));
    if (!lock.tryLock(2000) && lock.error() == QLockFile::LockFailedError) {
        retryLater();
        return Write::Waiting;
    }
    const std::string text = formatTomlFile(mergedWithFile(std::move(*m_pending)), kVersion);
    m_pending.reset();
    bool written = false;
    if (m_converted) {
        // The conversion's file could not be written before: it is this
        // list, and the old file moves aside once it is.
        written = application::convertOnce(pathOf(m_path), pathOf(m_legacy), [&] {
                      return application::writeFileAtomically(pathOf(m_path), text, pathOf(m_legacy));
                  }) != application::Conversion::Failed;
        if (written)
            m_converted.reset();
    } else {
        written = application::writeFileAtomically(pathOf(m_path), text);
    }
    return written ? Write::Written : Write::Failed;
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
            file->flush();
        }
    });
}

} // namespace hikari::ui
