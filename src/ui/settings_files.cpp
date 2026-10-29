#include "settings_files.h"

#include "hikari/application/hotkeys.h"
#include "toml_file.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QPointer>
#include <QSettings>
#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <set>
#include <span>
#include <sstream>

namespace hikari::ui {

namespace {

namespace fs = std::filesystem;
using application::SettingDefinition;
using application::SettingType;
using application::SettingValue;
using StringList = std::vector<std::string>;

QString qs(std::string_view s)
{
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}

fs::path pathOf(const QString &path)
{
    return fs::path(path.toStdU16String());
}

std::vector<std::string_view> segments(std::string_view id)
{
    std::vector<std::string_view> out;
    for (std::size_t start = 0;;) {
        const auto dot = id.find('.', start);
        out.push_back(id.substr(start, dot == std::string_view::npos ? dot : dot - start));
        if (dot == std::string_view::npos)
            return out;
        start = dot + 1;
    }
}

// ---- Which file

bool rememberedByTheApp(std::string_view id)
{
    static const std::set<std::string_view> ids{
        "find.recentFinds",       "find.recentReplacements", "findInSubs.recentFilters", "findInSubs.recentPaths",
        "selectLines.recentSelections", "colourPicker.recentColours", "automation.dialogAnswers",
        "automation.lastScript",  "styles.lastCatalog",      "privacy.usageDataId",      "updater.skippedVersion"};
    return id.starts_with("recent.") || ids.contains(id);
}

// [workspace] keys a user chooses on purpose.
bool chosenWorkspaceSetting(std::string_view id)
{
    return id == "toolbar.actions" || id == "toolbar.alignment" || id == "program.tabTextMaxChars";
}

// ---- TOML values

std::optional<SettingValue> fromToml(const toml::node &node)
{
    if (const auto *v = node.as_boolean())
        return v->get();
    if (const auto *v = node.as_integer())
        return v->get();
    if (const auto *v = node.as_floating_point())
        return static_cast<std::int64_t>(std::llround(v->get()));
    if (const auto *v = node.as_string())
        return v->get();
    if (const auto *array = node.as_array()) {
        StringList out;
        for (const auto &entry : *array) {
            if (const auto *s = entry.as_string())
                out.push_back(s->get());
            else if (const auto value = fromToml(entry); value && !std::holds_alternative<StringList>(*value))
                out.push_back(std::get<std::string>(application::convertSetting(*value, SettingType::String)));
        }
        return out;
    }
    return std::nullopt;
}

void putToml(toml::table &table, std::string_view key, const SettingValue &value)
{
    std::visit(
        [&](const auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, StringList>) {
                toml::array array;
                for (const auto &entry : v)
                    array.push_back(entry);
                table.insert_or_assign(key, std::move(array));
            } else
                table.insert_or_assign(key, v);
        },
        value);
}

// The table that holds the id's last segment; made on the way when `create`.
toml::table *tomlParent(toml::table &root, std::string_view id, bool create)
{
    const auto parts = segments(id);
    toml::table *table = &root;
    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        auto *next = table->get(parts[i]);
        if (!next || !next->is_table()) {
            if (!create)
                return nullptr;
            table->insert_or_assign(parts[i], toml::table());
            next = table->get(parts[i]);
        }
        table = next->as_table();
    }
    return table;
}

const toml::node *tomlNode(const toml::table &root, std::string_view id)
{
    auto *parent = tomlParent(const_cast<toml::table &>(root), id, false);
    return parent ? parent->get(segments(id).back()) : nullptr;
}

void eraseToml(toml::table &root, std::string_view id)
{
    const auto parts = segments(id);
    std::vector<toml::table *> chain{&root};
    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        auto *next = chain.back()->get(parts[i]);
        if (!next || !next->is_table())
            return;
        chain.push_back(next->as_table());
    }
    chain.back()->erase(parts.back());
    // Tables left empty go too.
    for (std::size_t i = chain.size() - 1; i > 0 && chain[i]->empty(); --i)
        chain[i - 1]->erase(parts[i - 1]);
}

// ---- JSON values

QJsonValue toJson(const SettingValue &value)
{
    return std::visit(
        [](const auto &v) -> QJsonValue {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, bool>)
                return v;
            else if constexpr (std::is_same_v<T, std::int64_t>)
                return static_cast<qint64>(v);
            else if constexpr (std::is_same_v<T, std::string>)
                return qs(v);
            else {
                QJsonArray array;
                for (const auto &entry : v)
                    array.append(qs(entry));
                return array;
            }
        },
        value);
}

std::optional<SettingValue> fromJson(const QJsonValue &value)
{
    if (value.isBool())
        return value.toBool();
    if (value.isDouble())
        return static_cast<std::int64_t>(std::llround(value.toDouble()));
    if (value.isString())
        return value.toString().toStdString();
    if (value.isArray()) {
        StringList out;
        for (const auto &entry : value.toArray())
            if (entry.isString())
                out.push_back(entry.toString().toStdString());
            else if (const auto v = fromJson(entry); v && !std::holds_alternative<StringList>(*v))
                out.push_back(std::get<std::string>(application::convertSetting(*v, SettingType::String)));
        return out;
    }
    return std::nullopt;
}

