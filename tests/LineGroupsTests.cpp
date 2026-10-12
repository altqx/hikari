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
#include "../HikariSub/LineGroups.h"

#include <algorithm>

using namespace LineGroups;

namespace {

// lines named by id: a description, a closed or open member, an ordinary line
Row D(uint64_t id) { return { id, DESCRIPTION, SHOWN }; }
Row C(uint64_t id) { return { id, CLOSED, HIDDEN }; }
Row O(uint64_t id) { return { id, OPENED, SHOWN }; }
Row X(uint64_t id) { return { id, ORDINARY, SHOWN }; }
Row Hidden(Row row)
{
	row.visible = HIDDEN;
	return row;
}
Row Shown(Row row)
{
	row.visible = SHOWN;
	return row;
}

const Change *ChangeOf(const std::vector<Change> &changes, size_t row)
{
	for (const Change &change : changes) {
		if (change.row == row)
			return &change;
	}
	return nullptr;
}

std::vector<Row> Applied(std::vector<Row> rows, const std::vector<Change> &changes)
{
	for (const Change &change : changes) {
		rows[change.row].tree = change.tree;
		rows[change.row].visible = change.visible;
	}
	return rows;
}

bool Breaks(const Verdict &verdict, uint64_t description)
{
	return std::find(verdict.broken.begin(), verdict.broken.end(), description) != verdict.broken.end();
}

} // namespace

TEST(repair_keeps_whole_groups)
{
	// adjacent groups, an empty description and a group at the end
	std::vector<Row> rows = { X(1), D(2), C(3), C(4), D(5), O(6), D(7), X(8), D(9), O(10) };
	CHECK(Repair(rows).empty());
}

TEST(repair_makes_members_with_no_description_ordinary)
{
	std::vector<Change> changes = Repair({ X(1), C(2), O(3), D(4), O(5) });
	CHECK_EQ(changes.size(), 2u);
	const Change *closed = ChangeOf(changes, 1);
	CHECK(closed && closed->tree == ORDINARY && closed->visible == SHOWN);
	const Change *open = ChangeOf(changes, 2);
	CHECK(open && open->tree == ORDINARY && open->visible == SHOWN);
	CHECK(!ChangeOf(changes, 4));
}

TEST(repair_gives_a_group_its_first_members_state)
{
	std::vector<Change> changes = Repair({ D(1), C(2), O(3), C(4), D(5), O(6), C(7) });
	CHECK_EQ(changes.size(), 2u);
	const Change *toClosed = ChangeOf(changes, 2);
	CHECK(toClosed && toClosed->tree == CLOSED && toClosed->visible == HIDDEN);
	const Change *toOpen = ChangeOf(changes, 6);
	CHECK(toOpen && toOpen->tree == OPENED && toOpen->visible == SHOWN);
}

TEST(repair_matches_member_visibility_to_state)
{
	std::vector<Change> changes = Repair({ D(1), Shown(C(2)), D(3), Hidden(O(4)) });
	CHECK_EQ(changes.size(), 2u);
	const Change *closed = ChangeOf(changes, 1);
	CHECK(closed && closed->tree == CLOSED && closed->visible == HIDDEN);
	const Change *open = ChangeOf(changes, 3);
	CHECK(open && open->tree == OPENED && open->visible == SHOWN);
}

TEST(new_line_put_inside_a_group_joins_it)
{
	std::vector<Row> before = { D(1), O(2), O(3), X(4) };
	std::vector<Change> changes = PlaceNewRows(before, { D(1), O(2), X(9), O(3), X(4) });
	CHECK_EQ(changes.size(), 1u);
	const Change *joined = ChangeOf(changes, 2);
	CHECK(joined && joined->tree == OPENED && joined->visible == SHOWN);

	// right after the description, in a closed group: hidden like the others
	before = { D(1), C(2), X(4) };
	changes = PlaceNewRows(before, { D(1), X(9), C(2), X(4) });
	const Change *hidden = ChangeOf(changes, 1);
	CHECK(hidden && hidden->tree == CLOSED && hidden->visible == HIDDEN);
}

