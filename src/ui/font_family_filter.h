#pragma once

#include <QAbstractListModel>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

namespace hikari::ui {

// The families whose names contain text, case-insensitively: an exact name first, then names
// that start with text, then the rest, each group in the families' order.
class FontFamilyFilter : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QStringList families READ families WRITE setFamilies NOTIFY familiesChanged)
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
    Q_PROPERTY(QStringList matches READ matches NOTIFY matchesChanged)
    Q_PROPERTY(int count READ count NOTIFY matchesChanged)
public:
    explicit FontFamilyFilter(QObject *parent = nullptr);

    QStringList families() const { return m_families; }
    void setFamilies(const QStringList &families);
    QString text() const { return m_text; }
    void setText(const QString &text);
    QStringList matches() const { return m_matches; }
    int count() const { return int(m_matches.size()); }
    Q_INVOKABLE QString at(int row) const { return m_matches.value(row); }
    Q_INVOKABLE int row(const QString &family) const;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void familiesChanged();
    void textChanged();
    void matchesChanged();

private:
    void refilter();

    QStringList m_families;
    QStringList m_folded;
    QString m_text;
    QStringList m_matches;
};

}