QJsonValue jsonNode(const QJsonObject &root, std::string_view id)
{
    const auto parts = segments(id);
    QJsonObject object = root;
    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        const QJsonValue next = object.value(qs(parts[i]));
        if (!next.isObject())
            return QJsonValue::Undefined;
        object = next.toObject();
    }
    return object.value(qs(parts.back()));
}

// Sets (or, for Undefined, removes) the value at `parts`; objects left empty go.
void putJson(QJsonObject &object, std::span<const std::string_view> parts, const QJsonValue &value)
{
    const QString key = qs(parts.front());
    if (parts.size() == 1) {
        if (value.isUndefined())
            object.remove(key);
        else
            object.insert(key, value);
        return;
    }
    QJsonObject child = object.value(key).toObject();
    if (value.isUndefined() && !object.value(key).isObject())
        return;
    putJson(child, parts.subspan(1), value);
    if (child.isEmpty())
        object.remove(key);
    else
        object.insert(key, child);
}

void putJson(QJsonObject &root, std::string_view id, const QJsonValue &value)
{
    const auto parts = segments(id);
    putJson(root, parts, value);
}

// ---- Rows "<key>\t<rest>" as a table or object keyed by their first field

struct Row {
    QString key;
    QString rest;
};

// The rows, or nothing when one has no tab or a key comes twice (then the
// list is kept as it is).
std::optional<std::vector<Row>> keyedRows(const StringList &rows)
{
    std::vector<Row> out;
    QStringList seen;
    for (const auto &row : rows) {
        const QString text = qs(row);
        const auto tab = text.indexOf(QLatin1Char('\t'));
        if (tab < 0 || seen.contains(text.left(tab)))
            return std::nullopt;
        seen << text.left(tab);
        out.push_back({text.left(tab), text.mid(tab + 1)});
    }
    return out;
}

QString rowText(const QString &key, const QString &rest)
{
    return key + QLatin1Char('\t') + rest;
}

QString compactJson(const QJsonValue &value)
{
    if (value.isObject())
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
}

// JSON as TOML: nothing for a null, which TOML has no value for. Whole
// numbers are integers, as Qt writes them.
std::optional<toml::array> tomlArrayOf(const QJsonArray &array);
std::optional<toml::table> tomlTableOf(const QJsonObject &object);

bool putJsonInToml(const QJsonValue &v, const std::function<void(toml::node &&)> &put)
{
    if (v.isBool()) {
        toml::value<bool> n(v.toBool());
        put(std::move(n));
    } else if (v.isDouble()) {
        const double d = v.toDouble();
        if (std::floor(d) == d && std::abs(d) < 9.0e15) {
            toml::value<std::int64_t> n(static_cast<std::int64_t>(d));
            put(std::move(n));
        } else {
            toml::value<double> n(d);
            put(std::move(n));
        }
    } else if (v.isString()) {
        toml::value<std::string> n(v.toString().toStdString());
        put(std::move(n));
    } else if (v.isArray()) {
        auto a = tomlArrayOf(v.toArray());
        if (!a)
            return false;
        put(std::move(*a));
    } else if (v.isObject()) {
        auto t = tomlTableOf(v.toObject());
        if (!t)
            return false;
        put(std::move(*t));
    } else
        return false;
    return true;
}

std::optional<toml::array> tomlArrayOf(const QJsonArray &array)
{
    toml::array out;
    for (const auto &v : array)
        if (!putJsonInToml(v, [&](toml::node &&n) { out.push_back(std::move(n)); }))
            return std::nullopt;
    return out;
}

std::optional<toml::table> tomlTableOf(const QJsonObject &object)
{
    toml::table out;
    for (auto it = object.begin(); it != object.end(); ++it) {
        const std::string key = it.key().toStdString();
        if (!putJsonInToml(*it, [&](toml::node &&n) { out.insert_or_assign(key, std::move(n)); }))
            return std::nullopt;
    }
    return out;
}

QJsonValue jsonOf(const toml::node &node)
{
    if (const auto *v = node.as_boolean())
        return v->get();
    if (const auto *v = node.as_integer())
        return static_cast<qint64>(v->get());
    if (const auto *v = node.as_floating_point())
        return v->get();
    if (const auto *v = node.as_string())
        return qs(v->get());
    if (const auto *a = node.as_array()) {
        QJsonArray out;
        for (const auto &entry : *a)
            out.append(jsonOf(entry));
        return out;
    }
    if (const auto *t = node.as_table()) {
        QJsonObject out;
        for (const auto &[key, value] : *t)
            out.insert(qs(key.str()), jsonOf(value));
        return out;
    }
    return {};
}

// ---- Tag buttons: editor.tagButton1..20 are [[editor.tagButton]] slots

constexpr std::string_view kTagButtonSlots = "editor.tagButton";

int tagButtonSlot(const SettingDefinition &setting)
{
    if (setting.scope != application::SettingScope::Collection || !setting.id.starts_with(kTagButtonSlots))
        return -1;
    const auto digits = setting.id.substr(kTagButtonSlots.size());
    if (digits.empty() || !std::ranges::all_of(digits, [](char c) { return c >= '0' && c <= '9'; }))
        return -1;
    return std::stoi(std::string(digits)) - 1;
}

