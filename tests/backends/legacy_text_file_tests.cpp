// R4-uchardet / R5-per-platform: legacy OpenWrite::FileOpen, the reader of
// Rules.txt (and the other files legacy reads through CheckCharSet).

#include "hikari/backends/legacy_text_file.h"

#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace hikari::backends;

namespace {

// "Zażółć gęślą jaźń. Pchnąć w tę łódź jeża lub ośm skrzyń fig." in cp1250,
// three times, so uchardet is sure of it.
QByteArray polish1250()
{
    const QByteArray sentence = QByteArray("Za\xBF\xF3\xB3\xE6 g\xEA\x9Cl\xB9 ja\x9F\xF1. Pchn\xB9\xE6 w t\xEA \xB3\xF3"
                                           "d\x9F je\xBF"
                                           "a lub o\x9Cm skrzy\xF1 fig.\r\n");
    return sentence + sentence + sentence;
}
QString polishText()
{
    return QString::fromUtf8("Zażółć gęślą jaźń. Pchnąć w tę łódź jeża lub ośm skrzyń fig.");
}

// A French pangram in cp1252 (œ is 0x9C there).
QByteArray french1252()
{
    const QByteArray sentence = QByteArray("Le c\x9C"
                                           "ur d\xE9\xE7u mais l'\xE2"
                                           "me plut\xF4t na\xEFve, Lou\xFFs r\xEA"
                                           "va de crapa\xFC"
                                           "ter en cano\xEB au del\xE0 des \xEE"
                                           "les, pr\xE8s du m\xE4lstr\xF6m o\xF9 br\xFBlent les nov\xE6.\r\n");
    return sentence + sentence;
}
QString frenchText()
{
    return QString::fromUtf8("Le cœur déçu mais l'âme plutôt naïve, Louÿs rêva de crapaüter en canoë au delà des îles, "
                             "près du mälström où brûlent les novæ.");
}

// The line end the running platform's legacy build reads for "\r\n".
QString eol()
{
#ifdef _WIN32
    return QStringLiteral("\n");
#else
    return QStringLiteral("\r\n");
#endif
}

} // namespace

TEST(LegacyTextFile, Utf8WithoutBomIsLegacysCheck)
{
    EXPECT_TRUE(legacyIsUtf8WithoutBom("za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87"));
    EXPECT_TRUE(legacyIsUtf8WithoutBom("\xF0\x9F\x98\x80"));
    // Pure ASCII and empty input are not UTF-8 to it.
    EXPECT_FALSE(legacyIsUtf8WithoutBom("plain ascii\r\n"));
    EXPECT_FALSE(legacyIsUtf8WithoutBom(""));
    // Overlong forms, encoded surrogates, truncated sequences, stray bytes.
    EXPECT_FALSE(legacyIsUtf8WithoutBom("\xC0\xAF"));
    EXPECT_FALSE(legacyIsUtf8WithoutBom("\xE0\x80\xAF"));
    EXPECT_FALSE(legacyIsUtf8WithoutBom("\xED\xA0\x80"));
    EXPECT_FALSE(legacyIsUtf8WithoutBom("ab\xC5"));
    EXPECT_FALSE(legacyIsUtf8WithoutBom("\xE2\x82"));
    EXPECT_FALSE(legacyIsUtf8WithoutBom("caf\xE9"));
}

TEST(LegacyTextFile, CheckCharSetAsksUchardet)
{
    EXPECT_EQ(legacyDetectCharset(polish1250()), QByteArray("WINDOWS-1250"));
    EXPECT_EQ(legacyDetectCharset(french1252()), QByteArray("WINDOWS-1252"));
    EXPECT_EQ(legacyDetectCharset(QByteArray("\xFF\xFE"
                                             "a\0b\0",
                                             6)),
              QByteArray("UTF-16"));
    EXPECT_EQ(legacyDetectCharset("plain ascii"), QByteArray("ASCII"));
    EXPECT_EQ(legacyDetectCharset(""), QByteArray());
}

