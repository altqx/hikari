// R4-uchardet / R5-per-platform: legacy OpenWrite::FileOpen(path, &text,
// true) at 20d647c4 (OpennWrite.cpp), as find and replace in files reads.

#include "hikari/backends/legacy_text_file.h"

#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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
    // F1-utf16-bom: the BOM decides and is removed. UTF-16 and UTF-32 are
    // never folded, so the CRs stay on Windows too.
    const QByteArray le("\xFF\xFE" "c\0a\0\xE9\0\r\0\n\0\x1A\x01", 14);
    EXPECT_EQ(legacyDetectCharset(le), "UTF-16");
    EXPECT_EQ(q(decodeLegacyText(le, P::Windows)), u("caé\r\nĚ"));
    EXPECT_EQ(q(decodeLegacyText(le, P::Linux)), u("caé\r\nĚ"));
    const QByteArray be("\xFE\xFF\0c\0a\0\xE9\0\r\0\n", 12);
    EXPECT_EQ(q(decodeLegacyText(be, P::Windows)), u("caé\r\n"));
}

TEST(LegacyText, Utf16BytesThatLookLikeCrLfAreNotFolded)
{
    // F1-utf16-crlf: U+0D0A (Malayalam UU) is the bytes 0D 0A in UTF-16BE, and
    // U+0D15 U+0D0A is 15 0D 0A 0D in UTF-16LE. Legacy Windows' text-mode
    // read deleted that 0D and shifted every later byte; here nothing is
    // folded in UTF-16, so the text survives on both platforms.
    const QByteArray be("\xFE\xFF\x0D\x0A\0x\0\r\0\n", 10);
    EXPECT_EQ(legacyDetectCharset(be), "UTF-16");
    EXPECT_EQ(q(decodeLegacyText(be, P::Windows)), u("\u0D0Ax\r\n"));
    EXPECT_EQ(q(decodeLegacyText(be, P::Linux)), u("\u0D0Ax\r\n"));
    const QByteArray le("\xFF\xFE\x15\x0D\x0A\x0D" "y\0", 8);
    EXPECT_EQ(q(decodeLegacyText(le, P::Windows)), u("\u0D15\u0D0Ay"));
}

namespace {

QByteArray hex(const char *digits)
{
    return QByteArray::fromHex(digits);
}

// The same file with Windows' text-mode read (CRLF to LF).
QString folded(QString text)
{
    return text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
}

// "{\an8}" and Korean (EUC-KR text ending in the CP949-only "똠방각하").
const QByteArray kUhc = hex("4469616c6f6775653a20302c303a30303a30312e30302c303a30303a30322e30302c44656661756c742c2c302c302c302c2c"
                            "7b5c616e387dbec8b3e7c7cfbcbcbfe420bfa9b7afbad02c20bfc0b4c320b3afbebeb0a120c1a4b8bb20c1c1b3d7bfe4"
                            "2e20bfecb8aeb4c220c7d1b1b9beee20c0dab8b7c0bb20b8b8b5e9b0ed20c0d6bdc0b4cfb4d92e208c63b9e6b0a2c7cf"
                            "0d0a");
// Romanian in ISO-8859-16 (ș and ț with the comma below).
const QByteArray kRomanian = hex("4469616c6f6775653a20302c303a30303a30312e30302c303a30303a30322e30302c44656661756c742c2c302c302c302c"
                                 "2caa7469696efe6120ba6920fe61726120726f6de26e65617363e32c20ee6e20616365617374e32064696d696e6561fe"
                                 "e320ee6e736f726974e32c2070e373747265617ae3207472616469fe69696c6520737472e362756e696c6f722e0d0a");
// "{\an8}" and Japanese in Shift_JIS ending in '~'; 表 is 95 5C, a trail
// byte that is 0x5C.
const QByteArray kShiftJis = hex("4469616c6f6775653a20302c303a30303a30312e30302c303a30303a30322e30302c44656661756c742c2c302c302c302c"
                                 "2c7b5c616e387d82b182f182c982bf82cd814190a28a4581428da193fa82cd82a282a293568b4382c582b782cb81428e"
                                 "9a968b82f0955c8ea682b582c482a282dc82b77e0d0a");
// Japanese in ISO-2022-JP, back in ASCII (ESC ( B) before the line end.
const QByteArray kIso2022Jp = hex("4469616c6f6775653a20302c303a30303a30312e30302c303a30303a30322e30302c44656661756c742c2c302c302c30"
                                  "2c2c1b244224332473244b2441244f21224024332621233a23467c244f242424244537352424472439244d21231b2842"
                                  "0a");

} // namespace

