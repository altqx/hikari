#include "search_suggestions_view.h"

#include "hikari/application/search_suggestions.h"

#include <QUrl>
#include <QVariantList>

namespace hikari::ui {

namespace {

QUrl svgDataUrl(const std::string &svg)
{
    if (svg.empty())
        return {};
    return QUrl(QStringLiteral("data:image/svg+xml;base64,") +
                QString::fromLatin1(QByteArray::fromStdString(svg).toBase64()));
}

}

QVariantMap searchSuggestionsView(std::string_view html)
{
    const auto parts = application::ai::parseSearchSuggestions(html);
    if (!parts)
        return {};
    QVariantList chips;
    for (const auto &chip : parts->chips)
        chips.append(QVariantMap{{QStringLiteral("text"), QString::fromStdString(chip.text)},
                                 {QStringLiteral("url"), QUrl(QString::fromStdString(chip.url))}});
    return {{QStringLiteral("chips"), chips},
            {QStringLiteral("lightLogo"), svgDataUrl(parts->lightLogo)},
            {QStringLiteral("darkLogo"), svgDataUrl(parts->darkLogo)}};
}

}
