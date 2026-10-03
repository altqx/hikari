#pragma once

// F3: the Qt side of the spell checker's dictionary list. (Word
// segmentation and case functions are the backends' legacySpellingText.)

#include <QString>

namespace hikari::ui {

// config::FindLanguage: the name a dictionary symbol is listed under. The
// shipped catalogue names first (by symbol, then by its part before '_'),
// else the locale's own name for its language, else the symbol.
QString dictionaryName(const QString &symbol);

} // namespace hikari::ui
