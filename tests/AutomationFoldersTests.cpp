//  Copyright (c) 2026, altqx

//  HikariSub is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.

//  HikariSub is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.

//  You should have received a copy of the GNU General Public License
//  along with HikariSub.  If not, see <http://www.gnu.org/licenses/>.

// Where Automation reads and writes in a portable and an installed copy
// (AutomationFolders.h).

#include "check.h"
#include "../HikariSub/AutomationFolders.h"

#include <string>
#include <utility>
#include <vector>

using namespace automation_folders;

namespace {

Roots Portable()
{
	Roots roots;
	roots.shipped = "/home/a/HikariSub";
	roots.data = "/home/a/HikariSub";
	return roots;
}

Roots InstalledLinux()
{
	Roots roots;
	roots.shipped = "/opt/hikarisub";
	roots.data = "/home/a/.local/share/HikariSub/Legacy";
	return roots;
}

Roots InstalledWindows()
{
	Roots roots;
	roots.shipped = "C:\\Program Files\\HikariSub";
	roots.data = "C:\\Users\\a\\AppData\\Local\\HikariSub\\Legacy\\";
	roots.separator = '\\';
	roots.caseSensitive = false;
	return roots;
}

using Listing = std::vector<std::pair<std::string, std::vector<std::string>>>;

} // namespace

TEST(automation_folders_portable_copy_keeps_everything_beside_the_program)
{
	Roots roots = Portable();
	CHECK(!Installed(roots));
	CHECK((AutoloadFolders(roots) == std::vector<std::string>{ "/home/a/HikariSub/Automation/automation/Autoload" }));
	CHECK((IncludeFolders(roots) == std::vector<std::string>{ "/home/a/HikariSub/Automation/automation/Include" }));
	// It ships its log, temp and autosave folders; nothing is made.
	CHECK(UserFolders(roots).empty());

	// Windows: one root, spelt in another case, is still one root.
	Roots windows = InstalledWindows();
	windows.data = "c:\\program files\\hikarisub";
	CHECK(!Installed(windows));
	CHECK(UserFolders(windows).empty());
	CHECK(AutoloadFolders(windows).size() == 1);
}

TEST(automation_folders_installed_copy_reads_the_users_autoload_folders)
{
	Roots roots = InstalledLinux();
	CHECK(Installed(roots));
	CHECK((AutoloadFolders(roots) == std::vector<std::string>{
		"/opt/hikarisub/Automation/automation/Autoload",
		"/home/a/.local/share/HikariSub/Legacy/Automation/automation/Autoload",
		// DependencyControl's ?user/automation/autoload.
		"/home/a/.local/share/HikariSub/Legacy/Automation/automation/autoload",
	}));
	CHECK((IncludeFolders(roots) == std::vector<std::string>{
		"/home/a/.local/share/HikariSub/Legacy/Automation/automation/Include",
		"/home/a/.local/share/HikariSub/Legacy/Automation/automation/include",
		"/opt/hikarisub/Automation/automation/Include",
	}));

	// Windows does not tell Autoload from autoload: one folder, and a
	// trailing separator on the data root is not doubled.
	Roots windows = InstalledWindows();
	CHECK((AutoloadFolders(windows) == std::vector<std::string>{
		"C:\\Program Files\\HikariSub\\Automation\\automation\\Autoload",
		"C:\\Users\\a\\AppData\\Local\\HikariSub\\Legacy\\Automation\\automation\\Autoload",
	}));
	CHECK((IncludeFolders(windows) == std::vector<std::string>{
		"C:\\Users\\a\\AppData\\Local\\HikariSub\\Legacy\\Automation\\automation\\Include",
		"C:\\Program Files\\HikariSub\\Automation\\automation\\Include",
	}));
}

TEST(automation_folders_installed_copy_makes_the_portable_layout_under_the_data_root)
{
	CHECK((UserFolders(InstalledLinux()) == std::vector<std::string>{
		"/home/a/.local/share/HikariSub/Legacy/Automation/log",
		"/home/a/.local/share/HikariSub/Legacy/Automation/temp",
		"/home/a/.local/share/HikariSub/Legacy/Automation/autosave",
		"/home/a/.local/share/HikariSub/Legacy/Automation/automation/Autoload",
		"/home/a/.local/share/HikariSub/Legacy/Automation/automation/Include",
	}));
	// None of them is in the install folder.
	for (const std::string &folder : UserFolders(InstalledWindows()))
		CHECK(folder.rfind("C:\\Users\\a\\AppData\\Local\\HikariSub\\Legacy\\Automation\\", 0) == 0);
}

