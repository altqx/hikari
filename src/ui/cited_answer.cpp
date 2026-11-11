#include "cited_answer.h"

#include <QUrl>
#include <QVariantMap>

#include <algorithm>
#include <vector>

namespace hikari::ui {

namespace {

struct Chip {
    qsizetype at = 0;
    int index = 0;
};

std::vector<Chip> chipsIn(const QString &text, const QVariantList &citations)
{
    std::vector<Chip> chips;
    for (const QVariant &c : citations) {
        const QVariantMap citation = c.toMap();
        bool hasStart = false;
        bool hasEnd = false;
        const qsizetype start = citation.value(QStringLiteral("start")).toLongLong(&hasStart);
        const qsizetype end = citation.value(QStringLiteral("end")).toLongLong(&hasEnd);
        const int index = citation.value(QStringLiteral("index")).toInt();
        if (!hasStart || !hasEnd || start < 0 || start >= end || end > text.size() || index <= 0)
            continue;
        if (end < text.size() && text.at(end).isLowSurrogate())
            continue;
        chips.push_back({end, index});
    }
    std::ranges::sort(chips, [](const Chip &a, const Chip &b) { return a.at != b.at ? a.at < b.at : a.index < b.index; });
    return chips;
}

}

QString citedAnswerHtml(const QString &text, const QVariantList &citations, const QColor &chip, const QColor &ink)
{
    QString html = QStringLiteral("<p style=\"white-space:pre-wrap\">");
    qsizetype from = 0;
    for (const Chip &c : chipsIn(text, citations)) {
        html += text.mid(from, c.at - from).toHtmlEscaped();
        html += QStringLiteral("<a href=\"cite:%1\" style=\"text-decoration:none;color:%2\">"
                               "<span style=\"background-color:%3\">&nbsp;%1&nbsp;</span></a>")
                    .arg(QString::number(c.index), ink.name(QColor::HexRgb), chip.name(QColor::HexRgb));
        from = c.at;
    }
    html += text.mid(from).toHtmlEscaped();
    return html + QStringLiteral("</p>");
}

QString siteOf(const QString &url)
{
    QString host = QUrl(url).host();
    if (host.startsWith(QLatin1String("www.")))
        host.remove(0, 4);
    return host.isEmpty() ? url : host;
}

}