TEST(new_description_put_inside_a_group_becomes_a_member)
{
	// a pasted description cannot take the members after it from their group
	std::vector<Row> before = { D(1), O(2), O(3) };
	std::vector<Change> changes = PlaceNewRows(before, { D(1), O(2), D(8), C(9), O(3) });
	const Change *description = ChangeOf(changes, 2);
	CHECK(description && description->tree == OPENED && description->visible == SHOWN);
	const Change *member = ChangeOf(changes, 3);
	CHECK(member && member->tree == OPENED && member->visible == SHOWN);
}

TEST(new_member_right_after_a_group_extends_it)
{
	std::vector<Row> before = { D(1), O(2), X(4) };
	CHECK(PlaceNewRows(before, { D(1), O(2), O(9), X(4) }).empty());
	// a copy of a closed member pasted after an open group is open
	std::vector<Change> changes = PlaceNewRows(before, { D(1), O(2), C(9), X(4) });
	const Change *opened = ChangeOf(changes, 2);
	CHECK(opened && opened->tree == OPENED && opened->visible == SHOWN);
}

TEST(new_member_outside_every_group_becomes_ordinary)
{
	std::vector<Row> before = { X(1), D(2), O(3) };
	std::vector<Change> changes = PlaceNewRows(before, { C(9), X(1), D(2), O(3) });
	CHECK_EQ(changes.size(), 1u);
	const Change *ordinary = ChangeOf(changes, 0);
	CHECK(ordinary && ordinary->tree == ORDINARY && ordinary->visible == SHOWN);
}

TEST(new_whole_group_is_kept)
{
	std::vector<Row> before = { X(1), D(2), O(3) };
	CHECK(PlaceNewRows(before, { X(1), D(8), C(9), D(2), O(3) }).empty());
	CHECK(PlaceNewRows(before, { X(1), D(2), O(3), D(8), C(9) }).empty());
}

TEST(a_line_put_after_a_description_goes_after_its_tree)
{
	std::vector<Row> closed = { X(1), D(2), C(3), C(4), X(5) };
	CHECK_EQ(RowAfterGroup(closed, 1), 4u);
	std::vector<Row> open = { X(1), D(2), O(3), O(4), X(5) };
	CHECK_EQ(RowAfterGroup(open, 1), 4u);
	// a tree at the end of the file, an empty tree, a tree right before another
	CHECK_EQ(RowAfterGroup({ D(2), C(3) }, 0), 2u);
	CHECK_EQ(RowAfterGroup({ D(2), X(5) }, 0), 1u);
	CHECK_EQ(RowAfterGroup({ D(2), O(3), D(6), O(7) }, 0), 2u);
	// any other line: right after it
	CHECK_EQ(RowAfterGroup(closed, 0), 1u);
	CHECK_EQ(RowAfterGroup(open, 2), 3u);
}

TEST(a_duplicated_description_is_an_ordinary_line_after_a_closed_tree)
{
	std::vector<Row> before = { X(1), D(2), C(3), C(4), X(5) };
	size_t at = RowAfterGroup(before, 1);
	std::vector<Row> after = before;
	after.insert(after.begin() + at, D(9));
	std::vector<Change> changes = PlaceNewRows(before, after);
	CHECK_EQ(changes.size(), 1u);
	const Change *copy = ChangeOf(changes, at);
	CHECK(copy && copy->tree == ORDINARY && copy->visible == SHOWN);
	// the tree keeps its lines, closed and hidden
	CHECK(Check(before, Applied(after, changes)).ok);
}

TEST(a_duplicated_description_is_an_ordinary_line_after_an_open_tree)
{
	std::vector<Row> before = { X(1), D(2), O(3), O(4), X(5) };
	size_t at = RowAfterGroup(before, 1);
	std::vector<Row> after = before;
	after.insert(after.begin() + at, D(9));
	std::vector<Change> changes = PlaceNewRows(before, after);
	CHECK_EQ(changes.size(), 1u);
	const Change *copy = ChangeOf(changes, at);
	CHECK(copy && copy->tree == ORDINARY && copy->visible == SHOWN);
	CHECK(Check(before, Applied(after, changes)).ok);
}

