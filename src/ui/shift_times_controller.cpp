#include "shift_times_controller.h"

#include <QJsonDocument>

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

using application::Tier;

constexpr const char *kShiftPresets = "shiftTimes.presets";
constexpr const char *kPostprocessorPresets = "postprocessor.presets";

constexpr int kRuleBits = 15;
constexpr int kLegacyPanelBit = 16;

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
    {"ppWhichLines", "postprocessor.whichLines"},
    {"ppStyles", "postprocessor.styles"},
};
struct Bit {
    const char *name;
    int bit;
};
// MUST KILL: kBits packs the seven switches into the shiftTimes.options int
constexpr Bit kBits[] = {{"forward", 1},         {"fromStartTime", 2}, {"moveToVideoTime", 4},
                         {"moveToAudioTime", 8}, {"byFrames", 16},     {"tagTimes", 32},
                         {"centerTimes", 64}};
constexpr int kAllBits = 127;

const QStringList kShiftKeys{
    QStringLiteral("forward"),         QStringLiteral("byFrames"),        QStringLiteral("timeMs"),
    QStringLiteral("frames"),          QStringLiteral("fromStartTime"),   QStringLiteral("moveToVideoTime"),
    QStringLiteral("moveToAudioTime"), QStringLiteral("tagTimes"),        QStringLiteral("whichLines"),
    QStringLiteral("whichTimes"),      QStringLiteral("correctEndTimes"), QStringLiteral("styles"),
    QStringLiteral("centerTimes")};
const QStringList kPostprocessorKeys{
    QStringLiteral("postprocessor"),       QStringLiteral("leadIn"),
    QStringLiteral("leadOut"),             QStringLiteral("thresholdStart"),
    QStringLiteral("thresholdEnd"),        QStringLiteral("keyframeBeforeStart"),
    QStringLiteral("keyframeAfterStart"),  QStringLiteral("keyframeBeforeEnd"),
    QStringLiteral("keyframeAfterEnd"),    QStringLiteral("ppWhichLines"),
    QStringLiteral("ppStyles")};

const QStringList &keysOf(const QString &tool)
{
    return tool == QLatin1String("postprocessor") ? kPostprocessorKeys : kShiftKeys;
}

bool known(const QString &tool)
{
    return tool == QLatin1String("shift") || tool == QLatin1String("postprocessor");
}

QVariantMap mapOf(const application::ShiftTimesSettings &s, int ppWhichLines, const QString &ppStyles)
{
    return {{QStringLiteral("forward"), s.forward},
            {QStringLiteral("byFrames"), s.byFrames},
            {QStringLiteral("timeMs"), s.timeMs},
            {QStringLiteral("frames"), s.frames},
            {QStringLiteral("fromStartTime"), s.fromStartTime},
            {QStringLiteral("moveToVideoTime"), s.moveToVideoTime},
            {QStringLiteral("moveToAudioTime"), s.moveToAudioTime},
            {QStringLiteral("tagTimes"), s.tagTimes},
            {QStringLiteral("centerTimes"), s.centerTimes},
            {QStringLiteral("whichLines"), s.whichLines},
            {QStringLiteral("whichTimes"), s.whichTimes},
            {QStringLiteral("correctEndTimes"), s.correctEndTimes},
            {QStringLiteral("styles"), q(s.styles)},
            {QStringLiteral("postprocessor"), s.postprocessor & kRuleBits},
            {QStringLiteral("leadIn"), s.leadIn},
            {QStringLiteral("leadOut"), s.leadOut},
            {QStringLiteral("thresholdStart"), s.thresholdStart},
            {QStringLiteral("thresholdEnd"), s.thresholdEnd},
            {QStringLiteral("keyframeBeforeStart"), s.keyframeBeforeStart},
            {QStringLiteral("keyframeAfterStart"), s.keyframeAfterStart},
            {QStringLiteral("keyframeBeforeEnd"), s.keyframeBeforeEnd},
            {QStringLiteral("keyframeAfterEnd"), s.keyframeAfterEnd},
            {QStringLiteral("ppWhichLines"), ppWhichLines},
            {QStringLiteral("ppStyles"), ppStyles}};
}

