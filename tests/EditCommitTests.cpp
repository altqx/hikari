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
#include "../HikariSub/EditCommit.h"

TEST(apply_with_nothing_changed_does_nothing)
{
	// no undo step, no re-sync of the visual tool or the audio display
	CHECK(PlanEditCommit(false, 1000, 2000) == EditCommit::NOTHING);
	// a Line that already ends before it starts, unchanged: nothing to
	// send, so nothing to refuse either
	CHECK(PlanEditCommit(false, 2000, 1000) == EditCommit::NOTHING);
}

TEST(a_commit_ending_before_its_start_is_refused)
{
	CHECK(PlanEditCommit(true, 7000, 6500) == EditCommit::REFUSED);
	CHECK(PlanEditCommit(true, 1, 0) == EditCommit::REFUSED);
}

TEST(typing_an_end_before_start_waits_for_the_commit)
{
	// with the default "save after 1 character" every keystroke is saved;
	// an End before the Start is not, or Apply would find nothing to refuse
	CHECK(!SaveWhileTyping(7000, 6500));
	CHECK(SaveWhileTyping(1000, 2000));
	CHECK(SaveWhileTyping(6500, 6500));
}

TEST(a_commit_with_valid_times_is_sent)
{
	CHECK(PlanEditCommit(true, 1000, 2000) == EditCommit::SEND);
	// a Line of no duration is valid
	CHECK(PlanEditCommit(true, 6500, 6500) == EditCommit::SEND);
}