TEST(a_duplicated_description_next_to_trees_is_an_ordinary_line)
{
	// before the original description, at the end of the file, and between
	// a closed tree and the next tree
	std::vector<Row> before = { D(2), C(3), D(6), O(7) };
	std::vector<Row> after = { D(9), D(2), C(3), D(6), O(7) };
	std::vector<Change> changes = PlaceNewRows(before, after);
	const Change *first = ChangeOf(changes, 0);
	CHECK(changes.size() == 1u && first && first->tree == ORDINARY && first->visible == SHOWN);
	CHECK(Check(before, Applied(after, changes)).ok);

	after = { D(2), C(3), D(9), D(6), O(7) };
	changes = PlaceNewRows(before, after);
	const Change *between = ChangeOf(changes, 2);
	CHECK(changes.size() == 1u && between && between->tree == ORDINARY && between->visible == SHOWN);
	CHECK(Check(before, Applied(after, changes)).ok);

	after = { D(2), C(3), D(6), O(7), D(9) };
	changes = PlaceNewRows(before, after);
	const Change *last = ChangeOf(changes, 4);
	CHECK(changes.size() == 1u && last && last->tree == ORDINARY && last->visible == SHOWN);
	CHECK(Check(before, Applied(after, changes)).ok);
}

TEST(a_line_inserted_after_a_description_next_to_a_tree)
{
	// Insert after a closed tree's description: an ordinary line after the
	// hidden lines stays ordinary and shown
	std::vector<Row> closed = { D(2), C(3), C(4), X(5) };
	std::vector<Row> after = closed;
	after.insert(after.begin() + RowAfterGroup(closed, 0), X(9));
	CHECK(PlaceNewRows(closed, after).empty());
	CHECK(Check(closed, after).ok);
	// after an open tree's description it is a line of the tree, shown
	std::vector<Row> open = { D(2), O(3), X(5) };
	after = { D(2), X(9), O(3), X(5) };
	std::vector<Change> changes = PlaceNewRows(open, after);
	const Change *joined = ChangeOf(changes, 1);
	CHECK(changes.size() == 1u && joined && joined->tree == OPENED && joined->visible == SHOWN);
	CHECK(Check(open, Applied(after, changes)).ok);
}

TEST(new_lines_leave_existing_lines_alone)
{
	// the orphan 3 is existing; only the check may refuse it
	std::vector<Row> before = { X(1), D(2), O(3) };
	CHECK(PlaceNewRows(before, { O(3), X(1), D(2) }).empty());
}

TEST(edits_inside_a_group_are_allowed)
{
	std::vector<Row> before = { D(1), O(2), O(3), X(4) };
	CHECK(Check(before, { D(1), O(3), O(2), X(4) }).ok);
	// a duplicated member, a deleted member, every member deleted
	CHECK(Check(before, { D(1), O(2), O(9), O(3), X(4) }).ok);
	CHECK(Check(before, { D(1), O(3), X(4) }).ok);
	CHECK(Check(before, { D(1), X(4) }).ok);
	// the whole group moved, or deleted
	CHECK(Check(before, { X(4), D(1), O(2), O(3) }).ok);
	CHECK(Check(before, { X(4) }).ok);
}

TEST(moving_a_member_out_of_its_group_is_refused)
{
	std::vector<Row> before = { D(1), O(2), O(3), X(4) };
	Verdict verdict = Check(before, { D(1), O(2), X(4), O(3) });
	CHECK(!verdict.ok);
	CHECK(Breaks(verdict, 1));
	CHECK_EQ(verdict.broken.size(), 1u);
}

TEST(an_ordinary_line_moved_into_a_group_is_refused)
{
	std::vector<Row> before = { X(4), D(1), O(2), O(3) };
	Verdict verdict = Check(before, { D(1), O(2), X(4), O(3) });
	CHECK(!verdict.ok);
	CHECK(Breaks(verdict, 1));
}

TEST(deleting_or_moving_a_description_alone_is_refused)
{
	std::vector<Row> before = { X(4), D(1), C(2), C(3) };
	Verdict deleted = Check(before, { X(4), C(2), C(3) });
	CHECK(!deleted.ok);
	CHECK(Breaks(deleted, 1));
	Verdict moved = Check(before, { D(1), X(4), C(2), C(3) });
	CHECK(!moved.ok);
	CHECK(Breaks(moved, 1));
}

