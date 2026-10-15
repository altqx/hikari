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

#include "check.h"
#include "../HikariSub/ReplaceBackup.h"

#include <string>

namespace {

// a name that can be made as a folder in the backup folder on any system
bool IsPlainFolderName(const std::wstring &name)
{
	if (name.empty() || name == L"." || name == L"..")
		return false;
	for (wchar_t unit : name) {
		if (unit < 32 || std::wstring(L"<>:\"/\\|?*").find(unit) != std::wstring::npos)
			return false;
	}
	return name.back() != L' ' && name.back() != L'.';
}

} // namespace

// Find in subtitles with subfolders: ep01/en.ass and ep02/en.ass are both
// replaced, so each needs a backup of its own
TEST(ReplaceBackupSameNameInOtherFolders)
{
	std::wstring first = ReplaceBackupFolderName(L"/home/user/subs/ep01/en.ass");
	std::wstring second = ReplaceBackupFolderName(L"/home/user/subs/ep02/en.ass");
	CHECK(IsPlainFolderName(first));
	CHECK(IsPlainFolderName(second));
	CHECK(first != second);
	// folders of the same name in other places
	CHECK(ReplaceBackupFolderName(L"/a/subs/en.ass") != ReplaceBackupFolderName(L"/b/subs/en.ass"));
#ifdef _WIN32
	CHECK(ReplaceBackupFolderName(L"C:\\subs\\ep01\\en.ass") != ReplaceBackupFolderName(L"C:\\subs\\ep02\\en.ass"));
#endif
}

// a file keeps one backup, the one of its last replace, as before
TEST(ReplaceBackupSameFileSameFolder)
{
	CHECK(ReplaceBackupFolderName(L"/home/user/subs/ep01/en.ass") ==
		ReplaceBackupFolderName(L"/home/user/subs/ep01/en.ass"));
	CHECK(ReplaceBackupFolderName(L"/home/user/subs/ep01/en.ass") ==
		ReplaceBackupFolderName(L"/home/user/subs/ep01/pl.ass"));
}

// the user finds a file's backup by the name of its folder
TEST(ReplaceBackupFolderNamedAfterFolder)
{
	CHECK(ReplaceBackupFolderName(L"/home/user/subs/ep01/en.ass").rfind(L"ep01 ", 0) == 0);
#ifdef _WIN32
	CHECK(ReplaceBackupFolderName(L"C:\\subs\\ep01\\en.ass").rfind(L"ep01 ", 0) == 0);
#endif
}

// a folder name that could not be made in the backup folder, or a long one
TEST(ReplaceBackupFolderNameIsPlain)
{
	CHECK(IsPlainFolderName(ReplaceBackupFolderName(L"/en.ass")));
	CHECK(IsPlainFolderName(ReplaceBackupFolderName(L"/subs/a:b?<c>|d*\"e\\f/en.ass")));
	CHECK(IsPlainFolderName(ReplaceBackupFolderName(L"/subs/ep01./en.ass")));
#ifdef _WIN32
	CHECK(IsPlainFolderName(ReplaceBackupFolderName(L"C:\\en.ass")));
	CHECK(IsPlainFolderName(ReplaceBackupFolderName(L"\\\\server\\share\\en.ass")));
#endif
	std::wstring longName(300, L'x');
	std::wstring folder = ReplaceBackupFolderName(L"/subs/" + longName + L"/en.ass");
	CHECK(IsPlainFolderName(folder));
	CHECK(folder.size() <= 64);
	// cut short, the name still tells long folders apart
	CHECK(folder != ReplaceBackupFolderName(L"/subs/" + longName + L"y/en.ass"));
}
