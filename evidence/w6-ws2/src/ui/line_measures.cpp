#include "line_measures.h"

#include <QTextBoundaryFinder>

#include <cmath>

namespace hikari::ui {

namespace {

// Lengths (UTF-16 units) of the word segments holding a letter or a number.
int wordChars(const QString &text)
{
    QTextBoundaryFinder finder(QTextBoundaryFinder::Word, text);
    int count = 0;
    qsizetype start = 0;
    for (qsizetype end = finder.toNextBoundary(); end >= 0; end = finder.toNextBoundary()) {
        const QStringView segment = QStringView(text).mid(start, end - start);
        bool counted = false;
        for (qsizetype i = 0; i < segment.size() && !counted; ++i) {
            char32_t c = segment[i].unicode();
            if (QChar::isHighSurrogate(c) && i + 1 < segment.size() && segment[i + 1].isLowSurrogate()) {
                c = QChar::surrogateToUcs4(segment[i], segment[i + 1]);
                ++i;
            }
            counted = QChar::isLetterOrNumber(c);
        }
        if (counted)
            count += static_cast<int>(segment.size());
        start = end;
    }
    return count;
}

} // namespace

LineMeasures measureLine(const QString &text, core::SubtitleFormat format, MeasureOptions options)
{
    const bool lineFormat = format == core::SubtitleFormat::TMPlayer || format == core::SubtitleFormat::MicroDvd ||
                            format == core::SubtitleFormat::Mpl2;
    const QChar split = lineFormat ? QLatin1Char('|') : QLatin1Char('\\');
    const QChar open = format == core::SubtitleFormat::Srt ? QLatin1Char('<') : QLatin1Char('{');
    const QChar close = format == core::SubtitleFormat::Srt ? QLatin1Char('>') : QLatin1Char('}');
    LineMeasures out;
    QString checkText;
    int hardSpaceOffset = 0;
    bool block = false, drawing = false;
    // SpellChecker::Check: one wrap's counts.
    auto check = [&](int fullTextLen) {
        const int counted = wordChars(checkText);
        out.chars += options.allCharsForCps ? fullTextLen : counted;
        const int wrapChars = options.allCharsForWraps ? fullTextLen : counted;
        if (!out.badWraps)
            out.badWraps = wrapChars > 43 || out.wraps.count(QLatin1Char('/')) > 1;
        out.wraps += QString::number(wrapChars) + QLatin1Char('/');
        checkText.clear();
    };
    const qsizetype length = text.size();
    for (qsizetype i = 0; i < length; ++i) {
        const QChar ch = text[i];
        if (block && ch == QLatin1Char('p') && i > 0 && text[i - 1] == QLatin1Char('\\') && i + 1 < length &&
            text[i + 1].isDigit())
            drawing = text[i + 1] != QLatin1Char('0');
        if (ch == open) {
            block = true;
        } else if (ch == close) {
            block = false;
            continue;
        }
        if (block || drawing)
            continue;
        if (ch != split) {
            checkText += ch;
            continue;
        }
        const int fullTextLen = static_cast<int>(checkText.size());
        if (lineFormat) {
            checkText += QLatin1Char(' ');
            check(fullTextLen);
            continue;
        }
        const QChar next = text[i + 1 < length ? i + 1 : i];
        const bool wrap = next == QLatin1Char('N') || next == QLatin1Char('n');
        if (wrap || next == QLatin1Char('h')) {
            checkText += QStringLiteral("  ");
            if (next == QLatin1Char('h'))
                hardSpaceOffset += 1;
        } else {
            checkText += ch;
            checkText += next;
        }
        if (wrap) {
            check(fullTextLen - hardSpaceOffset);
            hardSpaceOffset = 0;
        }
        ++i;
    }
    if (!checkText.isEmpty())
        check(static_cast<int>(checkText.size()) - hardSpaceOffset);
    if (out.wraps.isEmpty())
        out.wraps = QStringLiteral("0/");
    // TextData::GetStrippedWraps.
    if (out.wraps.endsWith(QLatin1Char('/')))
        out.wraps.chop(1);
    return out;
}

int legacyCps(int chars, std::int64_t startMs, std::int64_t endMs)
{
    const float seconds = static_cast<float>(endMs - startMs) / 1000.0f;
    const float value = static_cast<float>(chars) / seconds;
    if (!std::isfinite(value) || value < 0 || value > 999)
        return 999;
    const int cps = static_cast<int>(value);
    return cps > 999 ? 999 : cps;
}

} // namespace hikari::ui
