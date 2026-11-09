#pragma once

#include "hikari/application/automation.h"
#include "hikari/application/automation_services.h"

extern "C" {
#include <lua.h>
}

#include <optional>
#include <string>
#include <vector>

namespace hikari::lua_helper {

// The running macro's staged lists; script indices run 1-based across info, styles and dialogues.
const application::MacroSnapshot &staged();

// The dialogue line table on the stack top as subs.append would store it; std::nullopt for another class.
std::optional<application::MacroDialogueLine> dialogueOf(lua_State *L);

// A Style's fields as aegisub.text_extents sends them: read into a style table and written back.
std::vector<std::string> canonicalStyle(const std::vector<std::string> &fields);

// aegisub.text_extents for canonical fields; std::nullopt when the host has no answer.
std::optional<application::TextExtents> textExtents(lua_State *L, const std::vector<std::string> &style,
                                                    const std::string &text);

// aegisub.parse_karaoke_data on the line table at `line`: its syllables from index 0, read from the text, or from
// the translation when the text is empty.
void fillKaraokeData(lua_State *L, int line, const application::MacroDialogueLine &dialogue);

// The dialogue line with the text parse_karaoke_data reads in its text.
application::MacroDialogueLine karaokeSource(application::MacroDialogueLine dialogue);

// Adds a macro whose run and validate functions are C functions, as aegisub.register_macro would.
void registerMacro(lua_State *L, const std::string &name, const std::string &description, lua_CFunction run,
                   lua_CFunction validate);

}