// TagButtonsController::toLegacy's text: "{\n\t<tag>\n\t<type>\n\t<name>[\n\t<icon>]\n}".
std::string tagButtonText(const std::string &tag, std::int64_t type, const std::string &name, const std::string &icon)
{
    std::string text = "{\n\t" + tag + "\n\t" + std::to_string(type) + "\n\t" + name;
    if (!icon.empty())
        text += "\n\t" + icon;
    return text + "\n}";
}

// The slot's fields, or { text = "…" } when the text is not one the fields
// give back.
toml::table tagButtonTable(const std::string &text)
{
    toml::table out;
    if (text.size() > 4 && text.starts_with("{\n") && text.ends_with("\n}")) {
        std::vector<std::string> entries;
        const std::string body = text.substr(2, text.size() - 4);
        for (std::size_t start = 0;;) {
            const auto nl = body.find('\n', start);
            entries.push_back(body.substr(start, nl == std::string::npos ? nl : nl - start));
            if (nl == std::string::npos)
                break;
            start = nl + 1;
        }
        if ((entries.size() == 3 || entries.size() == 4) &&
            std::ranges::all_of(entries, [](const std::string &e) { return e.starts_with('\t'); })) {
            const std::string tag = entries[0].substr(1), typeText = entries[1].substr(1), name = entries[2].substr(1);
            const std::string icon = entries.size() == 4 ? entries[3].substr(1) : std::string();
            const bool number = !typeText.empty() && typeText.size() < 10 &&
                                std::ranges::all_of(typeText, [](char c) { return c >= '0' && c <= '9'; });
            if (number) {
                const std::int64_t type = std::stoll(typeText);
                if (tagButtonText(tag, type, name, icon) == text) {
                    out.insert("tag", tag);
                    out.insert("type", type);
                    out.insert("name", name);
                    if (!icon.empty())
                        out.insert("icon", icon);
                    return out;
                }
            }
        }
    }
    out.insert("text", text);
    return out;
}

std::optional<std::string> tagButtonOf(const toml::table &slot)
{
    if (slot.empty())
        return std::nullopt;
    if (const auto text = slot["text"].value<std::string>())
        return *text;
    return tagButtonText(slot["tag"].value_or(std::string()), slot["type"].value_or(std::int64_t{0}),
                         slot["name"].value_or(std::string()), slot["icon"].value_or(std::string()));
}

// ---- Hotkeys

constexpr std::string_view kWindowTables[application::kHotkeyWindows] = {"global", "grid", "editor", "video", "audio"};
constexpr std::string_view kAutomationTable = "automation";

// Legacy accelerator text as Qt's portable text, when it comes back the same.
std::string hotkeyText(const std::string &accel)
{
    const std::string keys = application::qtKeysOfAccel(accel);
    return !keys.empty() && application::accelOfQtKeys(keys) == accel ? keys : accel;
}

std::string accelOfHotkeyText(const std::string &text)
{
    const std::string accel = application::accelOfQtKeys(text);
    return !accel.empty() && application::qtKeysOfAccel(accel) == text ? accel : text;
}

std::string hotkeyLabel(int id)
{
    const auto symbol = application::hotkeySymbol(id);
    return symbol.empty() ? std::to_string(id) : std::string(symbol);
}

std::optional<int> hotkeyIdOfLabel(const std::string &label)
{
    if (!label.empty() && std::ranges::all_of(label, [](char c) { return c >= '0' && c <= '9'; }))
        return label.size() < 10 ? std::optional(std::stoi(label)) : std::nullopt;
    const int id = application::hotkeyIdOf(label);
    return id ? std::optional(id) : std::nullopt;
}

bool audioFile(int window)
{
    return window == application::AudioHotkey;
}

application::HotkeyMap defaultsOf(bool audio)
{
    application::HotkeyMap map;
    application::loadDefaultHotkeys(map, audio);
    std::erase_if(map, [audio](const auto &entry) { return audioFile(entry.first.type) != audio; });
    return map;
}

// The binding's text in a table: a string, or the first of an array.
std::optional<std::string> bindingText(const toml::node &node)
{
    if (const auto *s = node.as_string())
        return s->get();
    if (const auto *a = node.as_array(); a && !a->empty())
        if (const auto *s = (*a)[0].as_string())
            return s->get();
    return std::nullopt;
}

std::optional<StringList> readHotkeys(const toml::table &file, bool audio)
{
    application::HotkeyMap map = defaultsOf(audio);
    bool any = false;
    for (int window = 0; window < application::kHotkeyWindows; ++window) {
        if (audioFile(window) != audio)
            continue;
        const auto *table = file[kWindowTables[window]].as_table();
        if (!table)
            continue;
        for (const auto &[key, node] : *table) {
            const auto id = hotkeyIdOfLabel(std::string(key.str()));
            const auto text = bindingText(node);
            if (!id || !text)
                continue;
            any = true;
            const application::HotkeyId at{*id, window};
            if (text->empty())
                map.erase(at);
            else
                map[at] = application::Hotkey{std::string(), accelOfHotkeyText(*text)};
        }
    }
    if (!any)
        return std::nullopt;
    return application::hotkeyLines(map, audio);
}

