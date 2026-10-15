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
#include "../HikariSub/FontOutputNames.h"

#include <map>

namespace {

// Stands in for the font files: each source path holds the named bytes.
struct Fonts {
	std::map<std::wstring, std::string> bytes;
	FontOutputNames names;
	FontOutputNames::Result Add(const std::wstring &source, const std::wstring &fileName)
	{
		return names.Add(source, fileName, [&](const std::wstring &earlier) {
			return bytes.at(earlier) == bytes.at(source);
		});
	}
};

} // namespace

TEST(font_collector_keeps_two_different_fonts_with_one_file_name)
{
	// a system and a per-user arial.ttf with different bytes: the second is
	// written next to the first, not over it
	Fonts fonts;
	fonts.bytes = { { L"C:/Windows/Fonts/arial.ttf", "system" },
		{ L"C:/Users/u/AppData/Local/Microsoft/Windows/Fonts/arial.ttf", "per-user" },
		{ L"D:/Fonts/arial.ttf", "external" } };
	auto first = fonts.Add(L"C:/Windows/Fonts/arial.ttf", L"arial.ttf");
	CHECK(first.outcome == FontOutputNames::NEW_NAME);
	CHECK(first.name == L"arial.ttf");
	auto second = fonts.Add(L"C:/Users/u/AppData/Local/Microsoft/Windows/Fonts/arial.ttf", L"arial.ttf");
	CHECK(second.outcome == FontOutputNames::RENAMED);
	CHECK(second.name == L"arial (2).ttf");
	auto third = fonts.Add(L"D:/Fonts/arial.ttf", L"arial.ttf");
	CHECK(third.outcome == FontOutputNames::RENAMED);
	CHECK(third.name == L"arial (3).ttf");
}

TEST(font_collector_writes_the_same_bytes_under_one_name_once)
{
	// the same font found under two paths is written once, under the name it
	// was first written under
	Fonts fonts;
	fonts.bytes = { { L"/usr/share/fonts/a/DejaVuSans.ttf", "dejavu" },
		{ L"/home/u/.local/share/fonts/DejaVuSans.ttf", "dejavu" },
		{ L"/opt/fonts/DejaVuSans.ttf", "other" },
		{ L"/srv/fonts/DejaVuSans.ttf", "other" } };
	CHECK(fonts.Add(L"/usr/share/fonts/a/DejaVuSans.ttf", L"DejaVuSans.ttf").outcome == FontOutputNames::NEW_NAME);
	auto same = fonts.Add(L"/home/u/.local/share/fonts/DejaVuSans.ttf", L"DejaVuSans.ttf");
	CHECK(same.outcome == FontOutputNames::SAME_FONT);
	CHECK(same.name == L"DejaVuSans.ttf");
	auto renamed = fonts.Add(L"/opt/fonts/DejaVuSans.ttf", L"DejaVuSans.ttf");
	CHECK(renamed.outcome == FontOutputNames::RENAMED);
	CHECK(renamed.name == L"DejaVuSans (2).ttf");
	// the same bytes as the renamed one: written once, under its new name
	auto sameAsRenamed = fonts.Add(L"/srv/fonts/DejaVuSans.ttf", L"DejaVuSans.ttf");
	CHECK(sameAsRenamed.outcome == FontOutputNames::SAME_FONT);
	CHECK(sameAsRenamed.name == L"DejaVuSans (2).ttf");
}

TEST(font_collector_compares_file_names_without_case)
{
	Fonts fonts;
	fonts.bytes = { { L"a/arial.ttf", "one" }, { L"b/ARIAL.TTF", "two" }, { L"c/Arial (2).ttf", "three" } };
	CHECK(fonts.Add(L"a/arial.ttf", L"arial.ttf").name == L"arial.ttf");
	auto upper = fonts.Add(L"b/ARIAL.TTF", L"ARIAL.TTF");
	CHECK(upper.outcome == FontOutputNames::RENAMED);
	CHECK(upper.name == L"ARIAL (2).TTF");
	// a font whose own name is taken by a renamed one gets the next free name
	auto own = fonts.Add(L"c/Arial (2).ttf", L"Arial (2).ttf");
	CHECK(own.outcome == FontOutputNames::RENAMED);
	CHECK(own.name == L"Arial (2) (2).ttf");
}

TEST(font_collector_renames_a_file_name_without_an_extension)
{
	Fonts fonts;
	fonts.bytes = { { L"a/font", "one" }, { L"b/font", "two" }, { L"a/.hidden", "three" }, { L"b/.hidden", "four" } };
	fonts.Add(L"a/font", L"font");
	CHECK(fonts.Add(L"b/font", L"font").name == L"font (2)");
	fonts.Add(L"a/.hidden", L".hidden");
	CHECK(fonts.Add(L"b/.hidden", L".hidden").name == L".hidden (2)");
}

TEST(font_collector_leaves_different_file_names_and_a_new_run_alone)
{
	Fonts fonts;
	fonts.bytes = { { L"a/one.ttf", "x" }, { L"a/two.otf", "x" }, { L"b/one.ttf", "y" } };
	CHECK(fonts.Add(L"a/one.ttf", L"one.ttf").outcome == FontOutputNames::NEW_NAME);
	auto two = fonts.Add(L"a/two.otf", L"two.otf");
	CHECK(two.outcome == FontOutputNames::NEW_NAME);
	CHECK(two.name == L"two.otf");
	// a new collection starts with every name free
	fonts.names.Clear();
	auto again = fonts.Add(L"b/one.ttf", L"one.ttf");
	CHECK(again.outcome == FontOutputNames::NEW_NAME);
	CHECK(again.name == L"one.ttf");
}
