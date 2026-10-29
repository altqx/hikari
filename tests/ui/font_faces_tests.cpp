#include "font_faces.h"

#include <QFontDatabase>
#include <QRawFont>
#include <QtTest>

using namespace hikari;

class FontFacesTest : public QObject {
    Q_OBJECT
private slots:
    void eachFamilyInItsOwnFace()
    {
        QString text;
        for (const QString &family : QFontDatabase::families()) {
            if (QFontDatabase::isPrivateFamily(family) || !QFontDatabase::writingSystems(family).contains(QFontDatabase::Latin))
                continue;
            const QRawFont raw = QRawFont::fromFont(QFont(family));
            if (raw.isValid() && std::all_of(family.cbegin(), family.cend(), [&](QChar c) { return raw.supportsCharacter(c); })) {
                text = family;
                break;
            }
        }
        if (text.isEmpty())
            QSKIP("no system text font");
        const auto family = [](const char *file) {
            const int id = QFontDatabase::addApplicationFont(QString::fromUtf8(file));
            return id >= 0 ? QFontDatabase::applicationFontFamilies(id).value(0) : QString();
        };
        const QString symbol = family(HIKARI_SYMBOL_FONT);
        QVERIFY(!symbol.isEmpty());
        const QString noLetters = family(HIKARI_NO_LETTERS_FONT);
        QVERIFY(!noLetters.isEmpty());

        ui::FontFaces faces;
        QCOMPARE(faces.lookups(), 0);
        const QVariantMap own = faces.face(text);
        QCOMPARE(own.value(QStringLiteral("family")).toString(), text);
        QCOMPARE(own.value(QStringLiteral("sample")).toString(), QString());
        const QVariantMap pictures = faces.face(symbol);
        QCOMPARE(pictures.value(QStringLiteral("family")).toString(), symbol);
        QCOMPARE(pictures.value(QStringLiteral("sample")).toString(), QStringLiteral("AaBbYyZz"));
        const QVariantMap neither = faces.face(noLetters);
        QCOMPARE(neither.value(QStringLiteral("family")).toString(), QString());
        QCOMPARE(neither.value(QStringLiteral("sample")).toString(), QString());
        const QVariantMap missing = faces.face(QStringLiteral("External extra.ttf"));
        QCOMPARE(missing.value(QStringLiteral("family")).toString(), QString());
        QCOMPARE(missing.value(QStringLiteral("sample")).toString(), QString());
        QCOMPARE(faces.face(QString()).value(QStringLiteral("family")).toString(), QString());
        QCOMPARE(faces.lookups(), 5);
        QCOMPARE(faces.face(text), own);
        QCOMPARE(faces.lookups(), 5);

        QSignalSpy revision(&faces, &ui::FontFaces::revisionChanged);
        const int before = faces.revision();
        faces.forget();
        QCOMPARE(faces.lookups(), 0);
        QCOMPARE(faces.revision(), before + 1);
        QCOMPARE(faces.face(text), own);
        QCOMPARE(faces.lookups(), 1);
        QFontDatabase::removeApplicationFont(QFontDatabase::addApplicationFont(QString::fromUtf8(HIKARI_SYMBOL_FONT)));
        QTRY_VERIFY(revision.size() >= 2);
        QCOMPARE(faces.lookups(), 0);
    }
};

QTEST_MAIN(FontFacesTest)
#include "font_faces_tests.moc"
