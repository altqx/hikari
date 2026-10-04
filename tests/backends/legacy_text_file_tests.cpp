// R4-uchardet / R5-per-platform: legacy OpenWrite::FileOpen(path, &text,
// true) at 20d647c4 (OpennWrite.cpp), as find and replace in files reads.

#include "hikari/backends/legacy_text_file.h"

#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace hikari::backends;
using P = LegacyTextPlatform;

namespace {

QString q(const std::optional<std::u16string> &s)
{
    return s ? QString(reinterpret_cast<const QChar *>(s->data()), static_cast<qsizetype>(s->size()))
             : QStringLiteral("<nullopt>");
}

QString u(const char *utf8)
{
    return QString::fromUtf8(utf8);
}

constexpr const char *kDialogue = "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,";

// The same two lines in cp1250 (Polish), with CRLF.
const QByteArray kCp1250 = QByteArray::fromHex(
    "4469616c6f6775653a20302c303a30303a30312e30302c303a30303a30322e30302c44656661756c742c2c302c302c302c2c5a61bff3b3e62067"
    "ea9c6cb9206a619ff12c209f649f62b36f2074726177792e0d0a4469616c6f6775653a20302c303a30303a30332e30302c303a30303a30342e30"
    "302c44656661756c742c2c302c302c302c2c5063686eb9e620772074ea20b3f3649f206a65bf61206c7562206f9c6d20736b727a79f12066696"
    "72e0d0a");

} // namespace

TEST(LegacyText, Cp1250IsDetectedAndDecoded)
{
    EXPECT_EQ(legacyDetectCharset(kCp1250), "WINDOWS-1250");
    const QString windows = q(decodeLegacyText(kCp1250, P::Windows));
    EXPECT_EQ(windows, u(kDialogue) + u("Zażółć gęślą jaźń, źdźbło trawy.\n") +
                           u("Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,Pchnąć w tę łódź jeża lub ośm skrzyń fig.\n"));
    // The Linux build kept the CRs.
    QString linux = windows;
    linux.replace(QLatin1Char('\n'), QStringLiteral("\r\n"));
    EXPECT_EQ(q(decodeLegacyText(kCp1250, P::Linux)), linux);
}

TEST(LegacyText, Cp1252AndCp1251AsUchardetNamesThem)
{
    // Without bytes 80-9F uchardet says ISO-8859-1 (legacy read it so too).
    const QByteArray latin = QByteArray(kDialogue) + "Voil\xE0 l'\xE9t\xE9, \xE7" "a va tr\xE8s bien \xE0 la fa\xE7" "ade du caf\xE9.\n";
    EXPECT_EQ(legacyDetectCharset(latin), "ISO-8859-1");
    EXPECT_EQ(q(decodeLegacyText(latin, P::Linux)), u(kDialogue) + u("Voilà l'été, ça va très bien à la façade du café.\n"));
    const QByteArray cp1252 = QByteArray::fromHex(
        "4469616c6f6775653a20302c303a30303a30312e30302c303a30303a30322e30302c44656661756c742c2c302c302c302c2c93566f696ce094"
        "206c27e974e9209720e76120636ffb7465203520802c207472e873206269656e20e0206c61206661e761646520647520636166e92e0d0a");
    EXPECT_EQ(legacyDetectCharset(cp1252), "WINDOWS-1252");
    EXPECT_EQ(q(decodeLegacyText(cp1252, P::Windows)),
              u(kDialogue) + u("“Voilà” l'été — ça coûte 5 €, très bien à la façade du café.\n"));
    const QByteArray cp1251 = QByteArray::fromHex(
        "4469616c6f6775653a20302c303a30303a30312e30302c303a30303a30322e30302c44656661756c742c2c302c302c302c2cd1fae5f8fc20e6"
        "e520e5f9b820fdf2e8f520ecffe3eae8f520f4f0e0edf6f3e7f1eae8f520e1f3ebeeea2c20e4e020e2fbefe5e920f7e0fe2e0d0a");
    EXPECT_EQ(legacyDetectCharset(cp1251), "WINDOWS-1251");
    EXPECT_EQ(q(decodeLegacyText(cp1251, P::Windows)),
              u(kDialogue) + u("Съешь же ещё этих мягких французских булок, да выпей чаю.\n"));
}

