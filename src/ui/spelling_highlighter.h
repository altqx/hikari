#pragma once

// F3: the Line editor's spelling marks (legacy TextEditor draws a
// EDITOR_SPELLCHECKER background behind each TextData error range). Attached
// to a TextArea's textDocument, it paints the given display ranges.

#include <QColor>
#include <QObject>
#include <QPointer>
#include <QQuickTextDocument>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <utility>
#include <vector>

namespace hikari::ui {

class SpellingHighlighter : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickTextDocument *document READ document WRITE setDocument NOTIFY documentChanged)
    // Flat [start, end) pairs in the field's text.
    Q_PROPERTY(QVariantList ranges READ ranges WRITE setRanges NOTIFY rangesChanged)
    Q_PROPERTY(QColor colour READ colour WRITE setColour NOTIFY colourChanged)

public:
    explicit SpellingHighlighter(QObject *parent = nullptr);
    ~SpellingHighlighter() override;

    QQuickTextDocument *document() const { return m_document; }
    void setDocument(QQuickTextDocument *document);
    QVariantList ranges() const { return m_ranges; }
    void setRanges(const QVariantList &ranges);
    QColor colour() const { return m_colour; }
    void setColour(const QColor &colour);

    // What the document's layout carries now: flat [start, end) pairs with
    // the mark's background (tests and accessibility checks read it).
    Q_INVOKABLE QVariantList appliedRanges() const;

signals:
    void documentChanged();
    void rangesChanged();
    void colourChanged();

private:
    class Highlighter;
    void apply();

    QPointer<QQuickTextDocument> m_document;
    QVariantList m_ranges;
    QColor m_colour = QColor(0xFF, 0x69, 0x68); // legacy EDITOR_SPELLCHECKER (light)
    Highlighter *m_highlighter = nullptr;
};

} // namespace hikari::ui
