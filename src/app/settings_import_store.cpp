#include "hikari/app/settings_import_store.h"

#include "settings_store.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>

namespace hikari::app {

namespace si = application::settings_import;
using application::SettingValue;

namespace {

QString qs(std::string_view s)
{
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}

std::string ss(const QString &s)
{
    return s.toStdString();
}

QByteArray bytesOf(std::string_view s)
{
    return QByteArray(s.data(), static_cast<qsizetype>(s.size()));
}

std::string stringOf(const QByteArray &b)
{
    return std::string(b.constData(), static_cast<std::size_t>(b.size()));
}

QString sha256(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

std::optional<QByteArray> readAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    return f.readAll();
}

bool writeAll(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(bytes);
    return f.commit();
}

QJsonValue toJson(const SettingValue &v)
{
    QJsonObject o;
    std::visit(
        [&](const auto &x) {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, bool>) {
                o[QStringLiteral("bool")] = x;
            } else if constexpr (std::is_same_v<T, std::int64_t>) {
                o[QStringLiteral("int")] = QString::number(x); // 64 bits, exactly
            } else if constexpr (std::is_same_v<T, std::string>) {
                o[QStringLiteral("string")] = qs(x);
            } else {
                QJsonArray a;
                for (const auto &e : x)
                    a.append(qs(e));
                o[QStringLiteral("list")] = a;
            }
        },
        v);
    return o;
}

SettingValue fromJson(const QJsonValue &json)
{
    const QJsonObject o = json.toObject();
    if (o.contains(QStringLiteral("bool")))
        return o.value(QStringLiteral("bool")).toBool();
    if (o.contains(QStringLiteral("int")))
        return std::int64_t(o.value(QStringLiteral("int")).toString().toLongLong());
    if (o.contains(QStringLiteral("string")))
        return ss(o.value(QStringLiteral("string")).toString());
    std::vector<std::string> list;
    for (const auto &e : o.value(QStringLiteral("list")).toArray())
        list.push_back(ss(e.toString()));
    return list;
}

QJsonObject receiptJson(const si::Receipt &r)
{
    QJsonObject o;
    QJsonArray sources;
    for (const auto &[path, sha] : r.sources)
        sources.append(QJsonArray{qs(path), qs(sha)});
    o[QStringLiteral("sources")] = sources;
    o[QStringLiteral("mappingVersion")] = r.mappingVersion;
    o[QStringLiteral("destinationProfile")] = qs(r.destinationProfile);
    QJsonObject imported;
    for (const auto &[id, v] : r.imported)
        imported[qs(id)] = toJson(v);
    o[QStringLiteral("imported")] = imported;
    QJsonArray kept;
    for (const auto &id : r.kept)
        kept.append(qs(id));
    o[QStringLiteral("kept")] = kept;
    return o;
}

si::Receipt receiptFrom(const QJsonObject &o)
{
    si::Receipt r;
    for (const auto &s : o.value(QStringLiteral("sources")).toArray()) {
        const QJsonArray pair = s.toArray();
        r.sources.emplace_back(ss(pair.at(0).toString()), ss(pair.at(1).toString()));
    }
    r.mappingVersion = o.value(QStringLiteral("mappingVersion")).toInt();
    r.destinationProfile = ss(o.value(QStringLiteral("destinationProfile")).toString());
    const QJsonObject imported = o.value(QStringLiteral("imported")).toObject();
    for (auto it = imported.begin(); it != imported.end(); ++it)
        r.imported[ss(it.key())] = fromJson(it.value());
    for (const auto &k : o.value(QStringLiteral("kept")).toArray())
        r.kept.insert(ss(k.toString()));
    return r;
}

// A profile's values in JSON; its files are written beside it.
QJsonObject valuesJson(const si::Profile &p)
{
    QJsonObject values;
    for (const auto &[id, v] : p.values)
        values[qs(id)] = toJson(v);
    return values;
}

bool writeProfile(const QString &dir, const si::Profile &p, QJsonObject &into, const QString &name)
{
    into[name + QStringLiteral("Values")] = valuesJson(p);
    QJsonArray files;
    for (const auto &[path, bytes] : p.files) {
        files.append(qs(path));
        if (!writeAll(dir + QLatin1Char('/') + name + QLatin1Char('/') + qs(path), bytesOf(bytes)))
            return false;
    }
    into[name + QStringLiteral("Files")] = files;
    return true;
}

std::optional<si::Profile> readProfile(const QString &dir, const QJsonObject &from, const QString &name)
{
    si::Profile p;
    const QJsonObject values = from.value(name + QStringLiteral("Values")).toObject();
    for (auto it = values.begin(); it != values.end(); ++it)
        p.values[ss(it.key())] = fromJson(it.value());
    for (const auto &f : from.value(name + QStringLiteral("Files")).toArray()) {
        const auto bytes = readAll(dir + QLatin1Char('/') + name + QLatin1Char('/') + f.toString());
        if (!bytes)
            return std::nullopt;
        p.files[ss(f.toString())] = stringOf(*bytes);
    }
    return p;
}

// The import's changes over what the live profile holds now: where the
// generation differs from its base it wins, elsewhere the live value stays
// (an edit made after staging is kept).
si::Profile merged(const si::Profile &base, const si::Profile &target, const si::Profile &live)
{
    si::Profile out = live;
    std::set<std::string, std::less<>> keys;
    for (const auto *p : {&base, &target})
        for (const auto &[k, v] : p->values)
            keys.insert(k);
    for (const auto &k : keys) {
        const auto b = base.values.find(k), t = target.values.find(k);
        const bool inBase = b != base.values.end(), inTarget = t != target.values.end();
        if (inBase == inTarget && (!inBase || b->second == t->second))
            continue;
        if (inTarget)
            out.values[k] = t->second;
        else
            out.values.erase(k);
    }
    std::set<std::string, std::less<>> files;
    for (const auto *p : {&base, &target})
        for (const auto &[k, v] : p->files)
            files.insert(k);
    for (const auto &k : files) {
        const auto b = base.files.find(k), t = target.files.find(k);
        const bool inBase = b != base.files.end(), inTarget = t != target.files.end();
        if (inBase == inTarget && (!inBase || b->second == t->second))
            continue;
        if (inTarget)
            out.files[k] = t->second;
        else
            out.files.erase(k);
    }
    return out;
}

// The authored collections the profile holds, by path in the settings
// folder: Rules.txt, Dictionary/UserDic.udic and Dictionary/*.dic / *.aff.
QStringList collectionPaths(const QString &folder)
{
    QStringList out;
    if (QFileInfo::exists(folder + QStringLiteral("/Rules.txt")))
        out << QStringLiteral("Rules.txt");
    const QDir dictionary(folder + QStringLiteral("/Dictionary"));
    for (const QString &name : dictionary.entryList(
             {QStringLiteral("UserDic.udic"), QStringLiteral("*.dic"), QStringLiteral("*.aff")}, QDir::Files, QDir::Name))
        out << QStringLiteral("Dictionary/") + name;
    return out;
}

bool trackedCollection(const QString &path)
{
    return path == QLatin1String("Rules.txt") ||
           (path.startsWith(QLatin1String("Dictionary/")) && !path.mid(11).contains(QLatin1Char('/')) &&
            (path.endsWith(QLatin1String(".dic")) || path.endsWith(QLatin1String(".aff")) ||
             path == QLatin1String("Dictionary/UserDic.udic")));
}

QString profileHash(const si::Profile &p)
{
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(QJsonDocument(valuesJson(p)).toJson(QJsonDocument::Compact));
    for (const auto &[path, bytes] : p.files) {
        h.addData(bytesOf(path));
        h.addData(QByteArrayLiteral("\0"));
        h.addData(QCryptographicHash::hash(bytesOf(bytes), QCryptographicHash::Sha256));
    }
    return QString::fromLatin1(h.result().toHex());
}

} // namespace

