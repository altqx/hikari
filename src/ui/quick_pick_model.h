#pragma once

#include <QAbstractListModel>
#include <QSet>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration>

#include <optional>
#include <vector>

namespace hikari::ui {

struct FuzzyMatch {
    int score = 0;
    std::vector<int> positions; // UTF-16 offsets into the text
};

// Letters of `query` in order, not necessarily next to each other, ignoring case and spaces in the query.
// Word starts and runs score higher. nullopt: no match; an empty query matches with score 0.
std::optional<FuzzyMatch> fuzzyMatch(const QString &query, const QString &text);

// The quick pick's list: the items a caller gives, filtered and ranked by the field's text, with group
// headings, the current row and, when picking several, the picked set.
// An item is {id, label, description, detail, icon, group, shortcut, trailing, disabled, picked, alwaysShow,
// rank, tooltip, kind}; kind "separator" with a label starts a group, as group does.
class QuickPickModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList items READ items WRITE setItems NOTIFY itemsChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(QStringList matchOn READ matchOn WRITE setMatchOn NOTIFY optionsChanged)
    Q_PROPERTY(bool keepOrder READ keepOrder WRITE setKeepOrder NOTIFY optionsChanged)
    Q_PROPERTY(bool filtering READ filtering WRITE setFiltering NOTIFY optionsChanged)
    Q_PROPERTY(bool many READ many WRITE setMany NOTIFY optionsChanged)
    Q_PROPERTY(int current READ current WRITE setCurrent NOTIFY currentChanged)
    Q_PROPERTY(QString currentId READ currentId NOTIFY currentChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int pickedCount READ pickedCount NOTIFY pickedChanged)

public:
    enum Role {
        KindRole = Qt::UserRole + 1,
        IdRole,
        LabelRole,
        LabelMarkupRole,
        DescriptionRole,
        DescriptionMarkupRole,
        DetailRole,
        IconRole,
        ShortcutRole,
        TrailingRole,
        DisabledRole,
        PickedRole,
        TooltipRole,
        FirstHeadingRole,
        DataRole,
    };

    explicit QuickPickModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QVariantList items() const { return m_items; }
    void setItems(const QVariantList &items);
    QString filter() const { return m_filter; }
    void setFilter(const QString &filter);
    QStringList matchOn() const { return m_matchOn; }
    void setMatchOn(const QStringList &fields);
    bool keepOrder() const { return m_keepOrder; }
    void setKeepOrder(bool keep);
    bool filtering() const { return m_filtering; }
    void setFiltering(bool filtering);
    bool many() const { return m_many; }
    void setMany(bool many);
    int current() const { return m_current; }
    void setCurrent(int row);
    QString currentId() const;
    int count() const { return m_shown; }
    int pickedCount() const { return int(m_picked.size()); }

    // Up and Down wrap at the ends; a page stops there. Headings are skipped.
    Q_INVOKABLE void move(int delta);
    Q_INVOKABLE void page(int delta, int rows);
    Q_INVOKABLE void first();
    Q_INVOKABLE void last();
    Q_INVOKABLE bool setCurrentById(const QString &id);
    Q_INVOKABLE QVariantMap itemAt(int row) const;
    Q_INVOKABLE QVariantMap currentItem() const { return itemAt(m_current); }
    Q_INVOKABLE void toggle(int row);
    Q_INVOKABLE void pickAllShown();
    // In the order the items were given.
    Q_INVOKABLE QStringList pickedIds() const;

signals:
    void itemsChanged();
    void filterChanged();
    void optionsChanged();
    void currentChanged();
    void countChanged();
    void pickedChanged();

private:
    struct Row {
        int item = -1; // -1: a heading
        QString heading;
        bool firstHeading = false;
        std::vector<int> labelMatch;
        std::vector<int> descriptionMatch;
    };
    void rebuild();
    bool isItem(int row) const;

    QVariantList m_items;
    std::vector<QString> m_groups; // per item, after separators
    QString m_filter;
    QStringList m_matchOn;
    bool m_keepOrder = false;
    bool m_filtering = true;
    bool m_many = false;
    std::vector<Row> m_rows;
    int m_current = -1;
    int m_shown = 0;
    QSet<QString> m_picked;
};

}
