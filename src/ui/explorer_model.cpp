#include "explorer_model.h"

#include "hikari/application/document_files.h"
#include "hikari/application/project.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

namespace hikari::ui {

namespace explorer = application::explorer;

struct ExplorerModel::Node {
    QString name;
    QString path;
    bool folder = false;
    bool hidden = false;
    bool loaded = false;
    bool projectSettings = false;
    std::vector<std::unique_ptr<Node>> children;
};

namespace {

QString kindName(explorer::FileKind kind)
{
    switch (kind) {
    case explorer::FileKind::Subtitles: return QStringLiteral("subtitles");
    case explorer::FileKind::Video: return QStringLiteral("video");
    case explorer::FileKind::Audio: return QStringLiteral("audio");
    case explorer::FileKind::Keyframes: return QStringLiteral("keyframes");
    case explorer::FileKind::Timecodes: return QStringLiteral("timecodes");
    case explorer::FileKind::Fonts: return QStringLiteral("fonts");
    case explorer::FileKind::Chapters: return QStringLiteral("chapters");
    case explorer::FileKind::Other: break;
    }
    return QStringLiteral("other");
}

QLatin1StringView projectFolderName()
{
    return {application::kProjectFolder.data(), static_cast<qsizetype>(application::kProjectFolder.size())};
}

std::string utf8(const QString &s)
{
    return s.toStdString();
}

QString invalidCharacters()
{
#ifdef Q_OS_WIN
    return QStringLiteral("/\\<>:\"|?*");
#else
    return QStringLiteral("/\\");
#endif
}

}

ExplorerModel::ExplorerModel(QObject *parent) : QAbstractListModel(parent)
{
    for (const auto pattern : explorer::kDefaultExcludes)
        m_excludes << QString::fromUtf8(pattern.data(), static_cast<qsizetype>(pattern.size()));
    for (const auto &pattern : std::as_const(m_excludes))
        m_filter.excludes.push_back(utf8(pattern));
    m_rescan.setSingleShot(true);
    m_rescan.setInterval(100);
    connect(&m_rescan, &QTimer::timeout, this, &ExplorerModel::rescanPending);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &ExplorerModel::watchedFolderChanged);
}

ExplorerModel::~ExplorerModel() = default;

int ExplorerModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant ExplorerModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const Row &row = m_rows[static_cast<std::size_t>(index.row())];
    switch (role) {
    case Qt::DisplayRole:
    case NameRole: return row.name;
    case PathRole: return row.path;
    case DepthRole: return row.depth;
    case KindRole: return row.kind;
    case ExpandableRole: return row.expandable;
    case ExpandedRole: return row.expanded;
    default: return {};
    }
}

QHash<int, QByteArray> ExplorerModel::roleNames() const
{
    return {{NameRole, "name"},       {PathRole, "path"},           {DepthRole, "depth"},
            {KindRole, "kind"},       {ExpandableRole, "expandable"}, {ExpandedRole, "expanded"}};
}

void ExplorerModel::setFolder(const QString &folder)
{
    const QString cleaned = folder.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(folder).absoluteFilePath());
    if (cleaned == m_root)
        return;
    m_root = cleaned;
    m_expanded.clear();
    m_currentPath.clear();
    reload();
    emit folderChanged();
}

void ExplorerModel::setExcludes(const QStringList &excludes)
{
    if (excludes == m_excludes)
        return;
    m_excludes = excludes;
    m_filter.excludes.clear();
    for (const auto &pattern : excludes)
        if (!pattern.trimmed().isEmpty())
            m_filter.excludes.push_back(utf8(pattern.trimmed()));
    rebuild();
    emit filterChanged();
}

void ExplorerModel::setAllFiles(bool all)
{
    if (all == m_filter.allFiles)
        return;
    m_filter.allFiles = all;
    rebuild();
    emit filterChanged();
}

void ExplorerModel::setGroupByName(bool group)
{
    if (group == m_groupByName)
        return;
    m_groupByName = group;
    rebuild();
    emit filterChanged();
}

void ExplorerModel::setCurrentRow(int row)
{
    const QString path = row >= 0 && row < count() ? m_rows[static_cast<std::size_t>(row)].path : QString();
    if (path == m_currentPath)
        return;
    m_currentPath = path;
    emit currentChanged();
}

int ExplorerModel::rowOf(const QString &path) const
{
    if (path.isEmpty())
        return -1;
    const auto it = std::ranges::find(m_rows, path, &Row::path);
    return it == m_rows.end() ? -1 : static_cast<int>(it - m_rows.begin());
}