SettingsImportStore::SettingsImportStore(ui::SettingsStore &settings, QString folder, QString profileName)
    : m_settings(settings), m_folder(std::move(folder)), m_profileName(std::move(profileName))
{
}

// ---- Discovery

std::optional<SettingsImportStore::Root> SettingsImportStore::inspectRoot(const QString &path)
{
    if (path.isEmpty() || !QFileInfo(path).isDir())
        return std::nullopt;
    Root root;
    root.path = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    for (const auto &known : si::knownSources())
        if (QFileInfo(root.path + QLatin1Char('/') + qs(known.path)).isFile())
            root.files << qs(known.path);
    const QDir dictionary(root.path + QStringLiteral("/Dictionary"));
    for (const QString &name : dictionary.entryList({QStringLiteral("*.dic"), QStringLiteral("*.aff")}, QDir::Files,
                                                    QDir::Name))
        root.files << QStringLiteral("Dictionary/") + name;
    // Themes/<name>.txt (config.cpp:612, 924).
    const QDir themes(root.path + QStringLiteral("/Themes"));
    for (const QString &name : themes.entryList({QStringLiteral("*.txt")}, QDir::Files, QDir::Name))
        root.themes << QStringLiteral("Themes/") + name;
    if (root.files.isEmpty())
        return std::nullopt;
    return root;
}

