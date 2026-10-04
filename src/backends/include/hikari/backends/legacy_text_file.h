#pragma once

// R4-uchardet / R5-per-platform: legacy OpenWrite::FileOpen(path, &text)
// (test = true), the reader of legacy text files such as Rules.txt.
//
// The bytes are UTF-8 when they start with its BOM or pass IsUTF8withoutBOM
// (which refuses pure ASCII); otherwise CheckCharSet asks uchardet and the
// text is decoded with wxCSConv(name in upper case), or wxConvLocal when
// uchardet names nothing. The file is then read through wxFFile "r": on
// Windows that is a text-mode read (CRLF becomes LF, Ctrl+Z ends the text),
// on Linux the bytes stay as they are ("\r" kept). wxConvAuto drops a UTF-8
// BOM; a BOM of another charset stays in the text as the converter gives it.
// A failed conversion gives an empty text, and an empty text is "not read".

#include <QByteArray>
#include <QByteArrayView>
#include <QString>

#include <optional>

namespace hikari::backends {

// OpenWrite::IsUTF8withoutBOM: well-formed UTF-8 with at least one non-ASCII
// character (pure ASCII and empty input are not UTF-8 to it).
bool legacyIsUtf8WithoutBom(QByteArrayView bytes);

// OpenWrite::CheckCharSet: uchardet's charset name, empty when it names none.
QByteArray legacyDetectCharset(QByteArrayView bytes);

// The decoding half of FileOpen for this platform's legacy build: the text
// FileOpen gives for these file bytes (empty when the conversion fails).
QString legacyFileOpenText(const QByteArray &bytes);

// FileOpen: nullopt when the file cannot be read or its text is empty (legacy
// returns false and the caller falls back, e.g. to the shipped rules).
std::optional<QString> legacyFileOpen(const QString &path);

} // namespace hikari::backends