void writeHotkeys(toml::table &file, bool audio, const StringList *lines)
{
    application::HotkeyMap stored;
    if (lines)
        application::readHotkeyLines(stored, *lines);
    const application::HotkeyMap defaults = defaultsOf(audio);
    for (int window = 0; window < application::kHotkeyWindows; ++window) {
        if (audioFile(window) != audio)
            continue;
        // What a later version's commands hold stays.
        toml::table table;
        if (const auto *old = file[kWindowTables[window]].as_table())
            for (const auto &[key, node] : *old)
                if (!hotkeyIdOfLabel(std::string(key.str())) && node.is_string())
                    table.insert(key, node);
        if (lines) {
            std::map<application::HotkeyId, std::string> changes;
            for (const auto &[at, hotkey] : stored)
                if (at.type == window && at.id >= 100 && !hotkey.accel.empty())
                    if (const auto it = defaults.find(at); it == defaults.end() || it->second.accel != hotkey.accel)
                        changes[at] = hotkeyText(hotkey.accel);
            for (const auto &[at, hotkey] : defaults)
                if (at.type == window && !hotkey.accel.empty())
                    if (const auto it = stored.find(at); it == stored.end() || it->second.accel.empty())
                        changes[at] = std::string();
            for (const auto &[at, text] : changes)
                table.insert_or_assign(hotkeyLabel(at.id), text);
        }
        if (table.empty())
            file.erase(kWindowTables[window]);
        else
            file.insert_or_assign(kWindowTables[window], std::move(table));
    }
}

// shortcuts.automationMacros: "legacy name\tkeys\tmacro\tsha256" rows.
std::optional<StringList> readMacroHotkeys(const toml::table &file)
{
    const auto *table = file[kAutomationTable].as_table();
    if (!table || table->empty())
        return std::nullopt;
    StringList rows;
    for (const auto &[key, node] : *table) {
        std::string keys, macro, sha;
        if (const auto *entry = node.as_table()) {
            keys = (*entry)["keys"].value_or(std::string());
            macro = (*entry)["macro"].value_or(std::string());
            sha = (*entry)["sha256"].value_or(std::string());
        } else if (const auto text = bindingText(node))
            keys = *text;
        rows.push_back(std::string(key.str()) + '\t' + keys + '\t' + macro + '\t' + sha);
    }
    return rows;
}

void writeMacroHotkeys(toml::table &file, const StringList *rows)
{
    toml::table table;
    if (rows)
        for (const auto &row : *rows) {
            const QStringList v = qs(row).split(QLatin1Char('\t'));
            toml::table entry;
            entry.insert("keys", v.value(1).toStdString());
            entry.insert("macro", v.value(2).toStdString());
            entry.insert("sha256", v.value(3).toStdString());
            table.insert_or_assign(v.value(0).toStdString(), std::move(entry));
        }
    if (table.empty())
        file.erase(kAutomationTable);
    else
        file.insert_or_assign(kAutomationTable, std::move(table));
}

std::string tomlText(const toml::node &node)
{
    return formatToml(node);
}