QList<SettingsImportStore::Root> SettingsImportStore::discoverRoots(const QStringList &candidates)
{
    QList<Root> out;
    QStringList seen;
    for (const QString &c : candidates) {
        auto root = inspectRoot(c);
        if (!root || seen.contains(root->path))
            continue;
        seen << root->path;
        out << *root;
    }
    return out;
}

QStringList SettingsImportStore::defaultCandidates()
{
    QStringList out{QCoreApplication::applicationDirPath()};
#ifdef _WIN32
    for (const char *env : {"ProgramFiles", "ProgramFiles(x86)", "LOCALAPPDATA"}) {
        const QString base = qEnvironmentVariable(env);
        if (base.isEmpty())
            continue;
        out << base + QStringLiteral("/HikariSub") << base + QStringLiteral("/Programs/HikariSub")
            << base + QStringLiteral("/Kainote");
    }
#endif
    return out;
}

// ---- Snapshot

std::optional<SettingsImportStore::Snapshot> SettingsImportStore::snapshot(const Root &root, QString *error)
{
    Snapshot snap;
    snap.root = root.path;
    snap.themeFiles = root.themes;
    QStringList ids;
    for (const QString &path : root.files) {
        const auto bytes = readAll(root.path + QLatin1Char('/') + path);
        if (!bytes) {
            if (error)
                *error = QCoreApplication::translate("SettingsImport", "Cannot read %1").arg(path);
            return std::nullopt;
        }
        si::SourceFile s;
        s.kind = si::SourceKind::Dictionary;
        for (const auto &known : si::knownSources())
            if (qs(known.path) == path)
                s.kind = known.kind;
        s.path = ss(path);
        s.bytes = stringOf(*bytes);
        s.sha256 = ss(sha256(*bytes));
        ids << path + QLatin1Char('=') + qs(s.sha256);
        snap.sources.push_back(std::move(s));
    }
    snap.id = sha256(ids.join(QLatin1Char('\n')).toUtf8()).left(16);
    snap.dir = importDir() + QStringLiteral("/snapshots/") + snap.id;
    if (QFileInfo::exists(snap.dir + QStringLiteral("/snapshot.json")))
        return snap; // the same bytes again
    QJsonArray files;
    for (const auto &s : snap.sources) {
        if (!writeAll(snap.dir + QLatin1Char('/') + qs(s.path), bytesOf(s.bytes))) {
            if (error)
                *error = QCoreApplication::translate("SettingsImport", "Cannot write the snapshot");
            return std::nullopt;
        }
        const auto decoded = si::decodeSettingsText(s.bytes, si::kReadBy);
        QJsonObject f;
        f[QStringLiteral("path")] = qs(s.path);
        f[QStringLiteral("sha256")] = qs(s.sha256);
        f[QStringLiteral("size")] = qint64(s.bytes.size());
        f[QStringLiteral("encoding")] = decoded.evidence.decoded ? qs(decoded.evidence.interpretation)
                                                                 : QStringLiteral("undecided");
        f[QStringLiteral("bom")] = decoded.evidence.utf8Bom || decoded.evidence.utf16Bom;
        if (decoded.text)
            f[QStringLiteral("header")] = qs(decoded.text->substr(0, decoded.text->find('\n')));
        files.append(f);
    }
    QJsonObject o;
    o[QStringLiteral("root")] = snap.root;
    o[QStringLiteral("files")] = files;
    o[QStringLiteral("themes")] = QJsonArray::fromStringList(snap.themeFiles);
    if (!writeAll(snap.dir + QStringLiteral("/snapshot.json"), QJsonDocument(o).toJson())) {
        if (error)
            *error = QCoreApplication::translate("SettingsImport", "Cannot write the snapshot");
        return std::nullopt;
    }
    return snap;
}