TEST(LegacyText, Utf8WithAndWithoutItsBom)
{
    // wxConvAuto removes one BOM; text-mode folding is per platform.
    const QByteArray bom = "\xEF\xBB\xBF" "caf\xC3\xA9\r\nx\r\n";
    EXPECT_EQ(q(decodeLegacyText(bom, P::Windows)), u("café\nx\n"));
    EXPECT_EQ(q(decodeLegacyText(bom, P::Linux)), u("café\r\nx\r\n"));
    EXPECT_EQ(q(decodeLegacyText("caf\xC3\xA9\r\n", P::Linux)), u("café\r\n"));
    EXPECT_TRUE(legacyIsUtf8WithoutBom("caf\xC3\xA9"));
    // Pure ASCII is not "UTF-8 without BOM": uchardet names it ASCII.
    EXPECT_FALSE(legacyIsUtf8WithoutBom("plain"));
    EXPECT_EQ(legacyDetectCharset("plain\r\n"), "ASCII");
    EXPECT_EQ(q(decodeLegacyText("plain\r\n", P::Windows)), u("plain\n"));
    EXPECT_EQ(q(decodeLegacyText("plain\r\n", P::Linux)), u("plain\r\n"));
    // A BOM before bytes that are not UTF-8: wxConvAuto fails, FileOpen reads "".
    EXPECT_EQ(decodeLegacyText("\xEF\xBB\xBF" "caf\xE9", P::Windows), std::nullopt);
    // An empty file (or one with only a BOM) is a failed FileOpen.
    EXPECT_EQ(decodeLegacyText({}, P::Windows), std::nullopt);
    EXPECT_EQ(decodeLegacyText("\xEF\xBB\xBF", P::Linux), std::nullopt);
}

TEST(LegacyText, Utf16ByItsBomWithItsCrsOnBothPlatforms)
{
    // F1-utf16-bom: the BOM decides and is removed. Text mode never saw the
    // bytes 0D 0A side by side, so the CRs stay on Windows too.
    const QByteArray le("\xFF\xFE" "c\0a\0\xE9\0\r\0\n\0\x1A\x01", 14);
    EXPECT_EQ(legacyDetectCharset(le), "UTF-16");
    EXPECT_EQ(q(decodeLegacyText(le, P::Windows)), u("caé\r\nĚ"));
    EXPECT_EQ(q(decodeLegacyText(le, P::Linux)), u("caé\r\nĚ"));
    const QByteArray be("\xFE\xFF\0c\0a\0\xE9\0\r\0\n", 12);
    EXPECT_EQ(q(decodeLegacyText(be, P::Windows)), u("caé\r\n"));
}

TEST(LegacyText, ACtrlZIsReadNotTakenAsTheEnd)
{
    // F1-file-ctrlz: legacy's Windows text-mode read stopped at 0x1A.
    EXPECT_EQ(q(decodeLegacyText("caf\xC3\xA9 \x1A rest\r\n", P::Windows)), u("café \x1A rest\n"));
}

TEST(LegacyText, ReadsAFileAsThisPlatformsBuildDid)
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("a.srt"));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(kCp1250);
    file.close();
    const QString text = q(readLegacyTextFile(path));
#ifdef _WIN32
    EXPECT_FALSE(text.contains(QLatin1Char('\r')));
#else
    EXPECT_EQ(text.count(QStringLiteral("\r\n")), 2);
#endif
    EXPECT_TRUE(text.contains(u("Zażółć")));
    EXPECT_EQ(readLegacyTextFile(dir.filePath(QStringLiteral("missing.srt"))), std::nullopt);
}
