#include "lua_native.h"

#include <boost/locale.hpp>
#include <boost/regex/icu.hpp>

extern "C" {
#include <lauxlib.h>
#include <lualib.h>
int luaopen_lpeg(lua_State *L);    // HikariSub/AutomationLPeg.c
int luaopen_luabins(lua_State *L); // Thirdparty/luabins
}

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <locale>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace hikari::lua_native {

namespace {

namespace fs = std::filesystem;

char *dup(const std::string &s)
{
    auto *out = static_cast<char *>(std::malloc(s.size() + 1));
    if (!out)
        return nullptr;
    std::memcpy(out, s.data(), s.size());
    out[s.size()] = 0;
    return out;
}

// Legacy do_register_lib_table: leaves ffi.cast on the stack after declaring
// the opaque types.
void beginLibTable(lua_State *L, const std::vector<const char *> &types)
{
    lua_getglobal(L, "require");
    lua_pushstring(L, "ffi");
    lua_call(L, 1, 1);
    for (const char *type : types) {
        lua_getfield(L, -1, "cdef");
        lua_pushfstring(L, "typedef struct %s %s;", type, type);
        lua_call(L, 1, 0);
    }
    lua_getfield(L, -1, "cast");
    lua_remove(L, -2);
}

// Legacy do_register_lib_function: table[name] = ffi.cast(type, fn).
void addFunction(lua_State *L, const char *name, const char *type, void *fn)
{
    lua_pushvalue(L, -2);
    lua_pushstring(L, type);
    lua_pushlightuserdata(L, fn);
    lua_call(L, 2, 1);
    lua_setfield(L, -2, name);
}

template <typename F> void *fnptr(F *f)
{
    return reinterpret_cast<void *>(f);
}

// ---- aegisub.__unicode_impl -------------------------------------------------

char *toUpper(const char *str, char **err)
{
    try {
        return dup(boost::locale::to_upper(str));
    } catch (const std::exception &e) {
        *err = dup(e.what());
        return nullptr;
    }
}

char *toLower(const char *str, char **err)
{
    try {
        return dup(boost::locale::to_lower(str));
    } catch (const std::exception &e) {
        *err = dup(e.what());
        return nullptr;
    }
}

char *foldCase(const char *str, char **err)
{
    try {
        return dup(boost::locale::fold_case(str));
    } catch (const std::exception &e) {
        *err = dup(e.what());
        return nullptr;
    }
}

int openUnicode(lua_State *L)
{
    beginLibTable(L, {});
    lua_createtable(L, 0, 3);
    addFunction(L, "to_upper_case", "char * (*)(const char *, char **)", fnptr(toUpper));
    addFunction(L, "to_lower_case", "char * (*)(const char *, char **)", fnptr(toLower));
    addFunction(L, "to_fold_case", "char * (*)(const char *, char **)", fnptr(foldCase));
    lua_remove(L, -2);
    return 1;
}

// ---- aegisub.__re_impl (legacy, unchanged semantics) -------------------------

using boost::u32regex;

// A cmatch with its range attached, so a pointer to an int pair can be
// returned without a heap allocation per call.
struct agi_re_match {
    boost::cmatch m;
    int range[2];
};

struct agi_re_flag {
    const char *name;
    int value;
};

bool search(u32regex &re, const char *str, size_t len, int start, boost::cmatch &result)
{
    return u32regex_search(str + start, str + len, result, re,
                           start > 0 ? boost::match_prev_avail | boost::match_not_bob : boost::match_default);
}

agi_re_match *regexMatch(u32regex &re, const char *str, size_t len, int start)
{
    std::unique_ptr<agi_re_match> result(new agi_re_match);
    if (!search(re, str, len, start, result->m))
        return nullptr;
    return result.release();
}

int *regexGetMatch(agi_re_match &match, size_t idx)
{
    if (idx > match.m.size() || !match.m[static_cast<int>(idx)].matched)
        return nullptr;
    match.range[0] = static_cast<int>(std::distance(match.m.prefix().first, match.m[static_cast<int>(idx)].first + 1));
    match.range[1] = static_cast<int>(std::distance(match.m.prefix().first, match.m[static_cast<int>(idx)].second));
    return match.range;
}

int *regexSearch(u32regex &re, const char *str, size_t len, size_t start)
{
    boost::cmatch result;
    if (!search(re, str, len, static_cast<int>(start), result))
        return nullptr;
    auto *ret = static_cast<int *>(std::malloc(sizeof(int) * 2));
    ret[0] = static_cast<int>(start + result.position() + 1);
    ret[1] = static_cast<int>(start + result.position() + result.length());
    return ret;
}

char *regexReplace(u32regex &re, const char *replacement, const char *str, size_t len, int maxCount)
{
    // regex_replace does one or every replacement; this does up to maxCount.
    auto match = boost::u32regex_iterator<const char *>(str, str + len, re);
    auto endIt = boost::u32regex_iterator<const char *>();
    auto suffix = str;
    std::string ret;
    auto out = std::back_inserter(ret);
    while (match != endIt && maxCount > 0) {
        std::copy(suffix, match->prefix().second, out);
        match->format(out, replacement);
        suffix = match->suffix().first;
        ++match;
        --maxCount;
    }
    ret += suffix;
    return dup(ret);
}

u32regex *regexCompile(const char *pattern, int flags, char **err)
{
    std::unique_ptr<u32regex> re(new u32regex);
    try {
        *re = boost::make_u32regex(pattern, boost::regex::perl | flags);
        return re.release();
    } catch (const std::exception &e) {
        *err = dup(e.what());
        return nullptr;
    }
}

void regexFree(u32regex *re)
{
    delete re;
}

void matchFree(agi_re_match *m)
{
    delete m;
}

const agi_re_flag *regexFlags()
{
    static const agi_re_flag flags[] = {{"ICASE", u32regex::icase},
                                        {"NOSUB", u32regex::nosubs},
                                        {"COLLATE", u32regex::collate},
                                        {"NEWLINE_ALT", u32regex::newline_alt},
                                        {"NO_MOD_M", u32regex::no_mod_m},
                                        {"NO_MOD_S", u32regex::no_mod_s},
                                        {"MOD_S", u32regex::mod_s},
                                        {"MOD_X", u32regex::mod_x},
                                        {"NO_EMPTY_SUBEXPRESSIONS", u32regex::no_empty_expressions},
                                        {nullptr, 0}};
    return flags;
}

int openRe(lua_State *L)
{
    beginLibTable(L, {"agi_re_match", "u32regex"});
    lua_createtable(L, 0, 8);
    addFunction(L, "search", "int * (*)(u32regex&, const char *, size_t, size_t)", fnptr(regexSearch));
    addFunction(L, "match", "agi_re_match * (*)(u32regex&, const char *, size_t, int)", fnptr(regexMatch));
    addFunction(L, "get_match", "int * (*)(agi_re_match&, size_t)", fnptr(regexGetMatch));
    addFunction(L, "replace", "char * (*)(u32regex&, const char *, const char *, size_t, int)", fnptr(regexReplace));
    addFunction(L, "compile", "u32regex * (*)(const char *, int, char **)", fnptr(regexCompile));
    addFunction(L, "get_flags", "const agi_re_flag * (*)()", fnptr(regexFlags));
    addFunction(L, "match_free", "void (*)(agi_re_match *)", fnptr(matchFree));
    addFunction(L, "regex_free", "void (*)(u32regex *)", fnptr(regexFree));
    lua_remove(L, -2);
    return 1;
}

// ---- aegisub.__lfs_impl ------------------------------------------------------
// Legacy used boost::filesystem, whose char paths are in the ANSI code page
// on Windows; std::filesystem's char paths behave the same way there, and
// are UTF-8 elsewhere.

// Legacy wrap(): a std::exception's text, anything else "Unknown error".
template <typename Func> auto wrap(char **err, Func f) -> decltype(f())
{
    try {
        return f();
    } catch (const std::exception &e) {
        *err = dup(e.what());
    } catch (...) {
        *err = dup("Unknown error");
    }
    return {};
}

struct DirectoryIterator {
    fs::directory_iterator it, end;
    bool open = false;
};

bool lfsChdir(const char *dir, char **err)
{
    return wrap(err, [=] {
        fs::current_path(dir);
        return true;
    });
}

char *currentDir(char **err)
{
    return wrap(err, [] { return dup(fs::current_path().string()); });
}

bool lfsMkdir(const char *dir, char **err)
{
    return wrap(err, [=] {
        fs::create_directories(dir);
        return true;
    });
}

bool lfsRmdir(const char *dir, char **err)
{
    return wrap(err, [=] {
        fs::remove(dir);
        return true;
    });
}

bool lfsTouch(const char *path, char **err)
{
    return wrap(err, [=] {
        const fs::path p(path);
        if (p.has_parent_path())
            fs::create_directories(p.parent_path());
        if (!fs::exists(p))
            std::ofstream(p, std::ios::app);
        fs::last_write_time(p, fs::file_time_type::clock::now());
        return true;
    });
}

DirectoryIterator *dirNew(const char *path, char **err)
{
    return wrap(err, [=] {
        auto *d = new DirectoryIterator;
        std::error_code ec;
        d->it = fs::directory_iterator(path, ec);
        d->open = !ec; // legacy: an unreadable directory is simply empty
        return d;
    });
}

char *dirNext(DirectoryIterator &d, char **err)
{
    if (!d.open || d.it == d.end)
        return nullptr;
    return wrap(err, [&] {
        char *name = dup(d.it->path().filename().string());
        ++d.it;
        return name;
    });
}

void dirClose(DirectoryIterator &d)
{
    d.it = fs::directory_iterator();
    d.open = false;
}

void dirFree(DirectoryIterator *d)
{
    delete d;
}

char *getMode(const char *path, char **err)
{
    return wrap(err, [=]() -> char * {
        switch (fs::status(path).type()) {
        case fs::file_type::not_found: return nullptr;
        case fs::file_type::regular: return dup("file");
        case fs::file_type::directory: return dup("directory");
        case fs::file_type::symlink: return dup("link");
        case fs::file_type::block: return dup("block device");
        case fs::file_type::character: return dup("char device");
        case fs::file_type::fifo: return dup("fifo");
        case fs::file_type::socket: return dup("socket");
        default: return dup("other");
        }
    });
}

long long getMtime(const char *path, char **err)
{
    return wrap(err, [=] {
        const auto t = std::chrono::clock_cast<std::chrono::system_clock>(fs::last_write_time(path));
        return static_cast<long long>(std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count());
    });
}

unsigned long long getSize(const char *path, char **err)
{
    return wrap(err, [=] {
        if (fs::is_directory(path))
            throw "Not a file"; // legacy: not a std::exception, so "Unknown error"
        return static_cast<unsigned long long>(fs::file_size(path));
    });
}

int openLfs(lua_State *L)
{
    beginLibTable(L, {"DirectoryIterator"});
    lua_createtable(L, 0, 12);
    addFunction(L, "chdir", "bool (*)(const char *, char **)", fnptr(lfsChdir));
    addFunction(L, "currentdir", "char * (*)(char **)", fnptr(currentDir));
    addFunction(L, "mkdir", "bool (*)(const char *, char **)", fnptr(lfsMkdir));
    addFunction(L, "rmdir", "bool (*)(const char *, char **)", fnptr(lfsRmdir));
    addFunction(L, "touch", "bool (*)(const char *, char **)", fnptr(lfsTouch));
    addFunction(L, "get_mtime", "long long (*)(const char *, char **)", fnptr(getMtime));
    addFunction(L, "get_mode", "char * (*)(const char *, char **)", fnptr(getMode));
    addFunction(L, "get_size", "unsigned long long (*)(const char *, char **)", fnptr(getSize));
    addFunction(L, "dir_new", "DirectoryIterator * (*)(const char *, char **)", fnptr(dirNew));
    addFunction(L, "dir_free", "void (*)(DirectoryIterator *)", fnptr(dirFree));
    addFunction(L, "dir_next", "char * (*)(DirectoryIterator &, char **)", fnptr(dirNext));
    addFunction(L, "dir_close", "void (*)(DirectoryIterator &)", fnptr(dirClose));
    lua_remove(L, -2);
    return 1;
}

void setPreload(lua_State *L, const char *name, lua_CFunction open)
{
    lua_pushcfunction(L, open);
    lua_setfield(L, -2, name);
}

} // namespace

void installGlobalLocale()
{
    boost::locale::generator gen;
    for (const char *name : {"", "C.UTF-8"}) {
        try {
            std::locale::global(gen(name));
            return;
        } catch (...) {
        }
    }
    std::locale::global(std::locale::classic());
}

void preload(lua_State *L)
{
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "preload");
    setPreload(L, "aegisub.__re_impl", openRe);
    setPreload(L, "aegisub.__unicode_impl", openUnicode);
    setPreload(L, "aegisub.__lfs_impl", openLfs);
    setPreload(L, "lpeg", luaopen_lpeg);
    setPreload(L, "luabins", luaopen_luabins);
    lua_pop(L, 2);
}

} // namespace hikari::lua_native
