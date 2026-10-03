#include "spelling_highlighter.h"

#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

namespace hikari::ui {

class SpellingHighlighter::Highlighter : public QSyntaxHighlighter {
public:
    using QSyntaxHighlighter::QSyntaxHighlighter;
    std::vector<std::pair<int, int>> ranges;
    QColor colour;

protected:
    void highlightBlock(const QString &text) override
    {
        const int from = currentBlock().position();
        const int to = from + static_cast<int>(text.size());
        QTextCharFormat format;
        format.setBackground(colour);
        for (const auto &[start, end] : ranges) {
            const int s = std::max(start, from), e = std::min(end, to);
            if (s < e)
                setFormat(s - from, e - s, format);
        }
    }
};

SpellingHighlighter::SpellingHighlighter(QObject *parent) : QObject(parent) {}

SpellingHighlighter::~SpellingHighlighter() = default;

void SpellingHighlighter::setDocument(QQuickTextDocument *document)
{
    if (document == m_document)
        return;
    m_document = document;
    delete m_highlighter;
    m_highlighter = nullptr;
    if (document && document->textDocument()) {
        m_highlighter = new Highlighter(this);
        m_highlighter->colour = m_colour;
        apply();
        m_highlighter->setDocument(document->textDocument());
    }
    emit documentChanged();
}

void SpellingHighlighter::setRanges(const QVariantList &ranges)
{
    if (ranges == m_ranges)
        return;
    m_ranges = ranges;
    apply();
    if (m_highlighter && m_highlighter->document())
        m_highlighter->rehighlight();
    emit rangesChanged();
}

void SpellingHighlighter::setColour(const QColor &colour)
{
    if (colour == m_colour)
        return;
    m_colour = colour;
    if (m_highlighter) {
        m_highlighter->colour = colour;
        if (m_highlighter->document())
            m_highlighter->rehighlight();
    }
    emit colourChanged();
}

void SpellingHighlighter::apply()
{
    if (!m_highlighter)
        return;
    m_highlighter->ranges.clear();
    for (qsizetype i = 0; i + 1 < m_ranges.size(); i += 2)
        m_highlighter->ranges.emplace_back(m_ranges[i].toInt(), m_ranges[i + 1].toInt());
}

QVariantList SpellingHighlighter::appliedRanges() const
{
    QVariantList out;
    if (!m_document || !m_document->textDocument())
        return out;
    for (QTextBlock block = m_document->textDocument()->begin(); block.isValid(); block = block.next())
        for (const QTextLayout::FormatRange &range : block.layout()->formats())
            if (range.format.background().color() == m_colour)
                out << block.position() + range.start << block.position() + range.start + range.length;
    return out;
}

} // namespace hikari::ui
