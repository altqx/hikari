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

#include <chrono>
#include <optional>
#include <utility>

// While the seek bar's thumb is held and dragged, the video seeks at most once
// every 50 ms, with every player. The first move seeks at once; a move within
// the interval waits, and only the newest waiting time is sought once the
// interval is up. The release seeks the time it lands on unless that time was
// sought within the last interval. Before, DirectShow (and GStreamer, its
// stand-in on Linux) sought only on the release, showing nothing while
// dragging, and FFMS2 sought on every mouse move, which could outrun the
// decoder. A click beside the thumb still seeks at once, without this.
//
// Times are passed in, so the policy is tested without a clock; the seek bar
// runs it with a timer for the waiting time.
class SeekThrottle
{
public:
	using Clock = std::chrono::steady_clock;
	static constexpr std::chrono::milliseconds Interval{ 50 };

	explicit SeekThrottle(std::chrono::milliseconds seekInterval = Interval) : interval(seekInterval) {}

	// The held thumb moved to time ms at now: the time to seek now, if any.
	std::optional<int> Move(int ms, Clock::time_point now)
	{
		last = ms;
		if (sent == ms) {
			waiting.reset(); // back on the time sought last: nothing to seek
			return std::nullopt;
		}
		if (!sentAt || now - *sentAt >= interval)
			return Send(ms, now);
		waiting = ms;
		return std::nullopt;
	}

	// When the waiting time is due; nothing when none waits.
	std::optional<Clock::time_point> Due() const
	{
		if (!waiting || !sentAt)
			return std::nullopt;
		return *sentAt + interval;
	}

	// The waiting time once due at now: the time to seek now, if any.
	std::optional<int> Tick(Clock::time_point now)
	{
		const auto at = Due();
		if (!at || now < *at)
			return std::nullopt;
		return Send(*waiting, now);
	}

	// The thumb was let go at now: the time to seek now (the last move's), if
	// any. The drag is over; the next move starts a new one.
	std::optional<int> Release(Clock::time_point now)
	{
		const auto lastMove = std::exchange(last, std::nullopt);
		const bool current = lastMove && sent == lastMove && sentAt && now - *sentAt < interval;
		sent.reset();
		sentAt.reset();
		waiting.reset();
		if (!lastMove || current)
			return std::nullopt;
		return lastMove;
	}

	// A time is waiting for its seek.
	bool Waiting() const { return waiting.has_value(); }

private:
	std::optional<int> Send(int ms, Clock::time_point now)
	{
		sent = ms;
		sentAt = now;
		waiting.reset();
		return ms;
	}

	std::chrono::milliseconds interval;
	std::optional<Clock::time_point> sentAt; // the last seek of this drag
	std::optional<int> sent;                 // its time
	std::optional<int> waiting;              // a newer time not yet sought
	std::optional<int> last;                 // the last move's time
};
