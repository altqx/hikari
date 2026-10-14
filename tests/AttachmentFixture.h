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

#pragma once

// tests/fixtures/attachments (make-fixtures.py): attached.ass carries
// HikariAttachedTest.ttf in [Fonts] and red.png in [Graphics]. The script is
// split here as SubsLoader::LoadASS splits it: the sections it does not read
// itself go to SubsFile's embedded sections, the rest is what
// SubsGrid::GetVisible hands the renderers (a byte order mark, then the
// script without those sections).

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace attachment_fixture {

inline std::string Path(const char *name)
{
	return std::string(HIKARISUB_TEST_FIXTURES) + "/attachments/" + name;
}

inline std::string Read(const char *name)
{
	std::ifstream in(Path(name), std::ios::binary);
	std::ostringstream out;
	out << in.rdbuf();
	return out.str();
}

inline std::vector<unsigned char> Bytes(const char *name)
{
	std::string text = Read(name);
	return std::vector<unsigned char>(text.begin(), text.end());
}

inline std::string Lower(std::string s)
{
	for (char &c : s)
		if (c >= 'A' && c <= 'Z')
			c += 'a' - 'A';
	return s;
}

struct Split {
	std::wstring embedded; // SubsFile::GetEmbeddedSections, ASCII here
	std::string visible;   // GetVisible's text, UTF-8, with its byte order mark
};

inline Split SplitScript()
{
	Split split;
	split.visible = "\xEF\xBB\xBF";
	std::istringstream in(Read("attached.ass"));
	std::string line;
	bool embedded = false;
	while (std::getline(in, line)) {
		while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
			line.pop_back();
		size_t start = line.find_first_not_of(" \t");
		if (start == std::string::npos)
			continue;
		line.erase(0, start);
		if (line[0] == '[') {
			std::string header = Lower(line);
			bool info = header.rfind("[script info]", 0) == 0;
			embedded = !info && header.rfind("[v4", 0) != 0 && header.rfind("[events]", 0) != 0;
		}
		if (embedded) {
			split.embedded += std::wstring(line.begin(), line.end()) + L"\r\n";
			continue;
		}
		split.visible += line + "\r\n";
	}
	return split;
}

} // namespace attachment_fixture