bool SettingsImportStore::sourcesUnchanged(const Snapshot &snapshot)
{
    for (const auto &s : snapshot.sources) {
        const auto bytes = readAll(snapshot.root + QLatin1Char('/') + qs(s.path));
        if (!bytes || ss(sha256(*bytes)) != s.sha256)
            return false;
    }
    // A known file added since is a change too.
    const auto now = inspectRoot(snapshot.root);
    return now && now->files.size() == qsizetype(snapshot.sources.size());
}

// ---- The destination

si::Profile SettingsImportStore::liveProfile() const
{
    si::Profile p;
    const auto &settings = m_settings.settings();
    for (const auto &def : application::settingDefinitions())
        if (settings.isSet(def.id))
            p.values[std::string(def.id)] = settings.value(def.id);
    for (const QString &path : collectionPaths(m_folder))
        if (const auto bytes = readAll(m_folder + QLatin1Char('/') + path))
            p.files[ss(path)] = stringOf(*bytes);
    return p;
}

si::Destination SettingsImportStore::destinationOf(const si::Profile &profile) const
{
    si::Destination d;
    d.values = profile.values;
    for (const auto &[path, bytes] : profile.files)
        d.files[path] = si::DestinationFile{bytes, ss(sha256(bytesOf(bytes)))};
    const QDir bundled(QCoreApplication::applicationDirPath() + QStringLiteral("/Dictionary"));
    for (const QString &name : bundled.entryList({QStringLiteral("*.dic")}, QDir::Files))
        d.bundledDictionaries.insert(ss(QFileInfo(name).completeBaseName()));
#ifdef _WIN32
    d.windowsHost = true;
#endif
    d.pathExists = [](std::string_view path) { return QFileInfo::exists(qs(path)); };
    d.macroProblem = macroProblem;
    return d;
}

si::Destination SettingsImportStore::destination() const
{
    const Manifest m = readManifest();
    const si::Profile live = liveProfile();
    if (m.active > 0 && liveGeneration() != m.active)
        if (const auto g = readGeneration(m.active))
            return destinationOf(m.rollback ? g->profile : merged(g->base, g->profile, live));
    return destinationOf(live);
}

QString SettingsImportStore::revision() const
{
    return profileHash(si::profileOf(destination()));
}

std::optional<si::Receipt> SettingsImportStore::activeReceipt() const
{
    const Manifest m = readManifest();
    if (m.active == 0)
        return std::nullopt;
    const auto g = readGeneration(m.active);
    return g ? g->receipt : std::nullopt;
}

// ---- Generations

SettingsImportStore::Manifest SettingsImportStore::readManifest() const
{
    Manifest m;
    const auto bytes = readAll(importDir() + QStringLiteral("/manifest.json"));
    if (!bytes)
        return m;
    const QJsonObject o = QJsonDocument::fromJson(*bytes).object();
    m.active = o.value(QStringLiteral("active")).toInt();
    m.previous = o.value(QStringLiteral("previous")).toInt();
    m.rollback = o.value(QStringLiteral("rollback")).toBool();
    return m;
}

