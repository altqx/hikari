#pragma once

#include "hikari/application/spell_checker.h"

#include <string>
#include <string_view>

namespace hikari::backends {

// Hunspell over a .aff/.dic pair. Words go in the dictionary's SET encoding; a word it cannot
// hold is misspelled, with no suggestions.
application::SpellingBackendLoader hunspellSpellingLoader();

// One UTF-16 unit at a time: every letter maps with `everyLetter`, else A-Z only, as glibc's
// towlower, towupper and iswupper do in the "C" locale.
core::legacy::CaseMapping legacyCaseMapping(bool everyLetter);

// Words by boost::locale's ICU word boundaries (letters and numbers) in the system locale, cased
// by legacyCaseMapping(everyLetter).
application::SpellingText legacySpellingText(bool everyLetter = false);

// For Hunspell on Windows: C:\a becomes \\?\C:\a, \\server\share\a becomes \\?\UNC\server\share\a,
// and \\?\ or \\.\ paths stay. Takes an absolute, normalized path with backslashes.
std::string windowsLongPath(std::string_view path);

}
