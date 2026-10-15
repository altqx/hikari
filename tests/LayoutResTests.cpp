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
#include "../HikariSub/LayoutRes.h"

#include <map>
#include <string>

namespace {

// A script's info lines, which give an empty value for a missing name as
// SubsFile::GetSInfo does.
struct ScriptInfo {
	std::map<std::wstring, std::wstring> values;

	const std::wstring &GetSInfo(const std::wstring &name)
	{
		static const std::wstring missing;
		auto found = values.find(name);
		return found == values.end() ? missing : found->second;
	}
};

} // namespace

TEST(LayoutResMissingWithNeitherValue)
{
	ScriptInfo info{ { { L"PlayResX", L"1920" }, { L"PlayResY", L"1080" } } };
	CHECK(!HasLayoutRes(info));
}

TEST(LayoutResGivenByLayoutResXAlone)
{
	ScriptInfo info{ { { L"LayoutResX", L"1280" } } };
	CHECK(HasLayoutRes(info));
}

TEST(LayoutResGivenByLayoutResYAlone)
{
	ScriptInfo info{ { { L"LayoutResY", L"720" } } };
	CHECK(HasLayoutRes(info));
}

TEST(LayoutResGivenByBothValues)
{
	ScriptInfo info{ { { L"LayoutResX", L"1280" }, { L"LayoutResY", L"720" } } };
	CHECK(HasLayoutRes(info));
}
