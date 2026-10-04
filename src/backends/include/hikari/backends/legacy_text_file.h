#pragma once

// Legacy OpenWrite::FileOpen(path, &text, test = true) at 20d647c4: the text
// files the legacy app read with CheckCharSet (R4-uchardet), per platform
// (R5-per-platform). Find and replace in files reads through it; other legacy
// text readers (Rules.txt and the like) can too.
//
// The charset: a UTF-8 BOM or valid non-ASCII UTF-8 is UTF-8; otherwise
// uchardet names it, and with no name the system code page (wxConvLocal)
// decodes. Bytes the charset cannot decode give nullopt (wxCSConv failed and
// FileOpen read ""), as does an empty file.
//
// Line ends: the Windows build read in text mode, so CRLF became LF before
// decoding; the Linux build kept every '\r'. A UTF-16 or UTF-32 file never has
// the bytes 0D 0A side by side, so its '\r's stay on both.
//
// Departures (R3-hang-crash-loss, named in F1's coverage row): legacy's
// Windows text-mode read stopped at the first Ctrl+Z byte (0x1A, which UTF-16
// text such as U+011A has), so a file written back lost the rest
// (F1-file-ctrlz); here the whole file is read. Legacy Windows read
// uchardet's "UTF-16" with wx's native little-endian converter, keeping the
// BOM as U+FEFF (written back as a second BOM) and byte-swapping a big-endian
// file; the BOM is read and removed here, as the Linux build (iconv) did
// (F1-utf16-bom).

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
// FileOpen's decoding of a whole file's bytes as `platform` read them. The
// decoder for a named charset is the running system's (Win32 code pages on
// Windows as wxCSConv used, ICU through Qt elsewhere as iconv did).
std::optional<std::u16string> decodeLegacyText(const QByteArray &bytes,
                                               LegacyTextPlatform platform = kLegacyTextPlatform);
// FileOpen on a file: nullopt when it cannot be read, decoded or is empty.
std::optional<std::u16string> readLegacyTextFile(const QString &path);

} // namespace hikari::backends
