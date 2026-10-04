#pragma once

// F3 / R2-hunspell: the spell checker's production adapters. Legacy
// SpellChecker (20d647c4) loads Hunspell from a <language>.aff/.dic pair and
// splits words with boost::locale's word boundaries over ICU; both are here,
// behind application::SpellingBackend and SpellingText.

#include "hikari/application/spell_checker.h"

#include <string>
#include <string_view>

namespace hikari::backends {

// Hunspell 1.7.3 over a .aff/.dic pair. Words are converted to the
// dictionary's encoding (its SET line) as legacy's wxCSConv does; a word
// that encoding cannot hold is misspelled and has no suggestions.
application::SpellingBackendLoader hunspellSpellingLoader();

// boost::locale boundary::word segmentation (ICU backend, the system locale
// as legacy's HikariSubFrame makes global) with word_letters / word_number
// rules, on UTF-16 text; and per-UTF-16-unit case functions.
application::SpellingText legacySpellingText();

// The Windows long-path form of an absolute, normalized path with
// backslashes, as Hunspell gets it there: C:\a becomes \\?\C:\a, a UNC
// location \\server\share\a becomes \\?\UNC\server\share\a, and a path
// already in a \\?\ or \\.\ form is kept.
std::string windowsLongPath(std::string_view path);

} // namespace hikari::backends
