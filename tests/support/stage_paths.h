#pragma once

#include <QRegularExpression>
#include <QString>
#include <QStringList>

namespace hikari::testing {

#ifdef _WIN32
inline constexpr bool kWindows = true;
#else
inline constexpr bool kWindows = false;
#endif

// Windows messages mix separators and drive-letter case, double separators, and DependencyControl's Logger.moon turns "C:/" into "C: /".
inline QString normalizedPaths(QString text, const QStringList &dirs, bool windows = kWindows)
{
    const Qt::CaseSensitivity cs = windows ? Qt::CaseInsensitive : Qt::CaseSensitive;
    if (windows)
        text.replace(QLatin1Char('\\'), QLatin1Char('/'));
    for (QString dir : dirs) {
        if (windows)
            dir.replace(QLatin1Char('\\'), QLatin1Char('/'));
        text.replace(dir, QStringLiteral("<stage>"), cs);
        if (windows && dir.size() > 2 && dir.at(1) == QLatin1Char(':'))
            text.replace(dir.left(2) + QLatin1Char(' ') + dir.mid(2), QStringLiteral("<stage>"), cs);
    }
    QString out = text;
    if (windows) {
        static const QRegularExpression staged(QStringLiteral(R"(<stage>[^"'\s()]*)"));
        static const QRegularExpression doubled(QStringLiteral("/{2,}"));
        out.clear();
        qsizetype at = 0;
        for (auto it = staged.globalMatch(text); it.hasNext();) {
            const QRegularExpressionMatch m = it.next();
            QString path = m.captured();
            out += text.mid(at, m.capturedStart() - at) + path.replace(doubled, QStringLiteral("/"));
            at = m.capturedEnd();
        }
        out += text.mid(at);
    }
    // Lua truncates a long chunk name, so it depends on the build path.
    static const QRegularExpression chunk(QStringLiteral(R"(\[string "[^"]*"\])"));
    out.replace(chunk, QStringLiteral(R"([string "<chunk>"])"));
    return out;
}

}