QJsonObject pick(const QVariantMap &map, const QStringList &keys)
{
    QJsonObject out;
    for (const QString &k : keys)
        out.insert(k, QJsonValue::fromVariant(map.value(k)));
    return out;
}

}

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

void ShiftTimesController::load()
{
    QVariantMap map;
    for (const auto &f : kFields)
        map.insert(QLatin1String(f.name), m_store->value(QLatin1String(f.setting)));
    if (!m_store->contains("postprocessor.whichLines")) {
        map.insert(QStringLiteral("ppWhichLines"), map.value(QStringLiteral("whichLines")));
        map.insert(QStringLiteral("ppStyles"), map.value(QStringLiteral("styles")));
    }
    const int options = m_store->integer("shiftTimes.options");
    for (const auto &b : kBits)
        map.insert(QLatin1String(b.name), (options & b.bit) != 0);
    assign(map);
    m_settings.postprocessor &= kRuleBits;

    if (!m_store->contains("shiftTimes.presets") && !m_store->list("shiftTimes.profiles").isEmpty()) {
        for (const QString &profile : m_store->list("shiftTimes.profiles")) {
            const auto text = profile.toStdString();
            const auto values = application::applyShiftProfile(text, application::ShiftTimesSettings{});
            m_shiftPresets.of(Tier::Mine) << NamedPreset{QString::fromStdString(application::shiftProfileName(text)),
                                                         pick(mapOf(values, 0, {}), kShiftKeys)};
        }
        savePresets();
        m_store->reset(QStringLiteral("shiftTimes.profiles"));
    } else {
        m_shiftPresets.of(Tier::Mine) = readPresets(m_store->list(kShiftPresets));
    }
    m_postprocessorPresets.of(Tier::Mine) = readPresets(m_store->list(kPostprocessorPresets));
    m_shiftPreset = m_store->text("shiftTimes.preset");
    m_postprocessorPreset = m_store->text("postprocessor.preset");
    m_timingPreset = m_store->text("postprocessor.timingPreset");
    loadProjectPresets();
    emit changed();
    emit presetsChanged();
}

application::ShiftTimesSettings ShiftTimesController::runSettings(Tool tool) const
{
    auto s = m_settings;
    if (tool == Tool::Shift) {
        s.postprocessor = 0;
        return s;
    }
    s.postprocessor = (m_settings.postprocessor & kRuleBits) | kLegacyPanelBit;
    s.whichLines = m_ppWhichLines;
    s.styles = u8(m_ppStyles);
    s.byFrames = false;
    s.tagTimes = false;
    s.centerTimes = false;
    s.moveToVideoTime = false;
    s.moveToAudioTime = false;
    return s;
}

QVariantMap ShiftTimesController::settingsMap() const
{
    return mapOf(m_settings, m_ppWhichLines, m_ppStyles);
}

void ShiftTimesController::setSettingsMap(const QVariantMap &map)
{
    const auto before = settingsMap();
    assign(map);
    m_settings.postprocessor &= kRuleBits;
    const auto after = settingsMap();
    bool presets = false;
    for (const QString &tool : {QStringLiteral("shift"), QStringLiteral("postprocessor")}) {
        QString &current = *currentOf(tool);
        if (current.isEmpty())
            continue;
        for (const QString &k : keysOf(tool))
            if (before.value(k) != after.value(k)) {
                current.clear();
                presets = true;
                break;
            }
    }
    save();
    if (presets)
        savePresets();
    emit changed();
    if (presets)
        emit presetsChanged();
}

void ShiftTimesController::assign(const QVariantMap &map)
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
    b("centerTimes", s.centerTimes);
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
    i("ppWhichLines", m_ppWhichLines);
    if (map.contains(QStringLiteral("styles")))
        s.styles = u8(map.value(QStringLiteral("styles")).toString());
    if (map.contains(QStringLiteral("ppStyles")))
        m_ppStyles = map.value(QStringLiteral("ppStyles")).toString();
}

QStringList ShiftTimesController::names(const TieredPresets &presets)
{
    QStringList out;
    for (const auto &row : presets.rows())
        if (!row.hidden)
            out << q(row.name);
    return out;
}

