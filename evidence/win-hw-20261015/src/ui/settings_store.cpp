#include "settings_store.h"

#include <QSettings>

namespace hikari::ui {

namespace {

using application::SettingDefinition;
using application::SettingType;
using application::SettingValue;

std::string str(const QString &s)
{
    return s.toStdString();
}

QString qs(std::string_view s)
{
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}

// One QSettings over the INI file for the store's life: reads come from its
// cache and writes are batched until Qt syncs it (at the next event loop pass
// and when the store goes), not one file rewrite per value. Other QSettings on
// the same file in this process share that data.
class IniSettingsStorage final : public application::SettingsStorage {
public:
    explicit IniSettingsStorage(const QString &file) : m_ini(file, QSettings::IniFormat) {}

    QSettings &ini() { return m_ini; }

    std::optional<SettingValue> read(const SettingDefinition &setting) const override
    {
        const QString key = SettingsStore::keyOf(setting);
        if (!m_ini.contains(key))
            return std::nullopt;
        // The QVariant conversions the interim readers used.
        const QVariant v = m_ini.value(key);
        switch (setting.type) {
        case SettingType::Bool:
            return v.toBool();
        case SettingType::Int:
            return static_cast<std::int64_t>(v.toLongLong());
        case SettingType::String:
            return str(v.toString());
        case SettingType::StringList: {
            std::vector<std::string> out;
            for (const QString &entry : v.toStringList())
                out.push_back(str(entry));
            return out;
        }
        }
        return std::nullopt;
    }

    void write(const SettingDefinition &setting, const SettingValue &value) override
    {
        m_ini.setValue(SettingsStore::keyOf(setting), SettingsStore::toVariant(value));
    }

    void remove(const SettingDefinition &setting) override
    {
        m_ini.remove(SettingsStore::keyOf(setting));
    }

private:
    mutable QSettings m_ini;
};

} // namespace

SettingsStore::SettingsStore(QString file, QObject *parent) : QObject(parent), m_file(std::move(file))
{
    if (m_file.isEmpty())
        m_storage = std::make_unique<application::MemorySettingsStorage>();
    else {
        auto ini = std::make_unique<IniSettingsStorage>(m_file);
        migrateInterimKeys(ini->ini());
        // K2: the withdrawn colour settings leave the profile (no saved
        // colour overrides the theme any more).
        for (const auto id : application::retiredSettings())
            if (ini->ini().contains(QStringLiteral("profile/") + qs(id)))
                ini->ini().remove(QStringLiteral("profile/") + qs(id));
        m_storage = std::move(ini);
    }
    m_settings = std::make_unique<application::Settings>(*m_storage);
    m_settings->setObserver([this](const SettingDefinition &setting) { emit changed(qs(setting.id)); });
}

SettingsStore::~SettingsStore() = default;

QString SettingsStore::keyOf(const SettingDefinition &setting)
{
    return qs(application::scopeName(setting.scope)) + QLatin1Char('/') + qs(setting.id);
}

QVariant SettingsStore::toVariant(const SettingValue &value)
{
    return std::visit(
        [](const auto &v) -> QVariant {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, bool>)
                return v;
            else if constexpr (std::is_same_v<T, std::int64_t>)
                return QVariant::fromValue<qlonglong>(v);
            else if constexpr (std::is_same_v<T, std::string>)
                return QString::fromStdString(v);
            else {
                QStringList out;
                for (const auto &entry : v)
                    out << QString::fromStdString(entry);
                return out;
            }
        },
        value);
}

SettingValue SettingsStore::fromVariant(const QVariant &value)
{
    switch (value.typeId()) {
    case QMetaType::Bool:
        return value.toBool();
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Long:
    case QMetaType::ULong:
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::Double:
    case QMetaType::Float:
        return static_cast<std::int64_t>(value.toLongLong());
    case QMetaType::QStringList:
    case QMetaType::QVariantList: {
        std::vector<std::string> out;
        for (const QString &entry : value.toStringList())
            out.push_back(str(entry));
        return out;
    }
    default:
        return str(value.toString());
    }
}

QVariant SettingsStore::value(const QString &id) const
{
    return toVariant(m_settings->value(str(id)));
}

bool SettingsStore::isSet(const QString &id) const
{
    return m_settings->isSet(str(id));
}

bool SettingsStore::setValue(const QString &id, const QVariant &value)
{
    return m_settings->set(str(id), fromVariant(value));
}

void SettingsStore::reset(const QString &id)
{
    m_settings->reset(str(id));
}

void SettingsStore::resetAll()
{
    m_settings->resetAll();
    sync();
}

void SettingsStore::sync()
{
    if (auto *ini = dynamic_cast<IniSettingsStorage *>(m_storage.get()))
        ini->ini().sync();
}

QString SettingsStore::text(const char *id) const
{
    return QString::fromStdString(m_settings->text(id));
}

QStringList SettingsStore::list(const char *id) const
{
    QStringList out;
    for (const auto &entry : m_settings->list(id))
        out << QString::fromStdString(entry);
    return out;
}

void SettingsStore::migrateInterimKeys(QSettings &ini)
{
    const QString schemaKey = QStringLiteral("registry/schema");
    if (ini.value(schemaKey, 0).toInt() >= kSchema)
        return;
    auto targetKey = [](std::string_view id) { return keyOf(*application::findSetting(id)); };
    // A value already under the registry key wins; the interim key goes either way.
    for (const auto &interim : application::interimKeys()) {
        const QString from = qs(interim.key);
        if (!ini.contains(from))
            continue;
        const QString to = targetKey(interim.setting);
        if (!ini.contains(to))
            ini.setValue(to, ini.value(from));
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
        ini.setValue(targetKey("shiftTimes.options"), QVariant::fromValue<qlonglong>(options));
    ini.beginGroup(qs(application::kInterimAutomationHotkeysGroup));
    QStringList rows;
    for (const QString &name : ini.childKeys()) {
        const QStringList v = ini.value(name).toStringList();
        rows << QStringList{name, v.value(0), v.value(1), v.value(2)}.join(QLatin1Char('\t'));
    }
    ini.endGroup();
    if (!rows.isEmpty() && !ini.contains(targetKey(application::kAutomationHotkeysSetting)))
        ini.setValue(targetKey(application::kAutomationHotkeysSetting), rows);
    ini.remove(qs(application::kInterimAutomationHotkeysGroup));
    ini.setValue(schemaKey, kSchema);
}

} // namespace hikari::ui
