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
#include "../HikariSub/HotkeysFile.h"

namespace {

bool IsVersion(const wchar_t *line, HotkeysFileVersion version)
{
	return ReadHotkeysHeader(line).version == version;
}

} // namespace

TEST(hotkeys_header_semver_is_current)
{
	CHECK(IsVersion(L"[HikariSub v0.0.1-rc.1]\r", HotkeysFileVersion::Current));
	CHECK(IsVersion(L"[HikariSub v0.0.1]", HotkeysFileVersion::Current));
	CHECK(IsVersion(L"[HikariSub v1.2.3  main abcdef1 DEBUG]\r", HotkeysFileVersion::Current));
	CHECK(!ReadHotkeysHeader(L"[HikariSub v0.0.1-rc.1]").convert);
}

TEST(hotkeys_header_old_build_is_read_by_build_number)
{
	HotkeysFileHeader converted = ReadHotkeysHeader(L"[Kainote v0.9.0.1100 Release]\r");
	CHECK(converted.version == HotkeysFileVersion::OldBuild);
	CHECK(converted.convert);

	HotkeysFileHeader recent = ReadHotkeysHeader(L"[Kainote v0.9.0.1525  master 1234567]");
	CHECK(recent.version == HotkeysFileVersion::OldBuild);
	CHECK(!recent.convert);

	CHECK(IsVersion(L"[Kainote v0.8.0.487]", HotkeysFileVersion::Outdated));
	CHECK(IsVersion(L"[Kainote v0.8.0.488]", HotkeysFileVersion::OldBuild));
}

TEST(hotkeys_header_without_version_is_outdated)
{
	CHECK(IsVersion(L"", HotkeysFileVersion::Outdated));
	CHECK(IsVersion(L"Script x=Ctrl-A", HotkeysFileVersion::Outdated));
	CHECK(IsVersion(L"[Kainote]", HotkeysFileVersion::Outdated));
	CHECK(IsVersion(L"[HikariSub vnext]", HotkeysFileVersion::Outdated));
}

TEST(hotkeys_short_current_file_is_not_filled)
{
	// SaveHkeys leaves out removed shortcuts, so a current file with few
	// bindings must not get the defaults back
	CHECK(!HotkeysNeedDefaultFill(HotkeysFileVersion::Current, 0));
	CHECK(!HotkeysNeedDefaultFill(HotkeysFileVersion::Current, 3));
	CHECK(!HotkeysNeedDefaultFill(HotkeysFileVersion::Current, 10));
	CHECK(!HotkeysNeedDefaultFill(HotkeysFileVersion::Current, 200));
}

TEST(hotkeys_short_old_build_file_is_filled)
{
	CHECK(HotkeysNeedDefaultFill(HotkeysFileVersion::OldBuild, 0));
	CHECK(HotkeysNeedDefaultFill(HotkeysFileVersion::OldBuild, 10));
	CHECK(!HotkeysNeedDefaultFill(HotkeysFileVersion::OldBuild, 11));
}
