#pragma once

// Legacy OpenWrite::FileOpen(path, &text, test = true) at 20d647c4: the text
// files the legacy app read with CheckCharSet (R4-uchardet), per platform
// (R5-per-platform). Find and replace in files reads through it; other legacy
// text readers (Rules.txt and the like) can too.
//
// The charset: a UTF-8 BOM or valid non-ASCII UTF-8 is UTF-8; otherwise
// uchardet names it, and with no name the system code page (wxConvLocal)
// decodes. A named charset is decoded as wxCSConv did on each platform: Win32
// code pages on Windows, glibc iconv elsewhere (ASCII and ISO-8859-1 as
// Latin-1 on both; a name with no converter as Latin-1 on Linux, through ICU
// on Windows, F1-win-charsets). Bytes the charset
// cannot decode give nullopt (wxCSConv failed and FileOpen read ""), as does
// an empty file.
//
// Line ends: the Windows build read in text mode, so CRLF became LF before
// decoding; the Linux build kept every '\r'. Both are reproduced for the
// byte-oriented charsets.
//
// Departures (R3-hang-crash-loss unless noted, named in F1's coverage row):
// - F1-file-ctrlz: legacy's Windows text-mode read stopped at the first
//   Ctrl+Z byte (0x1A, which UTF-16 text such as U+011A has), so a file
//   written back lost the rest; here the whole file is read.
// - F1-utf16-crlf: in UTF-16 and UTF-32 the bytes 0D 0A can be one character
//   or straddle two (U+0D0A is 0D 0A in UTF-16BE; U+0D15 U+0D0A is 15 0D 0A 0D
//   in UTF-16LE). Legacy Windows deleted that 0D and shifted every later byte,
//   garbling the rest of the file; here these charsets are never folded.
// - F1-sjis-backslash: glibc's SHIFT_JIS reads the ASCII bytes 0x5C and 0x7E
//   as U+00A5 and U+203E, so legacy Linux turned every "\" of a tag into a
//   yen sign and a file replace wrote that back; here '\' and '~' are kept
//   for any charset whose iconv does so.
// - F1-utf16-bom (its own approved row): legacy Windows read uchardet's
//   "UTF-16" with wx's native little-endian converter, keeping the BOM as
//   U+FEFF and byte-swapping a big-endian file; legacy Linux's iconv, used
//   twice on one handle by wxString's conversion, failed on any UTF-16 or
//   UTF-32 file with a BOM. Here the BOM decides and is removed on both.
// - F1-win-charsets (its own approved row): legacy Windows read a charset wx
//   had no converter for (UHC, TIS-620, IBM8xx, MAC-*, GB18030, HZ, EUC-TW,
//   ISO-2022-CN/KR, ...) as Latin-1, and a file replace wrote that mojibake
//   back; here ICU decodes it, and a charset ICU lacks (ISO-8859-16, VISCII,
//   GEORGIAN-*) or bytes it cannot decode read as nothing.

#include <QByteArray>
#include <QString>

#include <optional>
#include <string>

namespace hikari::backends {

enum class LegacyTextPlatform { Windows, Linux };

#ifdef _WIN32
inline constexpr LegacyTextPlatform kLegacyTextPlatform = LegacyTextPlatform::Windows;
#else
inline constexpr LegacyTextPlatform kLegacyTextPlatform = LegacyTextPlatform::Linux;
#endif

// OpenWrite::IsUTF8withoutBOM: valid UTF-8 with at least one non-ASCII byte.
bool legacyIsUtf8WithoutBom(const QByteArray &bytes);
// OpenWrite::CheckCharSet: uchardet's name for the bytes, or "".
std::string legacyDetectCharset(const QByteArray &bytes);
// FileOpen's decoding of a whole file's bytes with `platform`'s line ends. The
// decoder for a named charset is the running system's (Win32 code pages on
// Windows, glibc iconv elsewhere, as wxCSConv used them).
std::optional<std::u16string> decodeLegacyText(const QByteArray &bytes,
                                               LegacyTextPlatform platform = kLegacyTextPlatform);
// FileOpen on a file: nullopt when it cannot be read, decoded or is empty.
std::optional<std::u16string> readLegacyTextFile(const QString &path);

} // namespace hikari::backends
