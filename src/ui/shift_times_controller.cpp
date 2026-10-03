#include "shift_times_controller.h"

#include <QSettings>

namespace hikari::ui {

namespace {

QString q(const std::u8string &s)
{
    return QString::fromUtf8(reinterpret_cast<const char *>(s.data()), qsizetype(s.size()));
}

std::u8string u8(const QString &s)
{
    const QByteArray b = s.toUtf8();
    return std::u8string(reinterpret_cast<const char8_t *>(b.constData()), static_cast<std::size_t>(b.size()));
}

} // namespace

ShiftTimesController::ShiftTimesController(QString settingsFile, QObject *parent)
    : QObject(parent), m_settingsFile(std::move(settingsFile))
{
    if (m_settingsFile.isEmpty())
        return;
    const QSettings ini(m_settingsFile, QSettings::IniFormat);
    QVariantMap map;
    for (const QString &key : {QStringLiteral("forward"), QStringLiteral("byFrames"), QStringLiteral("timeMs"),
                               QStringLiteral("frames"), QStringLiteral("fromStartTime"), QStringLiteral("moveToVideoTime"),
                               QStringLiteral("moveToAudioTime"), QStringLiteral("tagTimes"), QStringLiteral("whichLines"),
                               QStringLiteral("whichTimes"), QStringLiteral("correctEndTimes"), QStringLiteral("styles")})
        if (ini.contains(QStringLiteral("ShiftTimes/") + key))
            map.insert(key, ini.value(QStringLiteral("ShiftTimes/") + key));
    setSettingsMap(map);
    m_profiles = ini.value(QStringLiteral("ShiftTimes/Profiles")).toStringList();
}

QVariantMap ShiftTimesController::settingsMap() const
{
    const auto &s = m_settings;
    return {{QStringLiteral("forward"), s.forward},
            {QStringLiteral("byFrames"), s.byFrames},
            {QStringLiteral("timeMs"), s.timeMs},
            {QStringLiteral("frames"), s.frames},
            {QStringLiteral("fromStartTime"), s.fromStartTime},
            {QStringLiteral("moveToVideoTime"), s.moveToVideoTime},
            {QStringLiteral("moveToAudioTime"), s.moveToAudioTime},
            {QStringLiteral("tagTimes"), s.tagTimes},
            {QStringLiteral("whichLines"), s.whichLines},
            {QStringLiteral("whichTimes"), s.whichTimes},
            {QStringLiteral("correctEndTimes"), s.correctEndTimes},
            {QStringLiteral("styles"), q(s.styles)}};
}

void ShiftTimesController::setSettingsMap(const QVariantMap &map)
{
    auto &s = m_settings;
    auto b = [&](const char *key, bool &field) {
        if (map.contains(QLatin1String(key)))
            field = map.value(QLatin1String(key)).toBool();
    };
    auto i = [&](const char *key, int &field) {
        if (map.contains(QLatin1String(key)))
            field = map.value(QLatin1String(key)).toInt();
    };
    b("forward", s.forward);
    b("byFrames", s.byFrames);
    i("timeMs", s.timeMs);
    i("frames", s.frames);
    b("fromStartTime", s.fromStartTime);
    b("moveToVideoTime", s.moveToVideoTime);
    b("moveToAudioTime", s.moveToAudioTime);
    b("tagTimes", s.tagTimes);
    i("whichLines", s.whichLines);
    i("whichTimes", s.whichTimes);
    i("correctEndTimes", s.correctEndTimes);
    if (map.contains(QStringLiteral("styles")))
        s.styles = u8(map.value(QStringLiteral("styles")).toString());
    save();
    emit changed();
}

QStringList ShiftTimesController::profileNames() const
{
    QStringList names;
    for (const QString &p : m_profiles)
        names << QString::fromStdString(application::shiftProfileName(p.toStdString()));
    return names;
}

void ShiftTimesController::saveProfile(const QString &name)
{
    if (name.isEmpty())
        return;
    QStringList next{QString::fromStdString(application::shiftProfileText(name.toStdString(), m_settings))};
    for (const QString &p : m_profiles)
        if (!p.startsWith(name + QLatin1Char(':')))
            next << p;
    m_profiles = next;
    save();
    emit changed();
}

void ShiftTimesController::loadProfile(const QString &name)
{
    for (const QString &p : m_profiles)
        if (QString::fromStdString(application::shiftProfileName(p.toStdString())) == name) {
            m_settings = application::applyShiftProfile(p.toStdString(), m_settings);
            save();
            emit changed();
            return;
        }
}

void ShiftTimesController::removeProfile(const QString &name)
{
    QStringList next;
    for (const QString &p : m_profiles)
        if (QString::fromStdString(application::shiftProfileName(p.toStdString())) != name)
            next << p;
    if (next.size() == m_profiles.size())
        return;
    m_profiles = next;
    save();
    emit changed();
}

void ShiftTimesController::save() const
{
    if (m_settingsFile.isEmpty())
        return;
    QSettings ini(m_settingsFile, QSettings::IniFormat);
    const auto map = settingsMap();
    for (auto it = map.begin(); it != map.end(); ++it)
        ini.setValue(QStringLiteral("ShiftTimes/") + it.key(), it.value());
    ini.setValue(QStringLiteral("ShiftTimes/Profiles"), m_profiles);
}

} // namespace hikari::ui
