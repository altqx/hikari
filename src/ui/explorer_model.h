#pragma once

#include "hikari/application/explorer.h"

#include <QAbstractListModel>
#include <QFileSystemWatcher>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>
#include <QtQmlIntegration>

#include <memory>
#include <vector>

namespace hikari::ui {

// The Explorer panel's tree as its visible rows: the Project folder's files that pass the filter,
// folders first, with expanded folders and groups followed by their children.
class ExplorerModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString folder READ folder WRITE setFolder NOTIFY folderChanged)
    Q_PROPERTY(QStringList excludes READ excludes WRITE setExcludes NOTIFY filterChanged)
    Q_PROPERTY(bool allFiles READ allFiles WRITE setAllFiles NOTIFY filterChanged)
    Q_PROPERTY(bool groupByName READ groupByName WRITE setGroupByName NOTIFY filterChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int currentRow READ currentRow WRITE setCurrentRow NOTIFY currentChanged)
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentChanged)
public:
    enum Role { NameRole = Qt::UserRole + 1, PathRole, DepthRole, KindRole, ExpandableRole, ExpandedRole };
    Q_ENUM(Role)

    explicit ExplorerModel(QObject *parent = nullptr);
    ~ExplorerModel() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString folder() const { return m_root; }
    void setFolder(const QString &folder);
    QStringList excludes() const { return m_excludes; }
    void setExcludes(const QStringList &excludes);
    bool allFiles() const { return m_filter.allFiles; }
    void setAllFiles(bool all);
    bool groupByName() const { return m_groupByName; }
    void setGroupByName(bool group);
    int count() const { return static_cast<int>(m_rows.size()); }
    int currentRow() const { return rowOf(m_currentPath); }
    void setCurrentRow(int row);
    QString currentPath() const { return m_currentPath; }

    Q_INVOKABLE int rowOf(const QString &path) const;
    Q_INVOKABLE QVariantMap rowAt(int row) const;
    Q_INVOKABLE void setExpanded(int row, bool expanded);
    Q_INVOKABLE void toggle(int row);
    // The folder a new file at `row` goes to: the row's own folder, else the one holding it.
    Q_INVOKABLE QString folderAt(int row) const;
    // "" when `name` may be given to a new file in `folder`, or to `renaming` beside it.
    Q_INVOKABLE QString nameProblem(const QString &folder, const QString &name, const QString &renaming = {}) const;
    // {path, problem}: path is the renamed file or folder.
    Q_INVOKABLE QVariantMap rename(const QString &path, const QString &name);
    // "" when moved.
    Q_INVOKABLE QString moveToTrash(const QString &path);
    // {path, problem}: an Untitled Document's file.
    Q_INVOKABLE QVariantMap newSubtitleFile(const QString &folder, const QString &name);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE static QString nativePath(const QString &path);

signals:
    void folderChanged();
    void filterChanged();
    void countChanged();
    void currentChanged();

private:
    struct Node;
    struct Row {
        QString path;
        QString name;
        QString kind;
        int depth = 0;
        bool expandable = false;
        bool expanded = false;
        bool operator==(const Row &) const = default;
    };

    void reload();
    void load(Node &folder);
    Node *find(const QString &path);
    void watchedFolderChanged(const QString &path);
    void rescanPending();
    void rebuild();
    void appendFolder(std::vector<Row> &rows, const Node &folder, int depth);
    void appendFile(std::vector<Row> &rows, const Node &file, int depth, bool expandable);
    void apply(std::vector<Row> rows);
    QString relative(const QString &path) const;
    QString groupKey(const QString &lead) const;

    QString m_root;
    QStringList m_excludes;
    application::explorer::Filter m_filter;
    bool m_groupByName = false;
    std::unique_ptr<Node> m_tree;
    std::vector<Row> m_rows;
    QSet<QString> m_expanded;
    QString m_currentPath;
    QFileSystemWatcher m_watcher;
    QSet<QString> m_pending;
    QTimer m_rescan;
};

}
