#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace hikari::application {

enum class SettingType { Bool, Int, String, StringList };

enum class SettingScope { Profile, Collection, Workspace };

// Unresolved keeps the value without a consumer yet; Excluded is never stored.
enum class SettingDisposition { Mapped, Unresolved, Excluded };

using SettingValue = std::variant<bool, std::int64_t, std::string, std::vector<std::string>>;

struct SettingDefinition {
    std::string_view id;        // stable persisted identity, e.g. "grid.hideColumns"
    std::string_view legacyKey; // the Config.txt / AudioConfig.txt name; empty for HikariSub's own settings
    SettingType type;
    SettingScope scope;
    SettingDisposition disposition;
    bool legacyAudioFile;       // kept in AudioConfig.txt
    SettingValue defaultValue;
};

// HikariSub legacy's settings first, then HikariSub's own.
std::span<const SettingDefinition> settingDefinitions();
const SettingDefinition *findSetting(std::string_view id);
const SettingDefinition *findLegacySetting(std::string_view legacyKey);
std::string_view scopeName(SettingScope scope);

// Where a value is kept. read() returns the value converted to the setting's
// type, or nothing when it was never stored.
class SettingsStorage {
public:
    virtual ~SettingsStorage() = default;
    virtual std::optional<SettingValue> read(const SettingDefinition &setting) const = 0;
    virtual void write(const SettingDefinition &setting, const SettingValue &value) = 0;
    virtual void remove(const SettingDefinition &setting) = 0;
};

class MemorySettingsStorage final : public SettingsStorage {
public:
    std::optional<SettingValue> read(const SettingDefinition &setting) const override;
    void write(const SettingDefinition &setting, const SettingValue &value) override;
    void remove(const SettingDefinition &setting) override;

private:
    std::map<std::string, SettingValue, std::less<>> m_values;
};

class Settings {
public:
    explicit Settings(SettingsStorage &storage) : m_storage(storage) {}

    // The stored value, else the default; an unknown id gives false.
    SettingValue value(std::string_view id) const;
    bool boolean(std::string_view id) const;
    std::int64_t integer(std::string_view id) const;
    std::string text(std::string_view id) const;
    std::vector<std::string> list(std::string_view id) const;
    bool isSet(std::string_view id) const;

    // Stores `value` (converted to the setting's type); false for an unknown
    // or excluded id. The observer hears of each value that changes.
    bool set(std::string_view id, const SettingValue &value);
    void reset(std::string_view id);
    void resetAll();

    using Observer = std::function<void(const SettingDefinition &)>;
    void setObserver(Observer observer) { m_observer = std::move(observer); }

private:
    SettingsStorage &m_storage;
    Observer m_observer;
};

// A value converted to `type` the way the INI adapter reads it: bool from
// "true"/"1"/non-zero, integers from their decimal text (0 otherwise), lists
// from one string as one entry.
SettingValue convertSetting(const SettingValue &value, SettingType type);

// A stored value equal to a default HikariSub replaced was never chosen and
// reads as the new default.
SettingValue storedValue(const SettingDefinition &setting, SettingValue value);

// The INI keys HikariSub used before the registry (hikari.ini), migrated once
// onto their settings.
struct InterimKey {
    std::string_view key;     // QSettings key, "Group/Name"
    std::string_view setting; // registry id
};
std::span<const InterimKey> interimKeys();
// The ShiftTimes panel's booleans, as bits of one setting.
struct InterimBit {
    std::string_view key;
    std::int64_t bit;
};
std::span<const InterimBit> interimShiftOptionBits();
// The group the automation hotkeys used ("AutomationHotkeys/<name>").
inline constexpr std::string_view kInterimAutomationHotkeysGroup = "AutomationHotkeys";
inline constexpr std::string_view kAutomationHotkeysSetting = "shortcuts.automationMacros";
// Values are hotkey lines; see hotkeys.h.
inline constexpr std::string_view kHotkeysSetting = "shortcuts.hotkeys";
inline constexpr std::string_view kAudioHotkeysSetting = "shortcuts.audioHotkeys";

// Withdrawn setting ids a profile may still hold (the INI store drops them).
std::span<const std::string_view> retiredSettings();
// "#RRGGBB" or "#RRGGBBAA" as 0xAARRGGBB; nullopt otherwise.
std::optional<std::uint32_t> parseSettingColour(std::string_view text);

}
