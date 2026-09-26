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

// A minimal test harness: TEST registers a function, CHECK/CHECK_EQ record
// failures, and main() in check_main.cpp runs everything.

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace check {

struct Test {
	const char *name;
	std::function<void()> body;
};

inline std::vector<Test> &Registry()
{
	static std::vector<Test> tests;
	return tests;
}

inline int &Failures()
{
	static int failures = 0;
	return failures;
}

struct Registrar {
	Registrar(const char *name, std::function<void()> body) { Registry().push_back({ name, std::move(body) }); }
};

template <typename A, typename B>
void ExpectEqual(const A &actual, const B &expected, const char *actualText, const char *expectedText,
	const char *file, int line)
{
	if (actual == expected)
		return;
	++Failures();
	std::printf("%s:%d: CHECK_EQ(%s, %s) failed: got %s, expected %s\n", file, line, actualText, expectedText,
		std::to_string(actual).c_str(), std::to_string(expected).c_str());
}

} // namespace check

#define CHECK_CONCAT_(a, b) a##b
#define CHECK_CONCAT(a, b) CHECK_CONCAT_(a, b)

#define TEST(name) \
	static void CHECK_CONCAT(test_, name)(); \
	static check::Registrar CHECK_CONCAT(registrar_, name)(#name, CHECK_CONCAT(test_, name)); \
	static void CHECK_CONCAT(test_, name)()

#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			++check::Failures(); \
			std::printf("%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
		} \
	} while (0)

#define CHECK_EQ(actual, expected) \
	check::ExpectEqual((actual), (expected), #actual, #expected, __FILE__, __LINE__)
