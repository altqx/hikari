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

// JsonValue::Parse()'s caps, apart from wx so that the update check's policy,
// which cuts a reply off at the same size, and its tests can include them.

#pragma once

#include <cstddef>

namespace json_limits
{
	// Both caps exist because the input comes off the network: a hostile or
	// broken response must not be able to exhaust the stack or memory.
	constexpr size_t kMaxDepth = 64;
	// Parse() reads nothing longer.
	constexpr size_t kMaxInput = 4u * 1024u * 1024u;
}
