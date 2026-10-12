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

#include <cstddef>
#include <cstdint>
#include <vector>

// Line groups (trees) are stored by position only: a description line,
// then the run of member lines right after it, all closed or all open.
// A member that does not follow its description or another member of the
// group is out of every group. These functions keep that layout whole.
namespace LineGroups {

// the values of Dialogue::treeState
enum : unsigned char {
	ORDINARY = 0,
	DESCRIPTION = 3,
	CLOSED = 4,
	OPENED = 5
};

// the values of Dialogue::isVisible
enum : unsigned char {
	HIDDEN = 0,
	SHOWN = 1
};

struct Row {
	// the line's identity; a moved or edited line keeps it, a new line gets a new one
	uint64_t id;
	unsigned char tree;
	unsigned char visible;
};

// a line whose tree state or visibility has to change
struct Change {
	size_t row;
	unsigned char tree;
	unsigned char visible;
};

inline bool IsMember(unsigned char tree) { return tree == CLOSED || tree == OPENED; }

// Fixes a layout read from a file: members with no description become
// ordinary shown lines, every member of a group takes the state of its
// first member, closed members are hidden and open members shown.
std::vector<Change> Repair(const std::vector<Row> &rows);

// After an edit: the lines that are new (their id is not in before) take
// the place they were put in. A new line put between two lines of a group
// joins it; a new member right after a group extends it; any other new
// member becomes an ordinary shown line. Existing lines are not changed.
std::vector<Change> PlaceNewRows(const std::vector<Row> &before, const std::vector<Row> &after);

struct Verdict {
	// false when the edit leaves a member out of every group, or splits,
	// orphans or moves members out of a group that existed before
	bool ok = true;
	// descriptions (ids) of the groups that existed before and that the
	// edit breaks; empty when only lines that were in no group are affected
	std::vector<uint64_t> broken;
};

// Compares the lines after an edit with the lines before it. Lines whose id
// is in movedIn were put into a group on purpose and may leave theirs.
Verdict Check(const std::vector<Row> &before, const std::vector<Row> &after,
	const std::vector<uint64_t> &movedIn = {});

// Removing a group deletes its description and makes its members ordinary
// shown lines, like Remove tree.
struct Removal {
	// the description rows to delete, ascending
	std::vector<size_t> deleted;
	// the member rows to ungroup, numbered as before the deletion
	std::vector<Change> ungrouped;
};
Removal PlanRemove(const std::vector<Row> &rows, const std::vector<uint64_t> &descriptions);

// where row is after the rows in deleted (ascending) are gone, or -1 if it is one of them
size_t RowAfterDelete(const std::vector<size_t> &deleted, size_t row);

} // namespace LineGroups
