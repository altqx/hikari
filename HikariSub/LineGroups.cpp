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

#include "LineGroups.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace LineGroups {

namespace {

// a member keeps a visibility that matches its state: closed members are
// hidden, open members are not
unsigned char VisibleAs(unsigned char tree, unsigned char visible)
{
	if (tree == CLOSED)
		return HIDDEN;
	if (visible == HIDDEN)
		return SHOWN;
	return visible;
}

// an ordinary line that was hidden as a closed member is shown
unsigned char Shown(unsigned char visible)
{
	return visible == HIDDEN ? (unsigned char)SHOWN : visible;
}

void Set(std::vector<Change> &changes, std::vector<Row> &rows, size_t i, unsigned char tree, unsigned char visible)
{
	Row &row = rows[i];
	if (row.tree == tree && row.visible == visible)
		return;
	row.tree = tree;
	row.visible = visible;
	changes.push_back({ i, tree, visible });
}

void AddOnce(std::vector<uint64_t> &ids, uint64_t id)
{
	if (std::find(ids.begin(), ids.end(), id) == ids.end())
		ids.push_back(id);
}

} // namespace

std::vector<Change> Repair(const std::vector<Row> &original)
{
	std::vector<Row> rows = original;
	std::vector<Change> changes;
	bool inGroup = false;
	// the state of the group's first member, 0 before it
	unsigned char state = 0;
	for (size_t i = 0; i < rows.size(); i++) {
		const Row &row = rows[i];
		if (row.tree == DESCRIPTION) {
			inGroup = true;
			state = 0;
		}
		else if (IsMember(row.tree)) {
			if (!inGroup) {
				Set(changes, rows, i, ORDINARY, SHOWN);
				continue;
			}
			if (!state)
				state = row.tree;
			Set(changes, rows, i, state, VisibleAs(state, row.visible));
		}
		else {
			inGroup = false;
		}
	}
	return changes;
}

std::vector<Change> PlaceNewRows(const std::vector<Row> &before, const std::vector<Row> &original)
{
	std::unordered_set<uint64_t> existing;
	existing.reserve(before.size());
	for (const Row &row : before)
		existing.insert(row.id);

	std::vector<Row> rows = original;
	size_t count = rows.size();
	// for each row, the first existing row at or after it
	std::vector<size_t> nextExisting(count + 1, count);
	for (size_t i = count; i > 0; i--) {
		nextExisting[i - 1] = existing.count(rows[i - 1].id) ? i - 1 : nextExisting[i];
	}

	std::vector<Change> changes;
	bool inGroup = false;
	unsigned char state = 0;
	for (size_t i = 0; i < count; i++) {
		const Row &row = rows[i];
		if (existing.count(row.id)) {
			if (row.tree == DESCRIPTION) {
				inGroup = true;
				state = 0;
			}
			else if (IsMember(row.tree)) {
				if (inGroup && !state)
					state = row.tree;
			}
			else {
				inGroup = false;
			}
			continue;
		}
		size_t next = nextExisting[i];
		// put between two lines of a group, the new line joins it
		if (inGroup && next < count && IsMember(rows[next].tree)) {
			if (!state)
				state = rows[next].tree;
			Set(changes, rows, i, state, VisibleAs(state, row.visible));
			continue;
		}
		if (row.tree == DESCRIPTION) {
			// a copy of a description with no lines of its own after it, such
			// as a duplicated description, is an ordinary line, not a second,
			// empty tree
			if (i + 1 < count && IsMember(rows[i + 1].tree)) {
				inGroup = true;
				state = 0;
			}
			else {
				Set(changes, rows, i, ORDINARY, Shown(row.visible));
				inGroup = false;
			}
		}
		else if (IsMember(row.tree)) {
			if (inGroup) {
				if (!state)
					state = row.tree;
				Set(changes, rows, i, state, VisibleAs(state, row.visible));
			}
			else {
				Set(changes, rows, i, ORDINARY, Shown(row.visible));
			}
		}
		else {
			inGroup = false;
		}
	}
	return changes;
}

size_t RowAfterGroup(const std::vector<Row> &rows, size_t row)
{
	if (row >= rows.size())
		return rows.size();
	size_t next = row + 1;
	if (rows[row].tree != DESCRIPTION)
		return next;
	while (next < rows.size() && IsMember(rows[next].tree))
		next++;
	return next;
}

Verdict Check(const std::vector<Row> &before, const std::vector<Row> &after,
	const std::vector<uint64_t> &movedIn)
{
	// the description of each member before; members out of every group are
	// already broken and only kept from getting worse
	std::unordered_map<uint64_t, uint64_t> groupOf;
	std::unordered_set<uint64_t> loose;
	{
		bool inGroup = false;
		uint64_t description = 0;
		for (const Row &row : before) {
			if (row.tree == DESCRIPTION) {
				inGroup = true;
				description = row.id;
			}
			else if (IsMember(row.tree)) {
				if (inGroup)
					groupOf.emplace(row.id, description);
				else
					loose.insert(row.id);
			}
			else {
				inGroup = false;
			}
		}
	}
	std::unordered_set<uint64_t> moved(movedIn.begin(), movedIn.end());

	Verdict verdict;
	// the group run each old group's members were found in
	std::unordered_map<uint64_t, size_t> runOf;
	bool inGroup = false;
	size_t run = 0;
	uint64_t description = 0;
	for (const Row &row : after) {
		if (row.tree == DESCRIPTION) {
			inGroup = true;
			run++;
			description = row.id;
			continue;
		}
		if (!IsMember(row.tree)) {
			inGroup = false;
			continue;
		}
		auto old = groupOf.find(row.id);
		bool wasInGroup = old != groupOf.end() && !moved.count(row.id);
		if (!inGroup) {
			if (wasInGroup)
				AddOnce(verdict.broken, old->second);
			else if (!loose.count(row.id))
				verdict.ok = false;
			continue;
		}
		if (!wasInGroup)
			continue;
		if (description != old->second) {
			AddOnce(verdict.broken, old->second);
			continue;
		}
		auto found = runOf.emplace(old->second, run);
		if (!found.second && found.first->second != run)
			AddOnce(verdict.broken, old->second);
	}
	if (!verdict.broken.empty())
		verdict.ok = false;
	return verdict;
}

Removal PlanRemove(const std::vector<Row> &rows, const std::vector<uint64_t> &descriptions)
{
	std::unordered_set<uint64_t> remove(descriptions.begin(), descriptions.end());
	Removal removal;
	bool removing = false;
	for (size_t i = 0; i < rows.size(); i++) {
		const Row &row = rows[i];
		if (row.tree == DESCRIPTION) {
			// a group is removed once, even if a copy of its description came along
			removing = remove.erase(row.id) > 0;
			if (removing)
				removal.deleted.push_back(i);
		}
		else if (IsMember(row.tree)) {
			if (removing)
				removal.ungrouped.push_back({ i, ORDINARY, Shown(row.visible) });
		}
		else {
			removing = false;
		}
	}
	return removal;
}

size_t RowAfterDelete(const std::vector<size_t> &deleted, size_t row)
{
	auto at = std::lower_bound(deleted.begin(), deleted.end(), row);
	if (at != deleted.end() && *at == row)
		return (size_t)-1;
	return row - (at - deleted.begin());
}

} // namespace LineGroups