std::string tomlKey(std::string_view key)
{
    const bool bare = !key.empty() && std::ranges::all_of(key, [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
    return bare ? std::string(key) : tomlText(toml::value<std::string>(std::string(key)));
}

// hotkeys.toml as a person reads it: each binding with its command's name.
std::string formatHotkeys(const toml::table &file)
{
    std::ostringstream out;
    out << "version = " << SettingsFiles::kVersion << "\n";
    toml::table rest = file;
    for (int window = 0; window < application::kHotkeyWindows; ++window) {
        const auto *table = file[kWindowTables[window]].as_table();
        rest.erase(kWindowTables[window]);
        if (!table)
            continue;
        out << "\n[" << kWindowTables[window] << "]\n";
        for (const auto &[key, node] : *table) {
            out << tomlKey(key.str()) << " = " << tomlText(node);
            if (const auto id = hotkeyIdOfLabel(std::string(key.str())))
                if (const std::string name = application::hotkeyName(*id); !name.empty())
                    out << "  # " << name;
            out << "\n";
        }
    }
    if (const auto *table = file[kAutomationTable].as_table()) {
        rest.erase(kAutomationTable);
        out << "\n[" << kAutomationTable << "]\n";
        for (const auto &[key, node] : *table) {
            out << tomlKey(key.str()) << " = ";
            if (const auto *entry = node.as_table()) {
                out << "{ ";
                bool first = true;
                for (const auto &[field, value] : *entry) {
                    out << (first ? "" : ", ") << tomlKey(field.str()) << " = " << tomlText(value);
                    first = false;
                }
                out << " }\n";
            } else
                out << tomlText(node) << "\n";
        }
    }
    if (!rest.empty())
        out << "\n" << formatToml(rest) << "\n";
    return out.str();
}

// ---- The INI file before CF1

// One-time steps the INI store took at each start (the registry's interim
// keys, the retired settings), on its values in memory: hikari.ini is never
// written again.
void migrateIni(QVariantMap &ini)
{
    auto targetKey = [](std::string_view id) {
        const auto *setting = application::findSetting(id);
        return qs(application::scopeName(setting->scope)) + QLatin1Char('/') + qs(setting->id);
    };
    for (const auto id : application::retiredSettings())
        for (const auto *scope : {"profile/", "workspace/"})
            ini.remove(QLatin1String(scope) + qs(id));
    if (ini.value(QStringLiteral("registry/schema"), 0).toInt() >= 1)
        return;
    // A value already under the registry key wins; the interim key goes either way.
    for (const auto &interim : application::interimKeys()) {
        const QString from = qs(interim.key);
        if (!ini.contains(from))
            continue;
        const QString to = targetKey(interim.setting);
        if (!ini.contains(to))
            ini.insert(to, ini.value(from));
        ini.remove(from);
    }
    std::int64_t options = 0;
    bool anyBit = false;
    for (const auto &bit : application::interimShiftOptionBits()) {
        const QString from = qs(bit.key);
        if (!ini.contains(from))
            continue;
        anyBit = true;
        if (ini.value(from).toBool())
            options |= bit.bit;
        ini.remove(from);
    }
    if (anyBit && !ini.contains(targetKey("shiftTimes.options")))
        ini.insert(targetKey("shiftTimes.options"), QVariant::fromValue<qlonglong>(options));
    const QString group = qs(application::kInterimAutomationHotkeysGroup) + QLatin1Char('/');
    QStringList rows;
    for (const QString &key : ini.keys()) {
        if (!key.startsWith(group) || key.indexOf(QLatin1Char('/'), group.size()) >= 0)
            continue;
        const QStringList v = ini.value(key).toStringList();
        rows << QStringList{key.mid(group.size()), v.value(0), v.value(1), v.value(2)}.join(QLatin1Char('\t'));
        ini.remove(key);
    }
    if (!rows.isEmpty() && !ini.contains(targetKey(application::kAutomationHotkeysSetting)))
        ini.insert(targetKey(application::kAutomationHotkeysSetting), rows);
}

// The INI store's reading of a value (the QVariant conversions the interim
// readers used).
SettingValue iniValue(const QVariant &v, SettingType type)
{
    switch (type) {
    case SettingType::Bool:
        return v.toBool();
    case SettingType::Int:
        return static_cast<std::int64_t>(v.toLongLong());
    case SettingType::String:
        return v.toString().toStdString();
    case SettingType::StringList: {
        StringList out;
        for (const QString &entry : v.toStringList())
            out.push_back(entry.toStdString());
        return out;
    }
    }
    return false;
}

std::string jsonDocumentText(const QJsonObject &state)
{
    // `version` first, then the rest as Qt writes it (keys in order).
    QByteArray text = QJsonDocument(state).toJson(QJsonDocument::Indented);
    const QByteArray version = "    \"version\": " + QByteArray::number(SettingsFiles::kVersion);
    if (state.isEmpty())
        return "{\n" + version.toStdString() + "\n}\n";
    text.insert(text.indexOf('\n') + 1, version + ",\n");
    return text.toStdString();
}

// Recursive: a SettingsFiles whose load throws goes inside open()'s lock.
std::recursive_mutex &registryMutex()
{
    static std::recursive_mutex m;
    return m;
}

// state.json's text into `into`, else where it fails.
std::optional<application::ParseFailure> parseStateText(std::string_view text, QJsonObject &into)
{
    QJsonParseError error;
    const QJsonDocument document =
        QJsonDocument::fromJson(QByteArray(text.data(), static_cast<qsizetype>(text.size())), &error);
    if (error.error != QJsonParseError::NoError) {
        const auto [line, column] = application::lineColumnAt(text, static_cast<std::size_t>(error.offset));
        return application::ParseFailure{line, column, error.errorString().toStdString()};
    }
    if (!document.isObject())
        return application::ParseFailure{1, 1, "It is not a JSON object."};
    QJsonObject object = document.object();
    std::optional<std::int64_t> version;
    if (const QJsonValue v = object.value(QLatin1String("version")); v.isDouble() && std::floor(v.toDouble()) == v.toDouble())
        version = static_cast<std::int64_t>(v.toDouble());
    else if (!v.isUndefined())
        return application::ParseFailure{0, 0, "Its version is not a whole number."};
    if (auto failure = application::checkFileVersion(version, SettingsFiles::kVersion))
        return failure;
    object.remove(QLatin1String("version"));
    into = object;
    return std::nullopt;
}

std::map<QString, std::weak_ptr<SettingsFiles>> &registry()
{
    static std::map<QString, std::weak_ptr<SettingsFiles>> r;
    return r;
}

} // namespace

// ---- SettingsFiles

SettingsFiles::File SettingsFiles::fileOf(const SettingDefinition &setting)
{
    const std::string_view id = setting.id;
    if (id == application::kHotkeysSetting || id == application::kAudioHotkeysSetting ||
        id == application::kAutomationHotkeysSetting)
        return Hotkeys;
    if (rememberedByTheApp(id) ||
        (setting.scope == application::SettingScope::Workspace && !chosenWorkspaceSetting(id)))
        return State;
    return Settings;
}

QString SettingsFiles::fileName(File file)
{
    switch (file) {
    case Settings:
        return QStringLiteral("settings.toml");
    case Hotkeys:
        return QStringLiteral("hotkeys.toml");
    case State:
        return QStringLiteral("state.json");
    }
    return {};
}

QString SettingsFiles::path(File file) const
{
    return m_folder + QLatin1Char('/') + fileName(file);
}

std::shared_ptr<SettingsFiles> SettingsFiles::open(const QString &settingsFile)
{
    const QString folder = QFileInfo(settingsFile).absolutePath();
    std::lock_guard lock(registryMutex());
    auto &slot = registry()[QDir::cleanPath(folder)];
    if (auto files = slot.lock())
        return files;
    auto files = std::make_shared<SettingsFiles>(folder);
    files->load();
    slot = files;
    return files;
}

bool SettingsFiles::exist(const QString &settingsFile)
{
    const QString folder = QFileInfo(settingsFile).absolutePath() + QLatin1Char('/');
    for (const File file : {Settings, Hotkeys, State})
        if (QFileInfo::exists(folder + fileName(file)))
            return true;
    return QFileInfo::exists(folder + QStringLiteral("hikari.ini"));
}

SettingsFiles::SettingsFiles(QString folder) : m_folder(std::move(folder)) {}

SettingsFiles::~SettingsFiles()
{
    flush();
    std::lock_guard lock(registryMutex());
    if (const auto it = registry().find(QDir::cleanPath(m_folder)); it != registry().end() && it->second.expired())
        registry().erase(it);
}

void SettingsFiles::load()
{
    const auto conversion = application::convertOnce(pathOf(path(Settings)), pathOf(legacyIni()),
                                                     [this] { return convertIni(); });
    m_converted = conversion == application::Conversion::Converted;
    m_conversionPending = conversion == application::Conversion::Failed && m_writable[Settings];
    if (conversion != application::Conversion::NotNeeded)
        return; // the documents are the converted ones (also when a write failed)
    const auto parseToml = [](toml::table &into) {
        return [&into](std::string_view text) -> std::optional<application::ParseFailure> {
            auto parsed = parseTomlFile(text, kVersion);
            if (auto *failure = std::get_if<application::ParseFailure>(&parsed))
                return *failure;
            into = std::move(std::get<toml::table>(parsed));
            return std::nullopt;
        };
    };
    const auto parseJson = [this](std::string_view text) { return parseStateText(text, m_state); };
    const std::pair<File, std::function<std::optional<application::ParseFailure>(std::string_view)>> files[] = {
        {Settings, parseToml(m_settings)}, {Hotkeys, parseToml(m_hotkeys)}, {State, parseJson}};
    for (const auto &[file, parse] : files) {
        auto loaded = application::loadOwnedFile(pathOf(path(file)), parse);
        if (loaded.problem) {
            m_writable[file] = !loaded.problem->keptAs.empty();
            m_problems.push_back(std::move(*loaded.problem));
        }
    }
    // A first start writes settings.toml even with every value at its
    // default: the next start is not a first start (O5).
    if (!QFileInfo::exists(path(Settings)) &&
        std::ranges::none_of(m_problems, [this](const auto &p) { return p.file == pathOf(path(Settings)); }))
        changed(Settings);
    // The model has one shortcut per command and window: an array's first.
    for (int window = 0; window < application::kHotkeyWindows; ++window)
        if (const auto *table = m_hotkeys[kWindowTables[window]].as_table())
            for (const auto &[key, node] : *table)
                if (const auto *array = node.as_array(); array && array->size() > 1)
                    m_ignored.push_back(std::string(kWindowTables[window]) + "." + std::string(key.str()) +
                                        ": only the first shortcut is used");
}

bool SettingsFiles::convertIni()
{
    // An INI that does not read is not converted to defaults: nothing is
    // written this session, and the next start tries again.
    if (QFile probe(legacyIni()); !probe.open(QIODevice::ReadOnly)) {
        m_problems.push_back({pathOf(legacyIni()), {}, {0, 0, "It could not be read."}});
        std::ranges::fill(m_writable, false);
        return false;
    }
    QVariantMap ini;
    {
        const QSettings file(legacyIni(), QSettings::IniFormat);
        for (const QString &key : file.allKeys())
            ini.insert(key, file.value(key));
    }
    migrateIni(ini);
    for (const auto &setting : application::settingDefinitions()) {
        if (setting.disposition == application::SettingDisposition::Excluded)
            continue;
        const QString key = qs(application::scopeName(setting.scope)) + QLatin1Char('/') + qs(setting.id);
        if (const auto it = ini.constFind(key); it != ini.cend())
            write(setting, application::storedValue(setting, iniValue(*it, setting.type)));
    }
    return writeConversion();
}

// settings.toml last, and only after the other two: it says the conversion
// is done.
bool SettingsFiles::writeConversion()
{
    for (const File file : {Hotkeys, State, Settings}) {
        m_dirty[file] = true;
        if (!writeFile(file))
            return false;
    }
    return true;
}

std::optional<SettingValue> SettingsFiles::read(const SettingDefinition &setting) const
{
    switch (fileOf(setting)) {
    case Hotkeys:
        if (setting.id == application::kAutomationHotkeysSetting)
            return readMacroHotkeys(m_hotkeys);
        return readHotkeys(m_hotkeys, setting.id == application::kAudioHotkeysSetting);
    case State: {
        const QJsonValue node = jsonNode(m_state, setting.id);
        if (node.isUndefined())
            return std::nullopt;
        if ((setting.id == "workspace.dialogGeometry" || setting.id == "workspace.pickerFolders") && node.isObject()) {
            StringList rows;
            const QJsonObject object = node.toObject();
            for (auto it = object.begin(); it != object.end(); ++it) {
                QString rest = it->toString();
                if (it->isArray()) {
                    QStringList parts;
                    for (const auto &v : it->toArray())
                        parts << QString::number(v.toInteger());
                    rest = parts.join(QLatin1Char(','));
                }
                rows.push_back(rowText(it.key(), rest).toStdString());
            }
            return rows;
        }
        if (setting.id == "automation.dialogAnswers" && node.isArray()) {
            StringList rows;
            for (const auto &entry : node.toArray()) {
                if (entry.isString()) {
                    rows.push_back(entry.toString().toStdString());
                    continue;
                }
                QJsonObject answer = entry.toObject();
                const QString key = answer.take(QLatin1String("key")).toString();
                rows.push_back(rowText(key, compactJson(answer)).toStdString());
            }
            return rows;
        }
        return fromJson(node);
    }
    case Settings: {
        if (const int slot = tagButtonSlot(setting); slot >= 0) {
            const auto *slotsNode = tomlNode(m_settings, kTagButtonSlots);
            const auto *array = slotsNode ? slotsNode->as_array() : nullptr;
            if (!array || static_cast<std::size_t>(slot) >= array->size() || !(*array)[slot].is_table())
                return std::nullopt;
            if (auto text = tagButtonOf(*(*array)[slot].as_table()))
                return *text;
            return std::nullopt;
        }
        const auto *node = tomlNode(m_settings, setting.id);
        if (!node)
            return std::nullopt;
        if (setting.id == "automation.dialogPresets" && node->is_table()) {
            StringList rows;
            for (const auto &[key, presets] : *node->as_table())
                rows.push_back(rowText(qs(key.str()), compactJson(jsonOf(presets))).toStdString());
            return rows;
        }
        return fromToml(*node);
    }
    }
    return std::nullopt;
}

void SettingsFiles::write(const SettingDefinition &setting, const SettingValue &value)
{
    const SettingValue v = application::convertSetting(value, setting.type);
    if (read(setting) == v) // Settings writes unchanged values too
        return;
    if (v == setting.defaultValue && fileOf(setting) != Hotkeys) {
        remove(setting);
        return;
    }
    put(setting, v);
    m_changed[fileOf(setting)].insert(std::string(setting.id));
    changed(fileOf(setting));
}

void SettingsFiles::put(const SettingDefinition &setting, const SettingValue &v)
{
    const auto *list = std::get_if<StringList>(&v);
    switch (fileOf(setting)) {
    case Hotkeys:
        if (setting.id == application::kAutomationHotkeysSetting)
            writeMacroHotkeys(m_hotkeys, list);
        else
            writeHotkeys(m_hotkeys, setting.id == application::kAudioHotkeysSetting, list);
        break;
    case State: {
        QJsonValue node = toJson(v);
        if (list && (setting.id == "workspace.dialogGeometry" || setting.id == "workspace.pickerFolders")) {
            if (const auto rows = keyedRows(*list)) {
                QJsonObject object;
                for (const auto &row : *rows) {
                    QJsonValue entry = row.rest;
                    if (setting.id == "workspace.dialogGeometry") {
                        QJsonArray numbers;
                        for (const QString &part : row.rest.split(QLatin1Char(','))) {
                            bool ok = false;
                            const int n = part.toInt(&ok);
                            if (ok && QString::number(n) == part)
                                numbers.append(n);
                        }
                        if (numbers.size() == 4 && row.rest.count(QLatin1Char(',')) == 3)
                            entry = numbers;
                    }
                    object.insert(row.key, entry);
                }
                node = object;
            }
        } else if (list && setting.id == "automation.dialogAnswers") {
            QJsonArray answers;
            for (const auto &row : *list) {
                const QString text = qs(row);
                const auto tab = text.indexOf(QLatin1Char('\t'));
                const QJsonDocument json = QJsonDocument::fromJson(text.mid(tab + 1).toUtf8());
                if (tab < 0 || !json.isObject() || json.object().contains(QLatin1String("key")) ||
                    rowText(text.left(tab), compactJson(json.object())) != text) {
                    answers.append(text);
                    continue;
                }
                QJsonObject answer = json.object();
                answer.insert(QLatin1String("key"), text.left(tab));
                answers.append(answer);
            }
            node = answers;
        }
        putJson(m_state, setting.id, node);
        break;
    }
    case Settings: {
        if (const int slot = tagButtonSlot(setting); slot >= 0) {
            auto *parent = tomlParent(m_settings, kTagButtonSlots, true);
            const auto leaf = segments(kTagButtonSlots).back();
            if (!parent->get(leaf) || !parent->get(leaf)->is_array())
                parent->insert_or_assign(leaf, toml::array());
            auto &array = *parent->get(leaf)->as_array();
            while (array.size() <= static_cast<std::size_t>(slot))
                array.push_back(toml::table());
            array.replace(array.begin() + slot, tagButtonTable(std::get<std::string>(v)));
            break;
        }
        auto *parent = tomlParent(m_settings, setting.id, true);
        const auto leaf = segments(setting.id).back();
        if (list && setting.id == "automation.dialogPresets")
            if (const auto rows = keyedRows(*list)) {
                toml::table presets;
                bool all = true;
                for (const auto &row : *rows) {
                    const QJsonDocument json = QJsonDocument::fromJson(row.rest.toUtf8());
                    auto array = json.isArray() ? tomlArrayOf(json.array()) : std::nullopt;
                    if (!array || rowText(row.key, compactJson(jsonOf(*array))) != rowText(row.key, row.rest)) {
                        all = false;
                        break;
                    }
                    presets.insert_or_assign(row.key.toStdString(), std::move(*array));
                }
                if (all) {
                    parent->insert_or_assign(leaf, std::move(presets));
                    break;
                }
            }
        putToml(*parent, leaf, v);
        break;
    }
    }
}

void SettingsFiles::remove(const SettingDefinition &setting)
{
    erase(setting);
    m_changed[fileOf(setting)].insert(std::string(setting.id));
    changed(fileOf(setting));
}

void SettingsFiles::erase(const SettingDefinition &setting)
{
    switch (fileOf(setting)) {
    case Hotkeys:
        if (setting.id == application::kAutomationHotkeysSetting)
            writeMacroHotkeys(m_hotkeys, nullptr);
        else
            writeHotkeys(m_hotkeys, setting.id == application::kAudioHotkeysSetting, nullptr);
        break;
    case State:
        putJson(m_state, setting.id, QJsonValue::Undefined);
        break;
    case Settings:
        if (const int slot = tagButtonSlot(setting); slot >= 0) {
            auto *parent = tomlParent(m_settings, kTagButtonSlots, false);
            const auto leaf = segments(kTagButtonSlots).back();
            auto *node = parent ? parent->get(leaf) : nullptr;
            if (!node || !node->is_array())
                break;
            auto &array = *node->as_array();
            if (static_cast<std::size_t>(slot) < array.size())
                array.replace(array.begin() + slot, toml::table());
            // Empty slots at the end are left out.
            while (!array.empty() && array.back().is_table() && array.back().as_table()->empty())
                array.pop_back();
            if (array.empty())
                eraseToml(m_settings, kTagButtonSlots);
            break;
        }
        eraseToml(m_settings, setting.id);
        break;
    }
}

void SettingsFiles::changed(File file)
{
    m_dirty[file] = true;
    if (m_flushQueued || !QCoreApplication::instance())
        return;
    m_flushQueued = true;
    std::weak_ptr<SettingsFiles> self = weak_from_this();
    QTimer::singleShot(0, QCoreApplication::instance(), [self] {
        if (auto files = self.lock())
            files->flush();
    });
}

void SettingsFiles::flush()
{
    m_flushQueued = false;
    // A conversion that could not be written is tried again as a whole.
    if (m_conversionPending) {
        m_conversionPending = application::convertOnce(pathOf(path(Settings)), pathOf(legacyIni()), [this] {
                                  return writeConversion();
                              }) == application::Conversion::Failed;
        if (m_conversionPending)
            return;
    }
    for (const File file : {Hotkeys, State, Settings})
        if (m_dirty[file])
            writeFile(file);
}

bool SettingsFiles::writeFile(File file)
{
    if (!m_writable[file])
        return false;
    // One writer at a time on the folder, as QSettings' lock file did; without
    // the lock (a stuck one, a folder that cannot hold it) it writes all the same.
    QDir().mkpath(m_folder);
    QLockFile lock(m_folder + QStringLiteral("/.settings.lock"));
    lock.tryLock(2000);
    mergeFromDisk(file);
    std::string text;
    switch (file) {
    case Settings:
        text = formatTomlFile(m_settings, kVersion);
        break;
    case Hotkeys:
        text = formatHotkeys(m_hotkeys);
        break;
    case State:
        text = jsonDocumentText(m_state);
        break;
    }
    // Kept dirty when the write fails, so a later flush tries again.
    if (!application::writeFileAtomically(pathOf(path(file)), text))
        return false;
    m_dirty[file] = false;
    m_changed[file].clear();
    return true;
}

// Another HikariSub on the folder may have written the file since this one
// read it: the file as it is now is kept, with only what changed here put
// over it.
void SettingsFiles::mergeFromDisk(File file)
{
    if (m_changed[file].empty())
        return;
    const auto bytes = application::readOwnedFile(pathOf(path(file)));
    if (!bytes)
        return;
    std::vector<std::pair<const SettingDefinition *, std::optional<SettingValue>>> ours;
    for (const auto &id : m_changed[file])
        if (const auto *setting = application::findSetting(id))
            ours.emplace_back(setting, read(*setting));
    if (file == State) {
        QJsonObject state;
        if (parseStateText(*bytes, state))
            return;
        m_state = std::move(state);
    } else {
        auto parsed = parseTomlFile(*bytes, kVersion);
        auto *table = std::get_if<toml::table>(&parsed);
        if (!table)
            return;
        (file == Settings ? m_settings : m_hotkeys) = std::move(*table);
    }
    for (const auto &[setting, value] : ours)
        if (value)
            put(*setting, *value);
        else
            erase(*setting);
}

} // namespace hikari::ui
