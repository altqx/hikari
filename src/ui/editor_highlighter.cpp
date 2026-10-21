#include "editor_highlighter.h"

#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

#include <algorithm>
#include <tuple>

namespace hikari::ui {

class EditorHighlighter::Highlighter : public QSyntaxHighlighter {
public:
    using QSyntaxHighlighter::QSyntaxHighlighter;
    std::vector<std::pair<int, int>> ranges;
    QColor colour;
    std::vector<std::tuple<int, int, QColor>> runs; // [start, end) in a colour

protected:
    void highlightBlock(const QString &text) override
    {
        const int from = currentBlock().position();
        const int to = from + static_cast<int>(text.size());
        for (const auto &[start, end, foreground] : runs) {
            const int s = std::max(start, from), e = std::min(end, to);
            if (s >= e)
                continue;
            QTextCharFormat format;
            format.setForeground(foreground);
            setFormat(s - from, e - s, format);
        }
        // The spelling marks over the colours.
        for (const auto &[start, end] : ranges) {
            const int s = std::max(start, from), e = std::min(end, to);
            for (int i = s; i < e; ++i) {
                QTextCharFormat format = this->format(i - from);
                format.setBackground(colour);
                setFormat(i - from, 1, format);
            }
        }
    }
};

EditorHighlighter::EditorHighlighter(QObject *parent) : QObject(parent) {}

EditorHighlighter::~EditorHighlighter() = default;

void EditorHighlighter::setDocument(QQuickTextDocument *document)
{
    if (document == m_document)
        return;
    m_document = document;
    delete m_highlighter;
    m_highlighter = nullptr;
    if (document && document->textDocument()) {
        m_highlighter = new Highlighter(this);
        apply();
        m_highlighter->setDocument(document->textDocument());
    }
    emit documentChanged();
}

void EditorHighlighter::setRanges(const QVariantList &ranges)
{
    if (ranges == m_ranges)
        return;
    m_ranges = ranges;
    rehighlight();
    emit rangesChanged();
}

void EditorHighlighter::setColour(const QColor &colour)
{
    if (colour == m_colour)
        return;
    m_colour = colour;
    rehighlight();
    emit colourChanged();
}

void EditorHighlighter::setRuns(const QVariantList &runs)
{
    if (runs == m_runs)
        return;
    m_runs = runs;
    rehighlightLater();
    emit runsChanged();
}

void EditorHighlighter::setColours(const QVariantList &colours)
{
    if (colours == m_colours)
        return;
    m_colours = colours;
    rehighlightLater();
    emit coloursChanged();
}

void EditorHighlighter::rehighlight()
{
    apply();
    if (m_highlighter && m_highlighter->document())
        m_highlighter->rehighlight();
}

// The colours follow a binding on the field's text: formatting the document
// notifies the text again, so it is done once the binding has settled.
void EditorHighlighter::rehighlightLater()
{
    if (m_rehighlightPending)
        return;
    m_rehighlightPending = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
            m_rehighlightPending = false;
            rehighlight();
        },
        Qt::QueuedConnection);
}

void EditorHighlighter::apply()
{
    if (!m_highlighter)
        return;
    m_highlighter->colour = m_colour;
    m_highlighter->ranges.clear();
    for (qsizetype i = 0; i + 1 < m_ranges.size(); i += 2)
        m_highlighter->ranges.emplace_back(m_ranges[i].toInt(), m_ranges[i + 1].toInt());
    m_highlighter->runs.clear();
    for (qsizetype i = 0; i + 2 < m_runs.size(); i += 3) {
        const int role = m_runs[i + 2].toInt();
        if (role <= 0 || role >= m_colours.size())
            continue; // the field's own text colour
        const QColor colour = m_colours[role].value<QColor>();
        if (colour.isValid())
            m_highlighter->runs.emplace_back(m_runs[i].toInt(), m_runs[i + 1].toInt(), colour);
    }
}

QVariantList EditorHighlighter::appliedRanges() const
{
    QVariantList out;
    if (!m_document || !m_document->textDocument())
        return out;
    for (QTextBlock block = m_document->textDocument()->begin(); block.isValid(); block = block.next())
        for (const QTextLayout::FormatRange &range : block.layout()->formats())
            if (range.format.background().style() != Qt::NoBrush && range.format.background().color() == m_colour) {
                const int start = block.position() + range.start, end = start + range.length;
                // The marks are set one character at a time: report each run once.
                if (!out.isEmpty() && out.last().toInt() == start)
                    out.last() = end;
                else
                    out << start << end;
            }
    return out;
}

QVariantList EditorHighlighter::appliedColours() const
{
    QVariantList out;
    if (!m_document || !m_document->textDocument())
        return out;
    for (QTextBlock block = m_document->textDocument()->begin(); block.isValid(); block = block.next())
        for (const QTextLayout::FormatRange &range : block.layout()->formats()) {
            if (range.format.foreground().style() == Qt::NoBrush)
                continue;
            const int start = block.position() + range.start, end = start + range.length;
            const QString name = range.format.foreground().color().name();
            if (out.size() >= 3 && out[out.size() - 2].toInt() == start && out.last().toString() == name)
                out[out.size() - 2] = end;
            else
                out << start << end << name;
        }
    return out;
}

} // namespace hikari::ui
