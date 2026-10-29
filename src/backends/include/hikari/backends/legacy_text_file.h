#pragma once

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

// Valid UTF-8 with at least one non-ASCII byte.
bool legacyIsUtf8WithoutBom(const QByteArray &bytes);
// uchardet's name for the bytes, upper-cased, or "".
std::string legacyDetectCharset(const QByteArray &bytes);
// A UTF-8 BOM or non-ASCII UTF-8 is UTF-8, else uchardet's charset, else the system code page.
// nullopt when the bytes do not decode or give no text. Windows folds CRLF to LF first, except
// in UTF-16 and UTF-32.
std::optional<std::u16string> decodeLegacyText(const QByteArray &bytes,
                                               LegacyTextPlatform platform = kLegacyTextPlatform);
// nullopt when the file cannot be read or decoded, or is empty.
std::optional<std::u16string> readLegacyTextFile(const QString &path);

} // namespace hikari::backends
