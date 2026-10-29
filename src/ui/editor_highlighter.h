#pragma once

#include <QColor>
#include <QObject>
#include <QPointer>
#include <QQuickTextDocument>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <utility>
#include <vector>

namespace hikari::ui {

class EditorHighlighter : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickTextDocument *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QVariantList ranges READ ranges WRITE setRanges NOTIFY rangesChanged)
    Q_PROPERTY(QColor colour READ colour WRITE setColour NOTIFY colourChanged)
    // MUST KILL: runs is a flat [start, end, role] list and role 0 keeps the field's own colour
    Q_PROPERTY(QVariantList runs READ runs WRITE setRuns NOTIFY runsChanged)
    Q_PROPERTY(QVariantList colours READ colours WRITE setColours NOTIFY coloursChanged)

public:
    explicit EditorHighlighter(QObject *parent = nullptr);
    ~EditorHighlighter() override;

    QQuickTextDocument *document() const { return m_document; }
    void setDocument(QQuickTextDocument *document);
    QVariantList ranges() const { return m_ranges; }
    void setRanges(const QVariantList &ranges);
    QColor colour() const { return m_colour; }
    void setColour(const QColor &colour);
    QVariantList runs() const { return m_runs; }
    void setRuns(const QVariantList &runs);
    QVariantList colours() const { return m_colours; }
    void setColours(const QVariantList &colours);

    Q_INVOKABLE QVariantList appliedRanges() const;
    Q_INVOKABLE QVariantList appliedColours() const;

signals:
    void documentChanged();
    void rangesChanged();
    void colourChanged();
    void runsChanged();
    void coloursChanged();

private:
    class Highlighter;
    void apply();
    void rehighlight();
    void rehighlightLater();

    QPointer<QQuickTextDocument> m_document;
    QVariantList m_ranges;
    QColor m_colour = QColor(0xFF, 0x69, 0x68);
    QVariantList m_runs;
    QVariantList m_colours;
    Highlighter *m_highlighter = nullptr;
    bool m_rehighlightPending = false;
};

}