QVariantMap ExplorerModel::rowAt(int row) const
{
    if (row < 0 || row >= count())
        return {};
    const Row &r = m_rows[static_cast<std::size_t>(row)];
    return {{QStringLiteral("name"), r.name},          {QStringLiteral("path"), r.path},
            {QStringLiteral("depth"), r.depth},        {QStringLiteral("kind"), r.kind},
            {QStringLiteral("expandable"), r.expandable}, {QStringLiteral("expanded"), r.expanded}};
}

void ExplorerModel::setExpanded(int row, bool expanded)
{
    if (row < 0 || row >= count() || !m_rows[static_cast<std::size_t>(row)].expandable)
        return;
    const Row &r = m_rows[static_cast<std::size_t>(row)];
    const QString key = r.kind == QLatin1String("folder") ? r.path : groupKey(r.path);
    if (expanded == m_expanded.contains(key))
        return;
    if (expanded)
        m_expanded.insert(key);
    else
        m_expanded.remove(key);
    rebuild();
}

void ExplorerModel::toggle(int row)
{
    if (row >= 0 && row < count())
        setExpanded(row, !m_rows[static_cast<std::size_t>(row)].expanded);
}

QString ExplorerModel::folderAt(int row) const
{
    if (row < 0 || row >= count())
        return m_root;
    const Row &r = m_rows[static_cast<std::size_t>(row)];
    if (r.kind == QLatin1String("folder"))
        return r.path;
    if (r.kind == QLatin1String("projectSettings"))
        return m_root;
    return QFileInfo(r.path).absolutePath();
}

QString ExplorerModel::nameProblem(const QString &folder, const QString &name, const QString &renaming) const
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return QCoreApplication::translate("HikariSub.Explorer", "Type a name.");
    if (trimmed == QLatin1String(".") || trimmed == QLatin1String(".."))
        return QCoreApplication::translate("HikariSub.Explorer", "“%1” can’t be a name.").arg(trimmed);
    const QString invalid = invalidCharacters();
    if (std::ranges::any_of(trimmed, [&invalid](QChar c) { return invalid.contains(c); })) {
        QStringList shown;
        for (const QChar c : invalid)
            shown << QString(c);
        return QCoreApplication::translate("HikariSub.Explorer", "A name can’t contain %1.").arg(shown.join(QLatin1Char(' ')));
    }
    const QString target = QDir(folder).filePath(trimmed);
    const bool sameFile = !renaming.isEmpty() && QFileInfo(target) == QFileInfo(renaming);
    if (!sameFile && QFileInfo::exists(target))
        return QCoreApplication::translate("HikariSub.Explorer", "“%1” is already in this folder.").arg(trimmed);
    return {};
}

QVariantMap ExplorerModel::rename(const QString &path, const QString &name)
{
    const QFileInfo info(path);
    const QString folder = info.absolutePath();
    const auto failed = [](const QString &problem) {
        return QVariantMap{{QStringLiteral("path"), QString()}, {QStringLiteral("problem"), problem}};
    };
    if (const QString problem = nameProblem(folder, name, path); !problem.isEmpty())
        return failed(problem);
    const QString target = QDir::cleanPath(QDir(folder).filePath(name.trimmed()));
    if (target != path && !QDir().rename(path, target))
        return failed(QCoreApplication::translate("HikariSub.Explorer", "Couldn’t rename “%1”.").arg(info.fileName()));
    const QString prefix = path + QLatin1Char('/');
    QSet<QString> expanded;
    for (const QString &key : std::as_const(m_expanded)) {
        if (key == path)
            expanded.insert(target);
        else if (key.startsWith(prefix))
            expanded.insert(target + key.mid(path.size()));
        else if (key == groupKey(path))
            expanded.insert(groupKey(target));
        else
            expanded.insert(key);
    }
    m_expanded = expanded;
    if (m_currentPath == path || m_currentPath.startsWith(prefix))
        m_currentPath = target + m_currentPath.mid(path.size());
    if (Node *parent = find(folder))
        load(*parent);
    rebuild();
    emit currentChanged();
    return {{QStringLiteral("path"), target}, {QStringLiteral("problem"), QString()}};
}

QString ExplorerModel::moveToTrash(const QString &path)
{
    const QFileInfo info(path);
    if (!QFile::moveToTrash(path))
        return QCoreApplication::translate("HikariSub.Explorer", "Couldn’t move “%1” to the trash.").arg(info.fileName());
    if (Node *parent = find(info.absolutePath()))
        load(*parent);
    rebuild();
    return {};
}

