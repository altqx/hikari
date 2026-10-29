#include "font_faces.h"

#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QRawFont>


namespace hikari::ui {

namespace {

// A Windows symbol cmap (platform 3, encoding 0) maps letters to pictures.
bool symbolCmap(const QRawFont &font)
{
    const QByteArray cmap = font.fontTable("cmap");
    const auto u16 = [&](qsizetype at) {
        return at + 2 <= cmap.size() ? quint16(quint8(cmap[at]) << 8 | quint8(cmap[at + 1])) : quint16(0);
    };
    const int tables = u16(2);
    for (int i = 0; i < tables; ++i)
        if (u16(4 + 8 * i) == 3 && u16(6 + 8 * i) == 0)
            return true;
    return false;
}

}

FontFaces::FontFaces(QObject *parent) : QObject(parent)
{
    connect(qApp, &QGuiApplication::fontDatabaseChanged, this, &FontFaces::forget);
}

void FontFaces::forget()
{
    m_faces.clear();
    ++m_revision;
    emit revisionChanged();
}

QVariantMap FontFaces::face(const QString &family)
{
    if (const auto found = m_faces.constFind(family); found != m_faces.cend())
        return *found;
    QString face;
    QString sample;
    if (!family.isEmpty() && QFontDatabase::hasFamily(family)) {
        const QRawFont raw = QRawFont::fromFont(QFont(family));
        const auto systems = QFontDatabase::writingSystems(family);
        // Qt lists a font without OS/2 script bits under Symbol alone (URW's Dingbats has a Unicode cmap).
        const bool symbol = raw.isValid()
                            && (symbolCmap(raw) || systems == QList<QFontDatabase::WritingSystem>{QFontDatabase::Symbol});
        bool drawsName = raw.isValid() && !symbol;
        for (const QChar c : family)
            drawsName = drawsName && raw.supportsCharacter(c);
        if (symbol) {
            if (raw.supportsCharacter(QLatin1Char('A')))
                sample = QStringLiteral("AaBbYyZz");
        } else if (!drawsName) {
            for (const auto system : systems)
                if (system != QFontDatabase::Latin && system != QFontDatabase::Symbol
                    && system != QFontDatabase::Any && system != QFontDatabase::Other) {
                    sample = QFontDatabase::writingSystemSample(system);
                    break;
                }
        }
        if (drawsName || !sample.isEmpty())
            face = family;
    }
    const QVariantMap answer{{QStringLiteral("family"), face}, {QStringLiteral("sample"), sample}};
    m_faces.insert(family, answer);
    return answer;
}

}
