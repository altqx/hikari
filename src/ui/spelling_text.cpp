#include "spelling_text.h"

#include <QLocale>
#include <QTextBoundaryFinder>

#include <map>

namespace hikari::ui {

namespace {

std::vector<core::legacy::WordSegment> segmentWords(std::u16string_view text)
{
    std::vector<core::legacy::WordSegment> out;
    const QString s = QString::fromUtf16(text.data(), static_cast<qsizetype>(text.size()));
    QTextBoundaryFinder finder(QTextBoundaryFinder::Word, s);
    qsizetype start = 0;
    for (qsizetype end = finder.toNextBoundary(); end >= 0; end = finder.toNextBoundary()) {
        if (end <= start)
            continue;
        core::legacy::WordSegment segment{static_cast<std::size_t>(start), static_cast<std::size_t>(end - start), false,
                                          false};
        bool digits = false;
        for (qsizetype i = start; i < end; ++i) {
            char32_t c = s[i].unicode();
            if (QChar::isHighSurrogate(c) && i + 1 < end && s[i + 1].isLowSurrogate()) {
                c = QChar::surrogateToUcs4(s[i], s[i + 1]);
                ++i;
            }
            segment.letters = segment.letters || QChar::isLetter(c);
            digits = digits || QChar::isDigit(c);
        }
        segment.number = !segment.letters && digits;
        out.push_back(segment);
        start = end;
    }
    return out;
}

} // namespace

application::SpellingText qtSpellingText()
{
    application::SpellingText text;
    text.segment = segmentWords;
    text.cases.isUpper = [](char16_t c) { return QChar(c).isUpper(); };
    text.cases.toUpper = [](char16_t c) { return QChar(c).toUpper().unicode(); };
    text.cases.toLower = [](char16_t c) { return QChar(c).toLower().unicode(); };
    return text;
}

QString dictionaryName(const QString &symbol)
{
    // The languages HikariSub ships a catalogue or a dictionary for.
    static const std::map<QString, QString> shipped = {
        {QStringLiteral("en"), QStringLiteral("English")},
        {QStringLiteral("ko"), QString::fromUtf8("\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4")},
        {QStringLiteral("pl"), QStringLiteral("Polski")},
        {QStringLiteral("ta"), QString::fromUtf8("\xE0\xAE\xA4\xE0\xAE\xAE\xE0\xAE\xBF\xE0\xAE\xB4\xE0\xAF\x8D")},
        {QStringLiteral("th"), QString::fromUtf8("\xE0\xB9\x84\xE0\xB8\x97\xE0\xB8\xA2")},
    };
    auto it = shipped.find(symbol);
    if (it == shipped.end())
        it = shipped.find(symbol.section(QLatin1Char('_'), 0, 0));
    if (it != shipped.end())
        return it->second;
    const QLocale locale(symbol);
    const QString name = locale.language() == QLocale::C ? QString() : locale.nativeLanguageName();
    return name.isEmpty() ? symbol : name;
}

} // namespace hikari::ui