QVariantMap ExplorerModel::newSubtitleFile(const QString &folder, const QString &name)
{
    const auto failed = [](const QString &problem) {
        return QVariantMap{{QStringLiteral("path"), QString()}, {QStringLiteral("problem"), problem}};
    };
    if (const QString problem = nameProblem(folder, name); !problem.isEmpty())
        return failed(problem);
    const QString target = QDir::cleanPath(QDir(folder).filePath(name.trimmed()));
    QFile file(target);
    const std::string_view script = application::defaultScript();
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) ||
        file.write(script.data(), static_cast<qint64>(script.size())) != static_cast<qint64>(script.size()) ||
        !file.flush()) {
        file.remove();
        return failed(QCoreApplication::translate("HikariSub.Explorer", "Couldn’t create “%1”.").arg(name.trimmed()));
    }
    file.close();
    if (folder != m_root)
        m_expanded.insert(QDir::cleanPath(folder));
    if (Node *parent = find(QDir::cleanPath(folder)))
        load(*parent);
    m_currentPath = target;
    rebuild();
    emit currentChanged();
    return {{QStringLiteral("path"), target}, {QStringLiteral("problem"), QString()}};
}

void ExplorerModel::refresh()
{
    if (!m_tree)
        return;
    std::vector<Node *> folders{m_tree.get()};
    while (!folders.empty()) {
        Node *folder = folders.back();
        folders.pop_back();
        if (!folder->loaded)
            continue;
        load(*folder);
        for (auto &child : folder->children)
            if (child->folder)
                folders.push_back(child.get());
    }
    rebuild();
}

QString ExplorerModel::nativePath(const QString &path)
{
    return QDir::toNativeSeparators(path);
}

void ExplorerModel::reload()
{
    if (const QStringList watched = m_watcher.directories(); !watched.isEmpty())
        m_watcher.removePaths(watched);
    m_pending.clear();
    m_tree.reset();
    if (!m_root.isEmpty() && QFileInfo(m_root).isDir()) {
        m_tree = std::make_unique<Node>();
        m_tree->path = m_root;
        m_tree->name = QFileInfo(m_root).fileName();
        m_tree->folder = true;
        load(*m_tree);
    }
    rebuild();
}

void ExplorerModel::load(Node &folder)
{
    const QDir dir(folder.path);
    const auto entries =
        dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::NoSort);
    std::vector<std::unique_ptr<Node>> kept;
    const bool atRoot = &folder == m_tree.get();
    folder.projectSettings = false;
    for (const QFileInfo &entry : entries) {
        const QString name = entry.fileName();
        if (atRoot && entry.isDir() && name == projectFolderName()) {
            folder.projectSettings = true;
            continue;
        }
        auto it = std::ranges::find_if(folder.children, [&](const auto &c) { return c && c->name == name; });
        std::unique_ptr<Node> node;
        if (it != folder.children.end() && (*it)->folder == entry.isDir()) {
            node = std::move(*it);
        } else {
            node = std::make_unique<Node>();
            node->name = name;
            node->path = QDir::cleanPath(entry.absoluteFilePath());
            node->folder = entry.isDir();
        }
        node->hidden = entry.isHidden();
        kept.push_back(std::move(node));
    }
    std::ranges::sort(kept, [](const auto &a, const auto &b) {
        if (a->folder != b->folder)
            return a->folder;
        return explorer::naturalLess(utf8(a->name), utf8(b->name));
    });
    folder.children = std::move(kept);
    folder.loaded = true;
    if (!m_watcher.directories().contains(folder.path))
        m_watcher.addPath(folder.path);
}

ExplorerModel::Node *ExplorerModel::find(const QString &path)
{
    if (!m_tree)
        return nullptr;
    const QString cleaned = QDir::cleanPath(path);
    if (cleaned == m_root)
        return m_tree.get();
    const QString rel = relative(cleaned);
    if (rel.isEmpty() || rel.startsWith(QLatin1String("..")))
        return nullptr;
    Node *node = m_tree.get();
    for (const QString &part : rel.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        const auto it = std::ranges::find_if(node->children, [&](const auto &c) { return c->name == part; });
        if (it == node->children.end())
            return nullptr;
        node = it->get();
    }
    return node;
}

void ExplorerModel::watchedFolderChanged(const QString &path)
{
    m_pending.insert(QDir::cleanPath(path));
    m_rescan.start();
}

void ExplorerModel::rescanPending()
{
    const QSet<QString> pending = std::exchange(m_pending, {});
    for (const QString &path : pending) {
        Node *node = find(path);
        if (node && node->loaded && QFileInfo(path).isDir())
            load(*node);
        else if (m_watcher.directories().contains(path) && !QFileInfo(path).isDir())
            m_watcher.removePath(path);
    }
    rebuild();
}

