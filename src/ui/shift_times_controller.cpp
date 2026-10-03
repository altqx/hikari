#include "shift_times_controller.h"

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

// The panel's values and their settings; the switches are the
// shiftTimes.options bits (legacy ShiftTimes::SaveOptions).
struct Field {
    const char *name;
    const char *setting;
};
constexpr Field kFields[] = {
    {"timeMs", "shiftTimes.time"},
    {"frames", "shiftTimes.displayFrames"},
    {"whichLines", "shiftTimes.whichLines"},
    {"whichTimes", "shiftTimes.whichTimes"},
    {"correctEndTimes", "shiftTimes.correctEndTimes"},
    {"styles", "shiftTimes.styles"},
    {"postprocessor", "postprocessor.on"},
    {"leadIn", "postprocessor.leadIn"},
    {"leadOut", "postprocessor.leadOut"},
    {"thresholdStart", "postprocessor.thresholdStart"},
    {"thresholdEnd", "postprocessor.thresholdEnd"},
    {"keyframeBeforeStart", "postprocessor.keyframeBeforeStart"},
    {"keyframeAfterStart", "postprocessor.keyframeAfterStart"},
    {"keyframeBeforeEnd", "postprocessor.keyframeBeforeEnd"},
    {"keyframeAfterEnd", "postprocessor.keyframeAfterEnd"},
};
struct Bit {
    const char *name;
    int bit;
};
constexpr Bit kBits[] = {{"forward", 1},         {"fromStartTime", 2}, {"moveToVideoTime", 4},
                         {"moveToAudioTime", 8}, {"byFrames", 16},     {"tagTimes", 32}};
constexpr int kAllBits = 63;

} // namespace

ShiftTimesController::ShiftTimesController(QString settingsFile, QObject *parent)
    : QObject(parent), m_ownedStore(std::make_unique<SettingsStore>(std::move(settingsFile))),
      m_store(m_ownedStore.get())
{
    load();
}

ShiftTimesController::ShiftTimesController(SettingsStore &settings, QObject *parent)
    : QObject(parent), m_store(&settings)
{
    load();
}

void ShiftTimesController::reload()
{
    m_settings = {};
    load();
}

// Only stored values replace the panel's own defaults.
void ShiftTimesController::load()
{
    QVariantMap map;
    for (const auto &f : kFields)
        if (m_store->contains(f.setting))
            map.insert(QLatin1String(f.name), m_store->value(QLatin1String(f.setting)));
    if (m_store->contains("shiftTimes.options")) {
        const int options = m_store->integer("shiftTimes.options");
        for (const auto &b : kBits)
            map.insert(QLatin1String(b.name), (options & b.bit) != 0);
    }
    // Profiles first: setSettingsMap saves them with the settings.
    m_profiles = m_store->list("shiftTimes.profiles");
    setSettingsMap(map);
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
            {QStringLiteral("styles"), q(s.styles)},
            {QStringLiteral("postprocessor"), s.postprocessor},
            {QStringLiteral("leadIn"), s.leadIn},
            {QStringLiteral("leadOut"), s.leadOut},
            {QStringLiteral("thresholdStart"), s.thresholdStart},
            {QStringLiteral("thresholdEnd"), s.thresholdEnd},
            {QStringLiteral("keyframeBeforeStart"), s.keyframeBeforeStart},
            {QStringLiteral("keyframeAfterStart"), s.keyframeAfterStart},
            {QStringLiteral("keyframeBeforeEnd"), s.keyframeBeforeEnd},
            {QStringLiteral("keyframeAfterEnd"), s.keyframeAfterEnd}};
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
    i("postprocessor", s.postprocessor);
    i("leadIn", s.leadIn);
    i("leadOut", s.leadOut);
    i("thresholdStart", s.thresholdStart);
    i("thresholdEnd", s.thresholdEnd);
    i("keyframeBeforeStart", s.keyframeBeforeStart);
    i("keyframeAfterStart", s.keyframeAfterStart);
    i("keyframeBeforeEnd", s.keyframeBeforeEnd);
    i("keyframeAfterEnd", s.keyframeAfterEnd);
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
    const auto map = settingsMap();
    for (const auto &f : kFields)
        m_store->set(f.setting, map.value(QLatin1String(f.name)));
    // Bits legacy does not know are kept.
    int options = m_store->integer("shiftTimes.options") & ~kAllBits;
    for (const auto &b : kBits)
        if (map.value(QLatin1String(b.name)).toBool())
            options |= b.bit;
    m_store->set("shiftTimes.options", options);
    m_store->set("shiftTimes.profiles", m_profiles);
}

} // namespace hikari::ui
