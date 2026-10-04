#include "spelling_text.h"

#include <QLocale>

#include <map>

namespace hikari::ui {

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
