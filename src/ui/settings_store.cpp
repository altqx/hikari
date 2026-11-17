#include "settings_store.h"

#include "settings_files.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJSValue>

namespace hikari::ui {

namespace {

using application::SettingDefinition;
using application::SettingValue;

std::string str(const QString &s)
{
    return s.toStdString();
}

QString qs(std::string_view s)
{
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}

}

SettingsStore::SettingsStore(QString file, QObject *parent) : QObject(parent), m_file(std::move(file))
{
    if (m_file.isEmpty())
        m_storage = std::make_shared<application::MemorySettingsStorage>();
    else
        m_storage = m_files = SettingsFiles::open(m_file);
    m_settings = std::make_unique<application::Settings>(*m_storage);
    m_settings->setObserver([this](const SettingDefinition &setting) { emit changed(qs(setting.id)); });
    if (!m_files)
        return;
    m_reload.setSingleShot(true);
    m_reload.setInterval(150);
    connect(&m_reload, &QTimer::timeout, this, &SettingsStore::reloadFile);
    // An atomic save replaces the file, which drops it from the watch; the folder's change brings it back.
    const QString path = m_files->path(SettingsFiles::Settings);
    m_watcher.addPath(QFileInfo(path).absolutePath());
    if (QFileInfo::exists(path))
        m_watcher.addPath(path);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_reload, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this, path] {
        if (!m_watcher.files().contains(path) && QFileInfo::exists(path)) {
            m_watcher.addPath(path);
            m_reload.start();
        }
    });
}

void SettingsStore::reloadFile()
{
    const QString path = m_files->path(SettingsFiles::Settings);
    if (!m_watcher.files().contains(path) && QFileInfo::exists(path))
        m_watcher.addPath(path);
    const SettingsFiles::Reloaded reloaded = m_files->reloadSettings();
    const QString problem = reloaded.failure
                                ? QCoreApplication::translate("HikariSub.Options", "settings.toml has an error on line %1.")
                                      .arg(reloaded.failure->line)
                                : QString();
    if (problem != m_fileProblem) {
        m_fileProblem = problem;
        emit fileProblemChanged();
    }
    for (const std::string &id : reloaded.changed)
        emit changed(qs(id));
}

bool SettingsStore::differs(const QString &id) const
{
    const auto *setting = application::findSetting(str(id));
    return setting && m_settings->isSet(setting->id) && m_settings->value(setting->id) != setting->defaultValue;
}

QStringList SettingsStore::changedSettings() const
{
    QStringList out;
    for (const auto &setting : application::settingDefinitions())
        if (setting.disposition == application::SettingDisposition::Mapped && m_settings->isSet(setting.id) &&
            m_settings->value(setting.id) != setting.defaultValue)
            out << qs(setting.id);
    return out;
}

QString SettingsStore::filePath(const QString &file) const
{
    if (!m_files)
        return {};
    return m_files->path(file == QLatin1String("hotkeys") ? SettingsFiles::Hotkeys : SettingsFiles::Settings);
}

SettingsStore::~SettingsStore() = default;

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
    // A JavaScript array from QML arrives as a QJSValue.
    if (value.metaType() == QMetaType::fromType<QJSValue>())
        return fromVariant(value.value<QJSValue>().toVariant());
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
    if (m_files)
        m_files->flush();
}

std::vector<application::FileProblem> SettingsStore::problems() const
{
    return m_files ? m_files->problems() : std::vector<application::FileProblem>();
}

bool SettingsStore::converted() const
{
    return m_files && m_files->converted();
}

bool SettingsStore::filesExist(const QString &file)
{
    return SettingsFiles::exist(file);
}

std::vector<std::string> SettingsStore::ignored() const
{
    return m_files ? m_files->ignored() : std::vector<std::string>();
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

}