QList<NamedPreset> ShiftTimesController::readPresets(const QStringList &stored)
{
    QJsonArray presets;
    for (const QString &s : stored)
        presets.append(QJsonDocument::fromJson(s.toUtf8()).object());
    return ui::presetsOf(presets);
}

QStringList ShiftTimesController::writePresets(const QList<NamedPreset> &presets)
{
    QStringList out;
    for (const auto &p : jsonOf(presets))
        out << QString::fromUtf8(QJsonDocument(p.toObject()).toJson(QJsonDocument::Compact));
    return out;
}

TieredPresets *ShiftTimesController::presetsOf(const QString &tool)
{
    return tool == QLatin1String("postprocessor") ? &m_postprocessorPresets : &m_shiftPresets;
}

QString *ShiftTimesController::currentOf(const QString &tool)
{
    return tool == QLatin1String("postprocessor") ? &m_postprocessorPreset : &m_shiftPreset;
}

Tier *ShiftTimesController::currentTierOf(const QString &tool)
{
    return tool == QLatin1String("postprocessor") ? &m_postprocessorPresetTier : &m_shiftPresetTier;
}

std::optional<Tier> ShiftTimesController::tierFor(const QString &tool, const QString &name, const QString &tier) const
{
    const auto &presets = tool == QLatin1String("postprocessor") ? m_postprocessorPresets : m_shiftPresets;
    return presets.find(name, tierFromText(tier));
}

void ShiftTimesController::setProjectRoot(const QString &root)
{
    if (root == m_projectFile.root())
        return;
    m_projectFile = PresetsFile(root);
    loadProjectPresets();
    emit presetsChanged();
}

void ShiftTimesController::loadProjectPresets()
{
    m_shiftPresets.of(Tier::Project) = readPresets(m_projectFile.list(kShiftPresets));
    m_postprocessorPresets.of(Tier::Project) = readPresets(m_projectFile.list(kPostprocessorPresets));
    m_shiftPresetTier = m_shiftPresets.find(m_shiftPreset).value_or(Tier::Mine);
    m_postprocessorPresetTier = m_postprocessorPresets.find(m_postprocessorPreset).value_or(Tier::Mine);
}

QJsonObject ShiftTimesController::valuesOf(const QString &tool) const
{
    return pick(settingsMap(), keysOf(tool));
}

bool ShiftTimesController::savePreset(const QString &tool, const QString &name, const QString &tier)
{
    const QString trimmed = name.trimmed();
    if (!known(tool) || trimmed.isEmpty())
        return false;
    const Tier to = hasProject() ? tierFromText(tier).value_or(Tier::Mine) : Tier::Mine;
    presetsOf(tool)->save(to, {trimmed, valuesOf(tool)});
    *currentOf(tool) = trimmed;
    *currentTierOf(tool) = to;
    savePresets();
    emit presetsChanged();
    return true;
}

bool ShiftTimesController::applyPreset(const QString &tool, const QString &name, const QString &tier)
{
    if (!known(tool))
        return false;
    const auto from = tierFor(tool, name, tier);
    const NamedPreset *preset = from ? presetsOf(tool)->preset(name, from) : nullptr;
    if (!preset)
        return false;
    QVariantMap values = preset->values.toVariantMap();
    for (auto v = values.begin(); v != values.end();)
        v = keysOf(tool).contains(v.key()) ? std::next(v) : values.erase(v);
    assign(values);
    m_settings.postprocessor &= kRuleBits;
    *currentOf(tool) = name;
    *currentTierOf(tool) = *from;
    save();
    savePresets();
    emit changed();
    emit presetsChanged();
    return true;
}

bool ShiftTimesController::renamePreset(const QString &tool, const QString &from, const QString &to, const QString &tier)
{
    const QString trimmed = to.trimmed();
    if (!known(tool) || trimmed.isEmpty())
        return false;
    auto &presets = *presetsOf(tool);
    const auto at = tierFor(tool, from, tier);
    if (!at || presets.hidden(from, *at))
        return false;
    const bool timing = tool == QLatin1String("postprocessor") && m_timingPreset == from && presets.find(from) == at;
    if (!presets.rename(*at, from, trimmed))
        return false;
    if (*currentOf(tool) == from && *currentTierOf(tool) == *at)
        *currentOf(tool) = trimmed;
    if (timing)
        m_timingPreset = trimmed;
    savePresets();
    emit presetsChanged();
    return true;
}

