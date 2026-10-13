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
#include "../HikariSub/FrameExchange.h"

#include <atomic>
#include <cstring>
#include <thread>

namespace {

void PublishFilled(FrameExchange &exchange, unsigned char value, int time, int width = 4, int height = 2)
{
	unsigned char *buffer = exchange.BeginWrite(width, height, width * 4);
	std::memset(buffer, value, (size_t)width * height * 4);
	exchange.Publish(time);
}

bool FilledWith(const FrameExchange::Frame &frame, unsigned char value)
{
	if (!frame.data)
		return false;
	for (int i = 0; i < frame.height * frame.pitch; i++) {
		if (frame.data[i] != value)
			return false;
	}
	return true;
}

} // namespace

TEST(nothing_is_on_screen_at_first)
{
	FrameExchange exchange;
	CHECK(!exchange.TakeLatest());
	CHECK(exchange.Front().data == nullptr);
}

TEST(a_published_frame_comes_to_the_front)
{
	FrameExchange exchange;
	PublishFilled(exchange, 7, 1000, 8, 3);
	CHECK(exchange.Front().data == nullptr);
	CHECK(exchange.TakeLatest());
	FrameExchange::Frame front = exchange.Front();
	CHECK(FilledWith(front, 7));
	CHECK_EQ(front.time, 1000);
	CHECK_EQ(front.width, 8);
	CHECK_EQ(front.height, 3);
	CHECK_EQ(front.pitch, 32);
	CHECK(!exchange.TakeLatest());
}

TEST(the_newest_frame_wins)
{
	FrameExchange exchange;
	PublishFilled(exchange, 1, 10);
	PublishFilled(exchange, 2, 20);
	PublishFilled(exchange, 3, 30);
	CHECK(exchange.TakeLatest());
	CHECK(FilledWith(exchange.Front(), 3));
	CHECK_EQ(exchange.Front().time, 30);
}

TEST(writing_the_next_frame_leaves_the_front_alone)
{
	FrameExchange exchange;
	PublishFilled(exchange, 1, 10);
	CHECK(exchange.TakeLatest());
	// both buffers the producer may use next are not the one on screen
	PublishFilled(exchange, 2, 20);
	unsigned char *next = exchange.BeginWrite(4, 2, 16);
	std::memset(next, 3, 32);
	CHECK(FilledWith(exchange.Front(), 1));
	exchange.Publish(30);
	CHECK(exchange.TakeLatest());
	CHECK(FilledWith(exchange.Front(), 3));
}

TEST(a_copy_shown_drops_the_frame_not_taken)
{
	FrameExchange exchange;
	PublishFilled(exchange, 1, 10);
	unsigned char copy[32];
	std::memset(copy, 9, sizeof(copy));
	exchange.ShowCopy(copy, 4, 2, 16, 50);
	CHECK(FilledWith(exchange.Front(), 9));
	CHECK_EQ(exchange.Front().time, 50);
	CHECK(!exchange.TakeLatest());
	CHECK(FilledWith(exchange.Front(), 9));
}

TEST(the_serial_changes_with_the_front)
{
	FrameExchange exchange;
	unsigned serial = exchange.Front().serial;
	PublishFilled(exchange, 1, 10);
	CHECK_EQ(exchange.Front().serial, serial);
	CHECK(exchange.TakeLatest());
	CHECK(exchange.Front().serial != serial);
	serial = exchange.Front().serial;
	unsigned char copy[32] = {};
	exchange.ShowCopy(copy, 4, 2, 16, 0);
	CHECK(exchange.Front().serial != serial);
	serial = exchange.Front().serial;
	exchange.Clear();
	CHECK(exchange.Front().serial != serial);
	CHECK(exchange.Front().data == nullptr);
}

TEST(a_new_size_comes_with_its_frame)
{
	FrameExchange exchange;
	PublishFilled(exchange, 1, 10, 4, 2);
	CHECK(exchange.TakeLatest());
	PublishFilled(exchange, 2, 20, 16, 9);
	CHECK(exchange.TakeLatest());
	FrameExchange::Frame front = exchange.Front();
	CHECK_EQ(front.width, 16);
	CHECK_EQ(front.height, 9);
	CHECK(FilledWith(front, 2));
}

TEST(frames_from_another_thread_arrive_whole_and_in_order)
{
	FrameExchange exchange;
	const int frames = 2000;
	std::atomic<bool> done{ false };
	std::thread producer([&] {
		for (int i = 1; i <= frames; i++)
			PublishFilled(exchange, (unsigned char)(i & 0xFF), i, 64, 16);
		done = true;
	});
	int last = 0;
	bool whole = true, ordered = true;
	int seen = 0;
	for (;;) {
		bool finished = done;
		if (!exchange.TakeLatest()) {
			if (finished)
				break;
			continue;
		}
		FrameExchange::Frame front = exchange.Front();
		whole = whole && FilledWith(front, (unsigned char)(front.time & 0xFF));
		ordered = ordered && front.time > last;
		last = front.time;
		seen++;
	}
	producer.join();
	CHECK(whole);
	CHECK(ordered);
	CHECK(seen > 0);
	CHECK_EQ(exchange.Front().time, frames);
}
