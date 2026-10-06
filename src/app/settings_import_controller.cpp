#include "hikari/app/settings_import_controller.h"

#include <QCoreApplication>
#include <QDir>
#include <QVariantMap>

namespace hikari::app {

namespace si = application::settings_import;

namespace {

QString qs(std::string_view s)
{
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}

QString tr(const char *text)
{
    return QCoreApplication::translate("SettingsImport", text);
}

QString valueText(const std::optional<application::SettingValue> &v)
{
    if (!v)
        return {};
    return std::visit(
        [](const auto &x) -> QString {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, bool>)
                return x ? QStringLiteral("true") : QStringLiteral("false");
            else if constexpr (std::is_same_v<T, std::int64_t>)
                return QString::number(x);
            else if constexpr (std::is_same_v<T, std::string>)
                return qs(x);
            else {
                QStringList list;
                for (const auto &e : x)
                    list << qs(e);
                return list.join(QStringLiteral(" | "));
            }
        },
        *v);
}

QString kindName(si::RowKind kind)
{
    switch (kind) {
    case si::RowKind::File:
        return QStringLiteral("file");
    case si::RowKind::Setting:
        return QStringLiteral("setting");
    case si::RowKind::Shortcut:
        return QStringLiteral("shortcut");
    case si::RowKind::Macro:
        return QStringLiteral("macro");
    case si::RowKind::Collection:
        return QStringLiteral("collection");
    }
    return {};
}

} // namespace

SettingsImportController::SettingsImportController(SettingsImportStore *store, QObject *parent)
    : QObject(parent), m_store(store)
{
}

QVariantList SettingsImportController::roots() const
{
    QVariantList out;
    for (const auto &r : m_roots)
        out << QVariantMap{{QStringLiteral("path"), QDir::toNativeSeparators(r.path)},
                           {QStringLiteral("files"), r.files.join(QStringLiteral(", "))}};
    return out;
}

QVariantList SettingsImportController::rows() const
{
    QVariantList out;
    if (!m_plan)
        return out;
    for (const auto &r : m_plan->rows) {
        QStringList paths;
        for (const auto &p : r.unresolvedPaths)
            paths << qs(p);
        out << QVariantMap{{QStringLiteral("id"), qs(r.id)},
                           {QStringLiteral("kind"), kindName(r.kind)},
                           {QStringLiteral("source"), qs(r.source)},
                           {QStringLiteral("key"), qs(r.sourceKey)},
                           {QStringLiteral("raw"), qs(r.raw)},
                           {QStringLiteral("destination"), qs(r.destination)},
                           {QStringLiteral("value"), valueText(r.value)},
                           {QStringLiteral("current"), valueText(r.current)},
                           {QStringLiteral("disposition"), qs(si::dispositionName(r.disposition))},
                           {QStringLiteral("reason"), qs(r.reason)},
                           {QStringLiteral("selectable"), r.selectable},
                           {QStringLiteral("paths"), paths}};
    }
    return out;
}

QStringList SettingsImportController::chosenIds() const
{
    QStringList out;
    for (const auto &id : m_chosen)
        out << qs(id);
    return out;
}

QString SettingsImportController::summary() const
{
    if (!m_plan)
        return {};
    std::map<si::Disposition, int> counts;
    for (const auto &r : m_plan->rows)
        ++counts[r.disposition];
    return tr("%1 to import of %2 changes, %3 unchanged, %4 unresolved, %5 excluded, %6 retired")
        .arg(m_chosen.size())
        .arg(counts[si::Disposition::Change] + counts[si::Disposition::Missing])
        .arg(counts[si::Disposition::Unchanged])
        .arg(counts[si::Disposition::Unresolved])
        .arg(counts[si::Disposition::Excluded])
        .arg(counts[si::Disposition::Retired]);
}

void SettingsImportController::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}

void SettingsImportController::discover(const QStringList &candidates)
{
    m_roots = SettingsImportStore::discoverRoots(candidates.isEmpty() ? SettingsImportStore::defaultCandidates()
                                                                      : candidates);
    emit rootsChanged();
    setStatus(m_roots.isEmpty() ? tr("No legacy settings were found beside the program. Choose the folder of a legacy "
                                     "installation.")
                                : QString());
}

bool SettingsImportController::addRoot(const QString &folder)
{
    const QUrl url(folder);
    const QString path = url.isLocalFile() ? url.toLocalFile() : folder;
    const auto root = SettingsImportStore::inspectRoot(path);
    if (!root) {
        setStatus(tr("%1 holds no legacy settings (Config, Dictionary).").arg(QDir::toNativeSeparators(path)));
        return false;
    }
    if (std::ranges::none_of(m_roots, [&](const auto &r) { return r.path == root->path; })) {
        m_roots << *root;
        emit rootsChanged();
    }
    setStatus({});
    return true;
}