bool ShiftTimesController::deletePreset(const QString &tool, const QString &name, const QString &tier)
{
    if (!known(tool))
        return false;
    auto &presets = *presetsOf(tool);
    const auto at = tierFor(tool, name, tier);
    if (!at || presets.hidden(name, *at) || !presets.remove(*at, name))
        return false;
    if (*currentOf(tool) == name && *currentTierOf(tool) == *at)
        currentOf(tool)->clear();
    if (tool == QLatin1String("postprocessor") && m_timingPreset == name && !presets.find(name))
        m_timingPreset.clear();
    savePresets();
    emit presetsChanged();
    return true;
}

QVariantList ShiftTimesController::presetClashes(const QString &tool, const QString &name, const QString &tier) const
{
    const auto from = tierFromText(tier);
    const auto &presets = tool == QLatin1String("postprocessor") ? m_postprocessorPresets : m_shiftPresets;
    const auto suggestion = known(tool) && from ? presets.clash(name, *from) : std::nullopt;
    if (!suggestion)
        return {};
    return {QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("suggestion"), *suggestion}}};
}

QString ShiftTimesController::copyPreset(const QString &tool, const QString &name, const QString &tier,
                                         const QVariantList &choices)
{
    return sendPreset(tool, name, tier, choices, false);
}

QString ShiftTimesController::movePreset(const QString &tool, const QString &name, const QString &tier,
                                         const QVariantList &choices)
{
    return sendPreset(tool, name, tier, choices, true);
}

QString ShiftTimesController::sendPreset(const QString &tool, const QString &name, const QString &tier,
                                         const QVariantList &choices, bool move)
{
    const auto from = tierFromText(tier);
    if (!known(tool) || !from || !hasProject())
        return {};
    const auto landed = presetsOf(tool)->send(name, *from, clashChoiceFor(name, choices), move);
    if (!landed)
        return {};
    if (move && *currentOf(tool) == name && *currentTierOf(tool) == *from) {
        *currentOf(tool) = *landed;
        *currentTierOf(tool) = application::otherTier(*from);
    }
    savePresets();
    emit presetsChanged();
    return *landed;
}

void ShiftTimesController::setTimingPreset(const QString &name)
{
    if (name == m_timingPreset || (!name.isEmpty() && !m_postprocessorPresets.rows().empty() &&
                                   !m_postprocessorPresets.find(name)))
        return;
    m_timingPreset = name;
    savePresets();
    emit presetsChanged();
}

void ShiftTimesController::save() const
{
    const auto map = settingsMap();
    for (const auto &f : kFields)
        m_store->set(f.setting, map.value(QLatin1String(f.name)));
    int options = m_store->integer("shiftTimes.options") & ~kAllBits;
    for (const auto &b : kBits)
        if (map.value(QLatin1String(b.name)).toBool())
            options |= b.bit;
    m_store->set("shiftTimes.options", options);
}

void ShiftTimesController::savePresets()
{
    m_store->set(kShiftPresets, writePresets(m_shiftPresets.of(Tier::Mine)));
    m_store->set(kPostprocessorPresets, writePresets(m_postprocessorPresets.of(Tier::Mine)));
    m_store->set("shiftTimes.preset", m_shiftPreset);
    m_store->set("postprocessor.preset", m_postprocessorPreset);
    m_store->set("postprocessor.timingPreset", m_timingPreset);
    if (!hasProject())
        return;
    const auto project = [this](const char *id, const TieredPresets &presets) {
        if (const QStringList rows = writePresets(presets.of(Tier::Project)); rows != m_projectFile.list(id))
            m_projectFile.set(id, rows);
    };
    project(kShiftPresets, m_shiftPresets);
    project(kPostprocessorPresets, m_postprocessorPresets);
}

}