TEST(LegacyTextFile, DetectedCharsetsDecode)
{
    // cp1250 and cp1252 files read in their own charset, not the system code page.
    EXPECT_EQ(legacyFileOpenText(polish1250()), (polishText() + eol()).repeated(3));
    EXPECT_EQ(legacyFileOpenText(french1252()), (frenchText() + eol()).repeated(2));
    // Pure ASCII goes through uchardet's "ASCII" and reads unchanged.
    EXPECT_EQ(legacyFileOpenText("a\r\nb"), QStringLiteral("a") + eol() + QStringLiteral("b"));
}

TEST(LegacyTextFile, Utf8AndLineEndsPerPlatform)
{
    // UTF-8 with its BOM: wxConvAuto drops the BOM. Windows' text-mode read
    // folds CRLF to LF (a lone CR stays); Linux keeps every byte.
    const QByteArray bom = "\xEF\xBB\xBF#HikariSub rules file\r\n1|0\r\nb\rc\r\n";
#ifdef _WIN32
    EXPECT_EQ(legacyFileOpenText(bom), QStringLiteral("#HikariSub rules file\n1|0\nb\rc\n"));
#else
    EXPECT_EQ(legacyFileOpenText(bom), QStringLiteral("#HikariSub rules file\r\n1|0\r\nb\rc\r\n"));
#endif
    // UTF-8 without a BOM, as IsUTF8withoutBOM accepts it.
    EXPECT_EQ(legacyFileOpenText("Polish \xC5\x82\r\n"), QString::fromUtf8("Polish ł") + eol());
    // A BOM followed by invalid UTF-8: wxConvAuto's strict UTF-8 fails, the
    // text is empty and FileOpen reports nothing read.
    EXPECT_TRUE(legacyFileOpenText("\xEF\xBB\xBF"
                                   "caf\xE9")
                    .isEmpty());
    // Ctrl+Z ends a Windows text-mode read; Linux reads past it.
#ifdef _WIN32
    EXPECT_EQ(legacyFileOpenText("a\x1A"
                                 "b"),
              QStringLiteral("a"));
#else
    EXPECT_EQ(legacyFileOpenText("a\x1A"
                                 "b"),
              QStringLiteral("a\x1A"
                             "b"));
#endif
}

TEST(LegacyTextFile, Utf16WithBomPerPlatform)
{
    // uchardet names a BOM'd file "UTF-16". Linux: iconv takes the byte order
    // from the BOM and drops it. Windows: wx maps "UTF-16" to its UTF-16LE
    // converter, which keeps the BOM as U+FEFF (and the "\r", which is not
    // next to a "\n" byte, survives the text-mode read).
    const QByteArray le("\xFF\xFE"
                        "a\0\xF3\0\r\0\n\0",
                        10);
#ifdef _WIN32
    EXPECT_EQ(legacyFileOpenText(le), QChar(0xFEFF) + QString::fromUtf8("aó\r\n"));
#else
    EXPECT_EQ(legacyFileOpenText(le), QString::fromUtf8("aó\r\n"));
    const QByteArray be("\xFE\xFF\0a\0\xF3", 6);
    EXPECT_EQ(legacyFileOpenText(be), QString::fromUtf8("aó"));
#endif
}

TEST(LegacyTextFile, FileOpenReportsUnreadAndEmptyFiles)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    EXPECT_FALSE(legacyFileOpen(dir.filePath(QStringLiteral("missing.txt"))).has_value());
    const auto write = [&](const char *name, const QByteArray &bytes) {
        QFile file(dir.filePath(QString::fromLatin1(name)));
        EXPECT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(bytes);
        return file.fileName();
    };
    EXPECT_FALSE(legacyFileOpen(write("empty.txt", {})).has_value());
    EXPECT_FALSE(legacyFileOpen(write("bom.txt", "\xEF\xBB\xBF")).has_value());
    const auto polish = legacyFileOpen(write("polish.txt", polish1250()));
    ASSERT_TRUE(polish.has_value());
    EXPECT_TRUE(polish->startsWith(polishText()));
}