bool SettingsImportStore::writeManifest(const Manifest &m)
{
    if (failAt && failAt(Step::Manifest))
        return false;
    QJsonObject o;
    o[QStringLiteral("active")] = m.active;
    o[QStringLiteral("previous")] = m.previous;
    o[QStringLiteral("rollback")] = m.rollback;
    o[QStringLiteral("profile")] = m_profileName;
    // QSaveFile: written aside, then renamed over the old one at once.
    return writeAll(importDir() + QStringLiteral("/manifest.json"), QJsonDocument(o).toJson());
}

int SettingsImportStore::activeGeneration() const
{
    return readManifest().active;
}

int SettingsImportStore::previousGeneration() const
{
    return readManifest().previous;
}

bool SettingsImportStore::pending() const
{
    const Manifest m = readManifest();
    return m.active > 0 && liveGeneration() != m.active;
}

int SettingsImportStore::liveGeneration() const
{
    const auto bytes = readAll(importDir() + QStringLiteral("/live"));
    return bytes ? bytes->trimmed().toInt() : 0;
}

void SettingsImportStore::setLiveGeneration(int number)
{
    writeAll(importDir() + QStringLiteral("/live"), QByteArray::number(number));
}

std::optional<SettingsImportStore::Generation> SettingsImportStore::readGeneration(int number) const
{
    const QString dir = importDir() + QStringLiteral("/generations/") + QString::number(number);
    if (!QFileInfo::exists(dir + QStringLiteral("/complete")))
        return std::nullopt;
    const auto bytes = readAll(dir + QStringLiteral("/profile.json"));
    if (!bytes)
        return std::nullopt;
    const QJsonObject o = QJsonDocument::fromJson(*bytes).object();
    Generation g;
    auto profile = readProfile(dir, o, QStringLiteral("profile"));
    auto base = readProfile(dir, o, QStringLiteral("base"));
    if (!profile || !base)
        return std::nullopt;
    g.profile = std::move(*profile);
    g.base = std::move(*base);
    if (o.value(QStringLiteral("receipt")).isObject())
        g.receipt = receiptFrom(o.value(QStringLiteral("receipt")).toObject());
    return g;
}

