#pragma once

// F3: the Qt side of the spell checker's text walks. Legacy segments words
// with boost::locale over ICU (word_letters / word_number rules) and maps
// case one UTF-16 unit at a time with the C runtime; here Qt's Unicode word
// boundaries (QTextBoundaryFinder, as the Grid's measures, G7) and QChar.

#include "hikari/application/spell_checker.h"

#include <QString>

namespace hikari::ui {

// A segment holding a letter is a letters segment (letters, kana and
// ideographs are word_letters in ICU); one holding only digits is a number.
application::SpellingText qtSpellingText();

// config::FindLanguage: the name a dictionary symbol is listed under. The
// shipped catalogue names first (by symbol, then by its part before '_'),
// else the locale's own name for its language, else the symbol.
QString dictionaryName(const QString &symbol);

} // namespace hikari::ui
