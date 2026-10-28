#include "font_faces.h"

#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QRawFont>


namespace hikari::ui {

namespace {

// A symbol font (Wingdings, Webdings, Symbol): a Windows symbol cmap
// (platform 3, encoding 0), whose letters draw pictures.
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

} // namespace

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
        // The face's own glyphs, without fallback: whether it has the letters
        // of its name.
        const QRawFont raw = QRawFont::fromFont(QFont(family));
        const auto systems = QFontDatabase::writingSystems(family);
        // Or a font Qt lists under Symbol alone, without the OS/2 script bits
        // every text font sets (URW's Dingbats draws its letters as pictures
        // through a Unicode cmap).
        const bool symbol = raw.isValid()
                            && (symbolCmap(raw) || systems == QList<QFontDatabase::WritingSystem>{QFontDatabase::Symbol});
        bool drawsName = raw.isValid() && !symbol;
        for (const QChar c : family)
            drawsName = drawsName && raw.supportsCharacter(c);
        if (symbol) {
            // Its pictures for the letters, when it has any there.
            if (raw.supportsCharacter(QLatin1Char('A')))
                sample = QStringLiteral("AaBbYyZz");
        } else if (!drawsName) {
            // A text in a script the face writes, when the database knows one.
            for (const auto system : systems)
                if (system != QFontDatabase::Latin && system != QFontDatabase::Symbol
                    && system != QFontDatabase::Any && system != QFontDatabase::Other) {
                    sample = QFontDatabase::writingSystemSample(system);
                    break;
                }
        }
        // The name in the face, or the interface font and the sample after it;
        // a face drawing neither stays out (the interface font alone).
        if (drawsName || !sample.isEmpty())
            face = family;
    }
    const QVariantMap answer{{QStringLiteral("family"), face}, {QStringLiteral("sample"), sample}};
    m_faces.insert(family, answer);
    return answer;
}

} // namespace hikari::ui