int SettingsImportStore::nextGenerationNumber() const
{
    int next = 1;
    const QDir dir(importDir() + QStringLiteral("/generations"));
    for (const QString &name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        next = std::max(next, name.toInt() + 1);
    return next;
}

QStringList SettingsImportStore::incompleteStaging() const
{
    QStringList out;
    const QDir dir(importDir() + QStringLiteral("/generations"));
    for (const QString &name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
        if (!QFileInfo::exists(dir.filePath(name) + QStringLiteral("/complete")))
            out << dir.filePath(name);
    return out;
}

bool SettingsImportStore::stage(int number, const Generation &g)
{
    const QString dir = importDir() + QStringLiteral("/generations/") + QString::number(number);
    QJsonObject o;
    if (!QDir().mkpath(dir) || !writeProfile(dir, g.profile, o, QStringLiteral("profile")))
        return false;
    if (failAt && failAt(Step::Files))
        return false;
    if (!writeProfile(dir, g.base, o, QStringLiteral("base")))
        return false;
    if (g.receipt)
        o[QStringLiteral("receipt")] = receiptJson(*g.receipt);
    if (failAt && failAt(Step::Profile))
        return false;
    if (!writeAll(dir + QStringLiteral("/profile.json"), QJsonDocument(o).toJson()))
        return false;
    if (failAt && failAt(Step::Complete))
        return false;
    return writeAll(dir + QStringLiteral("/complete"), QByteArray());
}

SettingsImportStore::Result SettingsImportStore::activate(const si::Plan &plan,
                                                          const std::set<std::string, std::less<>> &chosen,
                                                          const Snapshot &snapshot, const QString &planRevision)
{
    // Recheck before committing: the legacy files and the destination are
    // still what the plan was made from.
    if (!sourcesUnchanged(snapshot))
        return Result::StaleSources;
    if (revision() != planRevision)
        return Result::StaleDestination;
    const si::Destination dest = destination();
    const si::Profile current = si::profileOf(dest);
    const si::Profile imported = si::applyPlan(plan, chosen, dest);
    if (imported == current)
        return Result::NoChange;

    const Manifest before = readManifest();
    int number = nextGenerationNumber();
    // The current destination is the previous generation: the active one
    // when nothing changed since, else a backup of it.
    int previous = before.active;
    std::optional<Generation> active = before.active ? readGeneration(before.active) : std::nullopt;
    const bool activeIsCurrent = active && active->profile == current;
    if (!activeIsCurrent) {
        if (failAt && failAt(Step::Backup))
            return Result::StagingFailed;
        Generation backup;
        backup.profile = current;
        backup.base = current;
        backup.receipt = active ? active->receipt : std::nullopt;
        if (!stage(number, backup))
            return Result::StagingFailed;
        previous = number++;
    }
    Generation g;
    g.profile = imported;
    g.base = current;
    g.receipt = si::receiptOf(plan, chosen, ss(m_profileName));
    if (!stage(number, g))
        return Result::StagingFailed;
    Manifest m;
    m.active = number;
    m.previous = previous;
    if (!writeManifest(m))
        return Result::StagingFailed;
    return Result::Activated;
}

QStringList SettingsImportStore::editsSinceActivation() const
{
    const Manifest m = readManifest();
    QStringList out;
    if (m.active == 0)
        return out;
    const auto g = readGeneration(m.active);
    if (!g)
        return out;
    const si::Profile now = si::profileOf(destination());
    std::set<std::string, std::less<>> keys;
    for (const auto *p : {&g->profile, &now})
        for (const auto &[k, v] : p->values)
            keys.insert(k);
    for (const auto &k : keys) {
        const auto a = g->profile.values.find(k), b = now.values.find(k);
        if ((a == g->profile.values.end()) != (b == now.values.end()) ||
            (a != g->profile.values.end() && a->second != b->second))
            out << qs(k);
    }
    std::set<std::string, std::less<>> files;
    for (const auto *p : {&g->profile, &now})
        for (const auto &[k, v] : p->files)
            files.insert(k);
    for (const auto &k : files) {
        const auto a = g->profile.files.find(k), b = now.files.find(k);
        if ((a == g->profile.files.end()) != (b == now.files.end()) ||
            (a != g->profile.files.end() && a->second != b->second))
            out << qs(k);
    }
    return out;
}

SettingsImportStore::Result SettingsImportStore::rollback()
{
    const Manifest m = readManifest();
    if (m.previous == 0 || !readGeneration(m.previous))
        return Result::NothingToRollBack;
    // The generation before the previous one, as its own activation recorded.
    Manifest back;
    back.active = m.previous;
    back.rollback = true;
    const QDir dir(importDir() + QStringLiteral("/generations"));
    for (const QString &name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (const int n = name.toInt(); n < m.previous && readGeneration(n))
            back.previous = std::max(back.previous, n);
    if (!writeManifest(back))
        return Result::StagingFailed;
    return Result::Activated;
}

void SettingsImportStore::materialize(const si::Profile &profile)
{
    auto &settings = m_settings.settings();
    for (const auto &def : application::settingDefinitions()) {
        if (const auto it = profile.values.find(def.id); it != profile.values.end())
            settings.set(def.id, it->second);
        else if (settings.isSet(def.id))
            settings.reset(def.id);
    }
    m_settings.sync();
    for (const QString &path : collectionPaths(m_folder))
        if (!profile.files.contains(ss(path)) && trackedCollection(path))
            QFile::remove(m_folder + QLatin1Char('/') + path);
    for (const auto &[path, bytes] : profile.files)
        if (trackedCollection(qs(path)))
            writeAll(m_folder + QLatin1Char('/') + qs(path), bytesOf(bytes));
}

bool SettingsImportStore::recover()
{
    const Manifest m = readManifest();
    if (m.active == 0 || liveGeneration() == m.active)
        return false;
    const auto g = readGeneration(m.active);
    if (!g)
        return false;
    materialize(m.rollback ? g->profile : merged(g->base, g->profile, liveProfile()));
    setLiveGeneration(m.active);
    return true;
}

} // namespace hikari::app
