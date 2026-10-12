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

#include <map>
#include <string>

namespace {

bool IsVersion(const wchar_t *line, HotkeysFileVersion version)
{
	return ReadHotkeysHeader(line).version == version;
}

void ReplaceAll(std::wstring &text, const std::wstring &from, const std::wstring &to)
{
	for (size_t at = text.find(from); at != std::wstring::npos; at = text.find(from, at + to.size()))
		text.replace(at, from.size(), to);
}

// What ConfigConverter::ConvertHotkeys does to a pre-1141 file's lines: old
// names and window letters become current ones, under a current header.
void ConvertLikeConfigConverter(std::wstring &lines)
{
	ReplaceAll(lines, L"OldCommit N=", L"Commit S=");
	ReplaceAll(lines, L"OldPlay W=", L"Play V=");
	lines = L"[HikariSub v0.0.1-rc.2]\n" + lines;
}

// LoadHkeys's loop in short: "<name> <window>=<keys>" lines, where a name
// GetIdValue does not know resolves to id 0.
std::map<int, std::wstring> ReadBindings(const std::wstring &lines)
{
	const std::map<std::wstring, int> ids{{L"Commit", 1}, {L"Play", 2}};
	std::map<int, std::wstring> keys;
	size_t start = 0;
	while (start < lines.size()) {
		size_t end = lines.find(L'\n', start);
		std::wstring line = lines.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
		start = (end == std::wstring::npos) ? lines.size() : end + 1;
		if (!line.empty() && line.back() == L'\r')
			line.pop_back();
		size_t space = line.find(L' ');
		size_t equals = line.find(L'=');
		if (space == std::wstring::npos || equals == std::wstring::npos)
			continue;
		auto id = ids.find(line.substr(0, space));
		keys[id == ids.end() ? 0 : id->second] = line.substr(equals + 1);
	}
	return keys;
}

const wchar_t *const OldFile =
	L"[Kainote v0.9.0.1100 Release]\r\n"
	L"OldCommit N=Ctrl-K\r\n"
	L"OldPlay W=F5\r\n";

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

TEST(hotkeys_fill_keeps_the_file_bindings)
{
	std::map<int, std::wstring> keys{{1, L"Ctrl-K"}, {3, L"F5"}};
	const std::map<int, std::wstring> defaults{{1, L"Ctrl-A"}, {2, L"Ctrl-B"}, {3, L"F3"}};
	CHECK(FillMissingHotkeys(keys, defaults) == 1);
	CHECK(keys.size() == 3);
	CHECK(keys[1] == L"Ctrl-K");
	CHECK(keys[2] == L"Ctrl-B");
	CHECK(keys[3] == L"F5");
	CHECK(FillMissingHotkeys(keys, defaults) == 0);
}

TEST(hotkeys_converted_file_is_read_from_the_rewritten_lines)
{
	HotkeysFileText file = ReadHotkeysText(OldFile, ConvertLikeConfigConverter);
	CHECK(file.header.version == HotkeysFileVersion::OldBuild);
	CHECK(file.header.convert);

	// the rewritten file, written back over the old one, starts with the
	// converter's current header
	CHECK(file.rewritten == L"[HikariSub v0.0.1-rc.2]\nCommit S=Ctrl-K\r\nPlay V=F5\r\n");

	// the bindings are the rewritten lines, without either header: the old
	// names would resolve to id 0 and the file's bindings would be lost
	CHECK(file.bindings == L"Commit S=Ctrl-K\r\nPlay V=F5\r\n");
	std::map<int, std::wstring> keys = ReadBindings(file.bindings);
	CHECK(keys.size() == 2);
	CHECK(keys[1] == L"Ctrl-K");
	CHECK(keys[2] == L"F5");
}

TEST(hotkeys_only_a_pre_1141_file_is_converted)
{
	int conversions = 0;
	auto count = [&](std::wstring &) { conversions++; };

	HotkeysFileText recent = ReadHotkeysText(L"[Kainote v0.9.0.1525  master 1234567]\r\nCommit S=Ctrl-K\r\n", count);
	CHECK(!recent.header.convert);
	CHECK(recent.rewritten.empty());
	CHECK(recent.bindings == L"Commit S=Ctrl-K\r\n");

	HotkeysFileText current = ReadHotkeysText(L"[HikariSub v0.0.1]\nCommit S=Ctrl-K\n", count);
	CHECK(current.header.version == HotkeysFileVersion::Current);
	CHECK(current.bindings == L"Commit S=Ctrl-K\n");

	HotkeysFileText headerless = ReadHotkeysText(L"Commit S=Ctrl-K\n", count);
	CHECK(headerless.header.version == HotkeysFileVersion::Outdated);
	CHECK(headerless.bindings == L"Commit S=Ctrl-K\n");

	CHECK(conversions == 0);
}

TEST(hotkeys_converted_filled_file_is_written_back)
{
	// first start: a short pre-1141 file is converted, read and filled
	HotkeysFileText first = ReadHotkeysText(OldFile, ConvertLikeConfigConverter);
	std::map<int, std::wstring> keys = ReadBindings(first.bindings);
	CHECK(HotkeysNeedDefaultFill(first.header.version, static_cast<int>(keys.size())));
	const std::map<int, std::wstring> defaults{{1, L"Enter"}, {2, L"Down"}, {3, L"H"}};
	size_t filled = FillMissingHotkeys(keys, defaults);
	CHECK(filled == 1);
	CHECK(keys[1] == L"Ctrl-K");
	CHECK(keys[2] == L"F5");
	CHECK(HotkeysNeedWriteBack(first.header, filled > 0));

	// next start without the write-back: the rewritten file is current,
	// which is never filled, so the default for id 3 would be gone
	HotkeysFileText next = ReadHotkeysText(first.rewritten, ConvertLikeConfigConverter);
	CHECK(next.header.version == HotkeysFileVersion::Current);
	CHECK(!next.header.convert);
	CHECK(!HotkeysNeedDefaultFill(next.header.version, static_cast<int>(ReadBindings(next.bindings).size())));
}

TEST(hotkeys_write_back_only_after_convert_and_fill)
{
	HotkeysFileHeader converted = ReadHotkeysHeader(L"[Kainote v0.9.0.1100 Release]");
	CHECK(!HotkeysNeedWriteBack(converted, false));

	// a 1141+ file keeps its old header on disk, so the next start fills it again
	HotkeysFileHeader recent = ReadHotkeysHeader(L"[Kainote v0.9.0.1525  master 1234567]");
	CHECK(HotkeysNeedDefaultFill(recent.version, 3));
	CHECK(!HotkeysNeedWriteBack(recent, true));
}