void ExplorerModel::rebuild()
{
    std::vector<Row> rows;
    if (m_tree) {
        if (m_tree->projectSettings)
            rows.push_back({QDir(m_root).filePath(projectFolderName()),
                            QCoreApplication::translate("HikariSub.Explorer", "Project settings"), QStringLiteral("projectSettings"), 0, false, false});
        appendFolder(rows, *m_tree, 0);
    }
    apply(std::move(rows));
}

void ExplorerModel::appendFolder(std::vector<Row> &rows, const Node &folder, int depth)
{
    std::vector<const Node *> files;
    for (const auto &child : folder.children) {
        const std::string rel = utf8(relative(child->path));
        const std::string name = utf8(child->name);
        if (!child->folder) {
            if (explorer::fileShown(m_filter, rel, name, child->hidden))
                files.push_back(child.get());
            continue;
        }
        if (!explorer::folderShown(m_filter, rel, name, child->hidden))
            continue;
        const bool expanded = m_expanded.contains(child->path);
        rows.push_back({child->path, child->name, QStringLiteral("folder"), depth, true, expanded});
        if (!expanded)
            continue;
        if (!child->loaded)
            load(*child);
        appendFolder(rows, *child, depth + 1);
    }
    if (!m_groupByName) {
        for (const Node *file : files)
            appendFile(rows, *file, depth, false);
        return;
    }
    std::vector<std::string> names;
    names.reserve(files.size());
    for (const Node *file : files)
        names.push_back(utf8(file->name));
    std::vector<std::vector<std::size_t>> membersOf(files.size());
    std::vector<char> member(files.size(), 0);
    for (const auto &group : explorer::groupByName(names)) {
        membersOf[group.lead] = group.members;
        for (const std::size_t i : group.members)
            member[i] = 1;
    }
    for (std::size_t i = 0; i < files.size(); ++i) {
        if (member[i])
            continue;
        appendFile(rows, *files[i], depth, !membersOf[i].empty());
        if (!rows.back().expanded)
            continue;
        for (const std::size_t m : membersOf[i])
            appendFile(rows, *files[m], depth + 1, false);
    }
}

void ExplorerModel::appendFile(std::vector<Row> &rows, const Node &file, int depth, bool expandable)
{
    const bool expanded = expandable && m_expanded.contains(groupKey(file.path));
    rows.push_back({file.path, file.name, kindName(explorer::classify(utf8(file.name))), depth, expandable, expanded});
}

void ExplorerModel::apply(std::vector<Row> rows)
{
    const int previousCurrent = currentRow();
    const std::size_t oldSize = m_rows.size();
    const auto samePlace = [](const Row &a, const Row &b) { return a.path == b.path && a.depth == b.depth; };
    std::size_t head = 0;
    while (head < oldSize && head < rows.size() && samePlace(m_rows[head], rows[head]))
        ++head;
    std::size_t tail = 0;
    while (tail < oldSize - head && tail < rows.size() - head &&
           samePlace(m_rows[oldSize - 1 - tail], rows[rows.size() - 1 - tail]))
        ++tail;
    const std::size_t removed = oldSize - head - tail;
    const std::size_t inserted = rows.size() - head - tail;
    if (removed > 0) {
        beginRemoveRows({}, static_cast<int>(head), static_cast<int>(head + removed - 1));
        m_rows.erase(m_rows.begin() + static_cast<std::ptrdiff_t>(head),
                     m_rows.begin() + static_cast<std::ptrdiff_t>(head + removed));
        endRemoveRows();
    }
    if (inserted > 0) {
        beginInsertRows({}, static_cast<int>(head), static_cast<int>(head + inserted - 1));
        m_rows.insert(m_rows.begin() + static_cast<std::ptrdiff_t>(head),
                      rows.begin() + static_cast<std::ptrdiff_t>(head),
                      rows.begin() + static_cast<std::ptrdiff_t>(head + inserted));
        endInsertRows();
    }
    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i] == rows[i])
            continue;
        m_rows[i] = rows[i];
        emit dataChanged(index(static_cast<int>(i)), index(static_cast<int>(i)));
    }
    if (removed > 0 || inserted > 0)
        emit countChanged();
    if (!m_currentPath.isEmpty() && rowOf(m_currentPath) < 0) {
        const int fallback = std::min(previousCurrent, count() - 1);
        m_currentPath = fallback >= 0 ? m_rows[static_cast<std::size_t>(fallback)].path : QString();
        emit currentChanged();
    } else if (currentRow() != previousCurrent) {
        emit currentChanged();
    }
}

QString ExplorerModel::relative(const QString &path) const
{
    return QDir(m_root).relativeFilePath(path);
}

QString ExplorerModel::groupKey(const QString &lead) const
{
    return QStringLiteral("group:") + lead;
}

}
