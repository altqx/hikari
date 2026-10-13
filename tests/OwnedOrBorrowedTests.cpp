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
#include "../HikariSub/OwnedOrBorrowed.h"

namespace {

// Counts its deletions, so a test sees what was freed.
struct Tracked {
	explicit Tracked(int &deletions) : deletions(deletions) {}
	~Tracked() { ++deletions; }
	int &deletions;
};

} // namespace

TEST(an_owned_object_is_deleted_when_let_go_of)
{
	int deletions = 0;
	OwnedOrBorrowed<Tracked> held;
	held.Own(new Tracked(deletions));
	CHECK(held.IsOwned());
	held.Reset();
	CHECK_EQ(deletions, 1);
	CHECK(held.Get() == nullptr);
}

TEST(a_borrowed_object_outlives_its_holder_letting_go)
{
	// audio from a video that fails to load: the video still frees its provider
	int deletions = 0;
	Tracked *video = new Tracked(deletions);
	{
		OwnedOrBorrowed<Tracked> held;
		held.Borrow(video);
		CHECK(!held.IsOwned());
		held.Reset();
		CHECK(held.Get() == nullptr);
		held.Borrow(video);
	}
	CHECK_EQ(deletions, 0);
	delete video;
	CHECK_EQ(deletions, 1);
}

TEST(holding_another_object_lets_go_of_the_last_one)
{
	int ownDeletions = 0;
	int videoDeletions = 0;
	Tracked video(videoDeletions);
	OwnedOrBorrowed<Tracked> held;
	held.Own(new Tracked(ownDeletions));
	held.Borrow(&video);
	CHECK_EQ(ownDeletions, 1);
	held.Own(new Tracked(ownDeletions));
	CHECK_EQ(videoDeletions, 0);
	held.Reset();
	CHECK_EQ(ownDeletions, 2);
}

TEST(owning_the_borrowed_object_takes_it_over)
{
	// a provider made for the audio alone is handed over by the video
	int deletions = 0;
	Tracked *handed = new Tracked(deletions);
	OwnedOrBorrowed<Tracked> held;
	held.Borrow(handed);
	held.Own(handed);
	CHECK_EQ(deletions, 0);
	CHECK(held.Get() == handed);
	held.Reset();
	CHECK_EQ(deletions, 1);
}

TEST(only_a_held_borrowed_object_is_borrowed)
{
	int deletions = 0;
	Tracked video(deletions);
	Tracked other(deletions);
	OwnedOrBorrowed<Tracked> held;
	CHECK(!held.Borrows(&video));
	CHECK(!held.Borrows(nullptr));
	held.Borrow(&video);
	CHECK(held.Borrows(&video));
	CHECK(!held.Borrows(&other));
	held.Own(new Tracked(deletions));
	CHECK(!held.Borrows(held.Get()));
	held.Reset();
	CHECK_EQ(deletions, 1);
}

TEST(the_holder_reads_through_to_the_object)
{
	int deletions = 0;
	OwnedOrBorrowed<Tracked> held;
	CHECK(!held);
	held.Own(new Tracked(deletions));
	CHECK(held);
	CHECK(&held->deletions == &deletions);
	Tracked *plain = held;
	CHECK(plain == held.Get());
}