TEST(automation_folders_a_user_script_replaces_the_shipped_one)
{
	Roots roots = InstalledLinux();
	std::vector<std::string> folders = AutoloadFolders(roots);
	Listing listed = {
		{ folders[0], { "b.lua", "a.moon", "readme.txt", "gradient-factory.lua", "UPPER.LUA" } },
		{ folders[1], { "gradient-factory.lua", "mine.lua", "notes" } },
		{ folders[2], { "l0.DependencyControl.Toolbox.moon", "Gradient-Factory.lua" } },
	};
	CHECK((ChooseScripts(roots, listed) == std::vector<std::string>{
		"/opt/hikarisub/Automation/automation/Autoload/b.lua",
		"/opt/hikarisub/Automation/automation/Autoload/a.moon",
		// In the shipped one's place, from the user's folder.
		"/home/a/.local/share/HikariSub/Legacy/Automation/automation/Autoload/gradient-factory.lua",
		"/opt/hikarisub/Automation/automation/Autoload/UPPER.LUA",
		"/home/a/.local/share/HikariSub/Legacy/Automation/automation/Autoload/mine.lua",
		"/home/a/.local/share/HikariSub/Legacy/Automation/automation/autoload/l0.DependencyControl.Toolbox.moon",
		// Another file on Linux.
		"/home/a/.local/share/HikariSub/Legacy/Automation/automation/autoload/Gradient-Factory.lua",
	}));

	// On Windows the name's case does not make another script.
	Roots windows = InstalledWindows();
	std::vector<std::string> windowsFolders = AutoloadFolders(windows);
	Listing windowsListed = {
		{ windowsFolders[0], { "gradient-factory.lua", "b.lua" } },
		{ windowsFolders[1], { "Gradient-Factory.lua" } },
	};
	CHECK((ChooseScripts(windows, windowsListed) == std::vector<std::string>{
		"C:\\Users\\a\\AppData\\Local\\HikariSub\\Legacy\\Automation\\automation\\Autoload\\Gradient-Factory.lua",
		"C:\\Program Files\\HikariSub\\Automation\\automation\\Autoload\\b.lua",
	}));
}

TEST(automation_folders_portable_copy_loads_its_folder_as_it_lists_it)
{
	Roots roots = Portable();
	Listing listed = { { AutoloadFolders(roots)[0], { "z.lua", "a.lua", "x.dll", "m.MOON" } } };
	CHECK((ChooseScripts(roots, listed) == std::vector<std::string>{
		"/home/a/HikariSub/Automation/automation/Autoload/z.lua",
		"/home/a/HikariSub/Automation/automation/Autoload/a.lua",
		"/home/a/HikariSub/Automation/automation/Autoload/m.MOON",
	}));
	CHECK(!IsScriptName("script.lua.bak"));
	CHECK(IsScriptName(".lua"));
}

TEST(automation_folders_decode_path_reads_the_prefix)
{
	CHECK(DecodePrefix(L"?audio/a.wav").prefix == PathPrefix::Audio);
	CHECK(DecodePrefix(L"?audio/a.wav").length == 6);
	CHECK(DecodePrefix(L"?data/a.lua").prefix == PathPrefix::Data);
	CHECK(DecodePrefix(L"?data/a.lua").length == 5);
	CHECK(DecodePrefix(L"?dictionary/en.dic").prefix == PathPrefix::Dictionary);
	CHECK(DecodePrefix(L"?dictionary/en.dic").length == 11);
	CHECK(DecodePrefix(L"?local/cfg").prefix == PathPrefix::Local);
	CHECK(DecodePrefix(L"?local/cfg").length == 6);
	CHECK(DecodePrefix(L"?script/out.ass").prefix == PathPrefix::Script);
	CHECK(DecodePrefix(L"?script/out.ass").length == 7);
	CHECK(DecodePrefix(L"?temp/t").prefix == PathPrefix::Temp);
	CHECK(DecodePrefix(L"?temp/t").length == 5);
	CHECK(DecodePrefix(L"?user").prefix == PathPrefix::User);
	CHECK(DecodePrefix(L"?user").length == 5);
	CHECK(DecodePrefix(L"?video/v.mkv").prefix == PathPrefix::Video);
	CHECK(DecodePrefix(L"?video/v.mkv").length == 6);
	// Only the second and fifth characters are compared.
	CHECK(DecodePrefix(L"?axxix/f").prefix == PathPrefix::Audio);
	CHECK(DecodePrefix(L"?other/f").prefix == PathPrefix::None);
	CHECK(DecodePrefix(L"/home/a/x.ass").prefix == PathPrefix::None);
}

TEST(automation_folders_decode_path_leaves_a_short_path)
{
	for (const wchar_t *path : { L"", L"?", L"?a", L"?au", L"?aud", L"?u", L"?use" }) {
		DecodedPrefix decoded = DecodePrefix(path);
		CHECK(decoded.prefix == PathPrefix::None);
		CHECK(decoded.length == 0);
	}
	// "?a" shortened in place, its buffer still holding "?a\0xi": what is
	// past its end is not read, or it would be ?audio.
	std::wstring shortened = L"?axxi/f";
	shortened.resize(2);
	CHECK(DecodePrefix(shortened).prefix == PathPrefix::None);
}
