#pragma once

#include "hikari/application/automation.h"
#include "hikari/application/automation_services.h"

extern "C" {
#include <lua.h>
}

#include <functional>
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

// Runs `call`; a host that could not answer raises aegisub.<name>'s error, as the call from Lua would.
void raiseOnHostFailure(lua_State *L, const char *name, const std::function<void()> &call);

// aegisub.parse_karaoke_data on the line table at `line`: its syllables from index 0, read from the text, or from
// the translation when the text is empty.
void fillKaraokeData(lua_State *L, int line, const application::MacroDialogueLine &dialogue);

// The dialogue line with the text parse_karaoke_data reads in its text.
application::MacroDialogueLine karaokeSource(application::MacroDialogueLine dialogue);

enum class HostFunction { TextExtents, ParseKaraokeData, DebugOut };
// Whether the value at `index` is still the host's own function, not one a script put in its place.
bool isHost(lua_State *L, int index, HostFunction function);
// Whether the value at `index` is the running macro's subtitles object.
bool isSubtitles(lua_State *L, int index);
// The level above which aegisub.debug.out writes nothing.
int traceLevel();
// The style table on the stack top as aegisub.text_extents sends it; std::nullopt for another class. Raises as
// text_extents does for a table it cannot read.
std::optional<std::vector<std::string>> styleFields(lua_State *L);
// Adds a macro whose run and validate functions are C functions, as aegisub.register_macro would.
void registerMacro(lua_State *L, const std::string &name, const std::string &description, lua_CFunction run,
                   lua_CFunction validate);

}