bool SettingsImportController::choose(const QString &rootPath)
{
    if (!m_store)
        return false;
    const auto root = SettingsImportStore::inspectRoot(QDir::fromNativeSeparators(rootPath));
    if (!root)
        return false;
    QString error;
    auto snap = m_store->snapshot(*root, &error);
    if (!snap) {
        setStatus(error);
        return false;
    }
    m_snapshot = std::move(snap);
    m_interpretation = 0;
    propose();
    setStatus({});
    return true;
}

void SettingsImportController::rebuild()
{
    if (!m_store || !m_snapshot)
        return;
    m_previous = m_store->activeReceipt();
    si::PlanOptions o;
    if (m_interpretation == 1)
        o.interpretation = si::Interpretation::Latin1;
    else if (m_interpretation == 2)
        o.interpretation = si::Interpretation::Windows1252;
    for (const QString &t : m_snapshot->themeFiles)
        o.themeFiles.push_back(t.toStdString());
    o.previous = m_previous ? &*m_previous : nullptr;
    m_revision = m_store->revision();
    m_plan = si::buildPlan(m_snapshot->sources, m_store->destination(), o);
}

void SettingsImportController::propose()
{
    rebuild();
    if (m_plan)
        m_chosen = si::proposedRows(*m_plan);
    emit planChanged();
    emit chosenChanged();
}

void SettingsImportController::setInterpretation(int interpretation)
{
    if (interpretation == m_interpretation)
        return;
    m_interpretation = interpretation;
    propose();
}

void SettingsImportController::setChosen(const QString &id, bool chosen)
{
    if (!m_plan)
        return;
    const si::PlanRow *row = m_plan->row(id.toStdString());
    if (!row || !row->selectable)
        return;
    if (chosen ? m_chosen.insert(row->id).second : m_chosen.erase(row->id) > 0)
        emit chosenChanged();
}

void SettingsImportController::chooseAll(const QString &which, bool chosen)
{
    if (!m_plan)
        return;
    if (which == QLatin1String("proposed")) {
        m_chosen = si::proposedRows(*m_plan);
    } else {
        for (const auto &r : m_plan->rows) {
            if (!r.selectable || qs(si::dispositionName(r.disposition)) != which)
                continue;
            if (chosen)
                m_chosen.insert(r.id);
            else
                m_chosen.erase(r.id);
        }
    }
    emit chosenChanged();
}

bool SettingsImportController::importChosen()
{
    if (!m_store || !m_plan || !m_snapshot)
        return false;
    const auto result = m_store->activate(*m_plan, m_chosen, *m_snapshot, m_revision);
    switch (result) {
    case SettingsImportStore::Result::Activated:
        setStatus(tr("Imported. The settings take effect when HikariSub starts again; a setting you change before "
                     "then keeps your change. The legacy files were not changed."));
        emit stateChanged();
        propose();
        return true;
    case SettingsImportStore::Result::NoChange:
        setStatus(tr("Nothing to import: the chosen settings are already in effect."));
        return true;
    case SettingsImportStore::Result::StaleSources:
        setStatus(tr("The legacy files changed since they were read. Read them again to review the changes."));
        return false;
    case SettingsImportStore::Result::StaleDestination:
        setStatus(tr("The current settings changed since this review. Review the import again."));
        propose();
        return false;
    case SettingsImportStore::Result::StagingFailed:
        setStatus(tr("The import could not be prepared; the current settings stay in effect."));
        return false;
    case SettingsImportStore::Result::NothingToRollBack:
        break;
    }
    return false;
}

QStringList SettingsImportController::rollbackEdits() const
{
    return m_store ? m_store->editsSinceActivation() : QStringList();
}

QStringList SettingsImportController::keptEdits() const
{
    return m_store ? m_store->editsKeptOverImport() : QStringList();
}

bool SettingsImportController::rollback()
{
    if (!m_store)
        return false;
    const auto result = m_store->rollback();
    if (result != SettingsImportStore::Result::Activated) {
        setStatus(tr("There is no earlier generation of the settings to go back to."));
        return false;
    }
    setStatus(tr("The settings before the import take effect when HikariSub starts again."));
    emit stateChanged();
    if (m_snapshot) {
        propose();
    }
    return true;
}

} // namespace hikari::app