TEST(a_member_moved_into_another_group_is_refused)
{
	std::vector<Row> before = { D(1), O(2), D(5), O(6) };
	Verdict verdict = Check(before, { D(1), D(5), O(2), O(6) });
	CHECK(!verdict.ok);
	CHECK(Breaks(verdict, 1));
	CHECK(!Breaks(verdict, 5));
}

TEST(deleting_a_description_between_groups_is_refused)
{
	// with the same state the members would silently join the group before
	std::vector<Row> before = { D(1), C(2), D(5), C(6) };
	Verdict verdict = Check(before, { D(1), C(2), C(6) });
	CHECK(!verdict.ok);
	CHECK(Breaks(verdict, 5));
	CHECK(!Breaks(verdict, 1));
}

TEST(a_group_split_in_two_is_refused)
{
	// two runs after copies of one description
	std::vector<Row> before = { D(1), O(2), O(3) };
	Verdict verdict = Check(before, { D(1), O(2), D(1), O(3) });
	CHECK(!verdict.ok);
	CHECK(Breaks(verdict, 1));
}

TEST(lines_moved_into_a_group_on_purpose_may_leave_theirs)
{
	std::vector<Row> before = { D(1), O(2), D(5), O(6), O(7) };
	std::vector<Row> after = { D(1), O(2), O(6), D(5), O(7) };
	CHECK(!Check(before, after).ok);
	CHECK(Check(before, after, { 6 }).ok);
}

TEST(a_new_group_left_with_a_line_outside_fails_without_a_group_to_remove)
{
	// A and B made a tree around a hidden ordinary line between them
	std::vector<Row> before = { X(1), Hidden(X(2)), X(3) };
	Verdict verdict = Check(before, { D(9), C(1), Hidden(X(2)), C(3) });
	CHECK(!verdict.ok);
	CHECK(verdict.broken.empty());
}

TEST(lines_that_were_already_outside_every_group_are_not_blamed)
{
	std::vector<Row> before = { X(1), C(2) };
	CHECK(Check(before, { C(2), X(1) }).ok);
}

TEST(removing_a_group_deletes_its_description_and_ungroups_members)
{
	std::vector<Row> rows = { X(1), D(2), C(3), C(4), D(5), O(6) };
	Removal removal = PlanRemove(rows, { 2 });
	CHECK_EQ(removal.deleted.size(), 1u);
	CHECK_EQ(removal.deleted[0], 1u);
	CHECK_EQ(removal.ungrouped.size(), 2u);
	const Change *first = ChangeOf(removal.ungrouped, 2);
	CHECK(first && first->tree == ORDINARY && first->visible == SHOWN);
	CHECK(ChangeOf(removal.ungrouped, 3));
	CHECK(!ChangeOf(removal.ungrouped, 5));

	Removal both = PlanRemove(rows, { 5, 2 });
	CHECK_EQ(both.deleted.size(), 2u);
	CHECK_EQ(both.ungrouped.size(), 3u);

	Removal empty = PlanRemove({ D(2), X(1) }, { 2 });
	CHECK_EQ(empty.deleted.size(), 1u);
	CHECK(empty.ungrouped.empty());
}

TEST(rows_after_deleted_rows_move_up)
{
	std::vector<size_t> deleted = { 1, 4 };
	CHECK_EQ(RowAfterDelete(deleted, 0), 0u);
	CHECK_EQ(RowAfterDelete(deleted, 1), (size_t)-1);
	CHECK_EQ(RowAfterDelete(deleted, 2), 1u);
	CHECK_EQ(RowAfterDelete(deleted, 5), 3u);
}

TEST(a_removed_group_no_longer_blocks_the_edit)
{
	// delete a description: refused, then allowed once its group is removed
	std::vector<Row> before = { X(4), D(1), C(2), C(3) };
	CHECK(!Check(before, { X(4), C(2), C(3) }).ok);
	std::vector<Row> removed = { X(4), X(2), X(3) };
	CHECK(Check(removed, { X(4), X(3) }).ok);
}
