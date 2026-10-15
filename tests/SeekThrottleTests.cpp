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
#include "../HikariSub/SeekThrottle.h"

#include <vector>

using namespace std::chrono_literals;

namespace {

using Clock = SeekThrottle::Clock;

// Runs a drag the way the seek bar does: each move, then the waiting time
// whenever the clock passes its due time (the seek bar's timer).
struct Drag {
	SeekThrottle throttle;
	Clock::time_point now{};
	std::vector<int> seeks;
	void Record(std::optional<int> ms)
	{
		if (ms)
			seeks.push_back(*ms);
	}
	void Advance(std::chrono::milliseconds by)
	{
		const auto until = now + by;
		while (const auto due = throttle.Due()) {
			if (*due > until)
				break;
			now = *due;
			Record(throttle.Tick(now));
		}
		now = until;
	}
	void Move(int ms) { Record(throttle.Move(ms, now)); }
	void Release() { Record(throttle.Release(now)); }
};

} // namespace

TEST(seek_bar_drag_seeks_at_most_every_50_ms_and_where_it_is_let_go)
{
	Drag drag;
	// a drag over 1000 ms of video in one second, a mouse move every 2 ms
	for (int i = 0; i < 500; ++i) {
		drag.Move(i * 2);
		drag.Advance(2ms);
	}
	drag.Move(997); // the release's own position
	drag.Release();
	// one seek per 50 ms for 1000 ms, 20 or 21, and the release's
	CHECK(drag.seeks.size() >= 20u);
	CHECK(drag.seeks.size() <= 22u);
	CHECK_EQ(drag.seeks.front(), 0); // the first move at once
	CHECK_EQ(drag.seeks.back(), 997);
	// each throttled seek is the newest time moved to by then
	for (size_t i = 1; i + 1 < drag.seeks.size(); ++i)
		CHECK(drag.seeks[i] > drag.seeks[i - 1]);
	CHECK(!drag.throttle.Waiting());
}

TEST(seek_bar_drag_seeks_the_first_move_at_once_and_the_newest_when_due)
{
	Drag drag;
	drag.Move(10);
	CHECK(drag.seeks == std::vector<int>{ 10 });
	drag.Advance(10ms);
	drag.Move(11);
	drag.Advance(10ms);
	drag.Move(12); // replaces 11
	CHECK(drag.seeks == std::vector<int>{ 10 });
	CHECK(drag.throttle.Due().has_value());
	CHECK(*drag.throttle.Due() == Clock::time_point{} + 50ms);
	drag.Advance(29ms);
	CHECK(drag.seeks == std::vector<int>{ 10 });
	drag.Advance(1ms);
	CHECK((drag.seeks == std::vector<int>{ 10, 12 }));
	// a move after a quiet interval seeks at once
	drag.Advance(100ms);
	drag.Move(20);
	CHECK((drag.seeks == std::vector<int>{ 10, 12, 20 }));
}

TEST(seek_bar_release_seeks_its_time_unless_just_sought)
{
	// pressed on the thumb and let go at once: the press's move seeks, the
	// release does not seek the same time again
	Drag click;
	click.Move(30);
	click.Advance(5ms);
	click.Release();
	CHECK(click.seeks == std::vector<int>{ 30 });
	// let go with a time waiting: the waiting seek is dropped, the release's
	// time sought at once
	Drag waiting;
	waiting.Move(1);
	waiting.Advance(5ms);
	waiting.Move(2);
	waiting.Advance(5ms);
	waiting.Move(3);
	waiting.Release();
	CHECK((waiting.seeks == std::vector<int>{ 1, 3 }));
	CHECK(!waiting.throttle.Due().has_value());
	// held still on the last time sought for longer than the interval (a
	// playing video has moved on): the release seeks it again
	Drag held;
	held.Move(40);
	held.Advance(300ms);
	held.Release();
	CHECK((held.seeks == std::vector<int>{ 40, 40 }));
	// back on the time sought last before a waiting one is due: nothing waits
	Drag back;
	back.Move(5);
	back.Advance(10ms);
	back.Move(6);
	back.Move(5);
	CHECK(!back.throttle.Waiting());
	back.Advance(100ms);
	CHECK(back.seeks == std::vector<int>{ 5 });
	// a release with no move seeks nothing; the next drag starts afresh
	Drag none;
	none.Release();
	CHECK(none.seeks.empty());
	none.Move(7);
	none.Advance(1ms);
	none.Release();
	none.Move(8);
	CHECK((none.seeks == std::vector<int>{ 7, 8 }));
}
