#pragma once

// SY3: how a font list draws each family in its own typeface (the Style
// editor's font list). face(family) answers {family, sample}: `family` is the
// face to draw the name in, "" when the system's font database has no such
// family (an external font that only the renderer leases, or a name no font
// has) or the face draws neither its name nor a sample, so the name stays in
// the interface font; `sample` is "" when the face
// can draw its own name, otherwise a short text in the face's own script (a
// symbol font, or one without the name's letters, draws its name in the
// interface font and the sample after it in its own face). Answers are kept,
// so each family is looked up once, and only for the rows a list creates,
// until the fonts change: Qt's font database, or a refresh of the font
// catalogs (forget()); `revision` then moves so the rows ask again.

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
    // Drops the answers kept (the fonts changed).
    Q_INVOKABLE void forget();
    int revision() const { return m_revision; }
    // The families looked up so far (the rows a list created).
    Q_INVOKABLE int lookups() const { return int(m_faces.size()); }

signals:
    void revisionChanged();

private:
    QHash<QString, QVariantMap> m_faces;
    int m_revision = 0;
};

} // namespace hikari::ui
