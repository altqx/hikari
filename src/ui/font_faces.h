#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace hikari::ui {

class FontFaces : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
public:
    explicit FontFaces(QObject *parent = nullptr);
    Q_INVOKABLE QVariantMap face(const QString &family);
    Q_INVOKABLE void forget();
    int revision() const { return m_revision; }
    Q_INVOKABLE int lookups() const { return int(m_faces.size()); }

signals:
    void revisionChanged();

private:
    QHash<QString, QVariantMap> m_faces;
    int m_revision = 0;
};

}