TEST(LegacyText, KoreanAndRomanianAsEachPlatformsWxCSConvReadThem)
{
    // uchardet calls EUC-KR and CP949 text "UHC" and Romanian "ISO-8859-16".
    EXPECT_EQ(legacyDetectCharset(kUhc), "UHC");
    EXPECT_EQ(legacyDetectCharset(kRomanian), "ISO-8859-16");
    const QString korean = u(kDialogue) + u("{\\an8}안녕하세요 여러분, 오늘 날씨가 정말 좋네요. 우리는 한국어 자막을 만들고 "
                                             "있습니다. 똠방각하\r\n");
    const QString romanian =
        u(kDialogue) + u("Știința și țara românească, în această dimineață însorită, păstrează tradițiile străbunilor.\r\n");
#ifdef _WIN32
    // wx knows neither name, so legacy read both as Latin-1 (F1-win-charsets:
    // ICU decodes UHC; it has no ISO-8859-16 converter, so that file is
    // skipped rather than read as mojibake).
    EXPECT_EQ(q(decodeLegacyText(kUhc, P::Windows)), folded(korean));
    EXPECT_EQ(decodeLegacyText(kRomanian, P::Windows), std::nullopt);
#else
    // glibc iconv opens both (Qt's ICU opened neither).
    EXPECT_EQ(q(decodeLegacyText(kUhc, P::Linux)), korean);
    EXPECT_EQ(q(decodeLegacyText(kRomanian, P::Linux)), romanian);
    // EUC-KR text is UHC too, and a sequence cut off at the end fails
    // (EINVAL), as wxMBConv_iconv failed.
    EXPECT_EQ(q(decodeLegacyText(kUhc.left(kUhc.size() - 2) + "\xB0", P::Linux)), u("<nullopt>"));
#endif
}

TEST(LegacyText, ShiftJisKeepsItsBackslashesAndTildes)
{
    // F1-sjis-backslash: glibc's SHIFT_JIS reads 0x5C as U+00A5 and 0x7E as
    // U+203E, so legacy Linux read "{\an8}" as "{¥an8}" and a file replace
    // wrote that back. The ASCII characters are kept; the 5C trail byte of
    // 表 is still part of 表. Windows' code page 932 kept them already.
    EXPECT_EQ(legacyDetectCharset(kShiftJis), "SHIFT_JIS");
    const QString text = u(kDialogue) + u("{\\an8}こんにちは、世界。今日はいい天気ですね。字幕を表示しています~\r\n");
#ifdef _WIN32
    EXPECT_EQ(q(decodeLegacyText(kShiftJis, P::Windows)), folded(text));
#else
    EXPECT_EQ(q(decodeLegacyText(kShiftJis, P::Linux)), text);
    EXPECT_FALSE(q(decodeLegacyText(kShiftJis, P::Linux)).contains(QChar(0x00A5)));
#endif
}

TEST(LegacyText, Iso2022JpAsEachPlatformReadIt)
{
    EXPECT_EQ(legacyDetectCharset(kIso2022Jp), "ISO-2022-JP");
#ifdef _WIN32
    // wx maps the name to code page 50222. wxEncodingToCodepage keeps it only
    // when GetCPInfo accepts it, and MultiByteToWideChar then refuses
    // MB_ERR_INVALID_CHARS for it: the read fails. Otherwise wx had no
    // converter (no wxEncodingConverter table).
    CPINFO info;
    if (::IsValidCodePage(50222) && ::GetCPInfo(50222, &info))
        EXPECT_EQ(decodeLegacyText(kIso2022Jp, P::Windows), std::nullopt);
    else // F1-win-charsets: ICU's ISO-2022-JP, where legacy read Latin-1
        EXPECT_EQ(q(decodeLegacyText(kIso2022Jp, P::Windows)), folded(u(kDialogue) + u("こんにちは、世界。今日はいい天気ですね。\n")));
#else
    EXPECT_EQ(q(decodeLegacyText(kIso2022Jp, P::Linux)),
              u(kDialogue) + u("こんにちは、世界。今日はいい天気ですね。\n"));
    // wxString's conversion ran iconv twice on one handle without a reset: a
    // file left in its JIS X 0208 shift at the end starts the second pass
    // mid-shift and fails, so legacy read nothing.
    EXPECT_EQ(decodeLegacyText(kIso2022Jp.left(kIso2022Jp.size() - 4), P::Linux), std::nullopt);
#endif
}

TEST(LegacyText, Utf8ThatUchardetNamesIsStrictOnBothPlatforms)
{
    // Polish UTF-8 cut off in its last character: IsUTF8withoutBOM refuses
    // it, uchardet still says UTF-8, and wxCSConv("UTF-8") failed on the bad
    // end (code page 65001 with MB_ERR_INVALID_CHARS on Windows, iconv's
    // EINVAL on Linux), so FileOpen read nothing.
    const QByteArray cut = QByteArray(kDialogue) + "Za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87 g\xC4\x99\xC5\x9Bl\xC4\x85 "
                                                   "ja\xC5\xBA\xC5\x84, \xC5\xBA" "d\xC5\xBA" "b\xC5\x82o trawy. Pchn\xC4\x85\xC4\x87 w "
                                                   "t\xC4\x99 \xC5\x82\xC3\xB3" "d\xC5\xBA je\xC5\xBC" "a lub o\xC5\x9Bm "
                                                   "skrzy\xC5\x84 fig.\r\n\xC5\xBC\xC3";
    EXPECT_FALSE(legacyIsUtf8WithoutBom(cut));
    EXPECT_EQ(legacyDetectCharset(cut), "UTF-8");
    EXPECT_EQ(decodeLegacyText(cut, P::Windows), std::nullopt);
    EXPECT_EQ(decodeLegacyText(cut, P::Linux), std::nullopt);
    // Whole, it is UTF-8 without a BOM and read as such.
    EXPECT_EQ(q(decodeLegacyText(cut + "\xB3", P::Linux)).right(4), u("fig.\r\nżó").right(4));
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
