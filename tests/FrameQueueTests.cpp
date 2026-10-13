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
#include "../HikariSub/FrameQueue.h"

#include <cstring>
#include <thread>

namespace {

// A decoder that writes each frame's number into its slot, until the queue stops.
struct Decoder
{
	FrameQueue &queue;
	int failAt;
	std::thread thread;

	Decoder(FrameQueue &q, int fail = -1) : queue(q), failAt(fail), thread([this] { Run(); }) {}
	~Decoder()
	{
		queue.Stop();
		thread.join();
	}
	void Run()
	{
		int frame;
		while (unsigned char *slot = queue.NextToDecode(&frame)) {
			std::memcpy(slot, &frame, sizeof(frame));
			queue.Decoded(slot, frame != failAt);
		}
	}
};

int FrameIn(const unsigned char *slot)
{
	int frame;
	std::memcpy(&frame, slot, sizeof(frame));
	return frame;
}

} // namespace

TEST(frames_come_out_in_order)
{
	FrameQueue queue(3, sizeof(int), 100);
	queue.Reset(10);
	Decoder decoder(queue);
	for (int wanted = 10; wanted < 20; wanted++) {
		FrameQueue::Result r = queue.Take(wanted);
		CHECK(r.slot != nullptr);
		CHECK_EQ(r.frame, wanted);
		CHECK_EQ(FrameIn(r.slot), wanted);
		queue.Release(r.slot);
	}
}

TEST(taking_a_later_frame_drops_the_earlier_ones)
{
	FrameQueue queue(3, sizeof(int), 100);
	queue.Reset(0);
	Decoder decoder(queue);
	FrameQueue::Result r = queue.Take(0);
	queue.Release(r.slot);
	r = queue.Take(40);
	CHECK(r.slot != nullptr);
	CHECK_EQ(r.frame, 40);
	CHECK_EQ(FrameIn(r.slot), 40);
	queue.Release(r.slot);
	r = queue.Take(41);
	CHECK_EQ(r.frame, 41);
	queue.Release(r.slot);
}

TEST(reset_starts_over_at_the_new_frame)
{
	FrameQueue queue(3, sizeof(int), 100);
	queue.Reset(50);
	Decoder decoder(queue);
	FrameQueue::Result r = queue.Take(50);
	queue.Release(r.slot);
	queue.Reset(5);
	r = queue.Take(5);
	CHECK_EQ(r.frame, 5);
	CHECK_EQ(FrameIn(r.slot), 5);
	queue.Release(r.slot);
}

TEST(a_failed_frame_is_reported)
{
	FrameQueue queue(3, sizeof(int), 100);
	queue.Reset(0);
	Decoder decoder(queue, 2);
	for (int wanted = 0; wanted < 2; wanted++)
		queue.Release(queue.Take(wanted).slot);
	FrameQueue::Result r = queue.Take(2);
	CHECK(r.slot == nullptr);
	CHECK(r.failed);
}

TEST(decoding_stops_after_the_last_frame)
{
	FrameQueue queue(3, sizeof(int), 5);
	queue.Reset(3);
	Decoder decoder(queue);
	FrameQueue::Result r = queue.Take(4);
	CHECK_EQ(r.frame, 4);
	queue.Release(r.slot);
	r = queue.Take(5);
	CHECK(r.slot == nullptr);
	CHECK(!r.failed);
}

TEST(stop_wakes_a_waiting_taker)
{
	FrameQueue queue(3, sizeof(int), 100);
	queue.Reset(0);
	std::thread stopper([&] { queue.Stop(); });
	FrameQueue::Result r = queue.Take(0);
	stopper.join();
	CHECK(r.slot == nullptr);
}

TEST(a_stopped_queue_plays_again_after_reset)
{
	FrameQueue queue(3, sizeof(int), 100);
	queue.Reset(0);
	{
		Decoder decoder(queue);
		FrameQueue::Result r = queue.Take(0);
		queue.Release(r.slot);
	}
	queue.Reset(40);
	Decoder decoder(queue);
	FrameQueue::Result r = queue.Take(40);
	CHECK(r.slot != nullptr);
	CHECK_EQ(r.frame, 40);
	CHECK_EQ(FrameIn(r.slot), 40);
	queue.Release(r.slot);
}

namespace {

// Finishes each decoded frame by adding 1000 to the number in its slot,
// and tags it with the frame number plus 7.
struct Finisher
{
	FrameQueue &queue;
	std::thread thread;

	Finisher(FrameQueue &q) : queue(q), thread([this] { Run(); }) {}
	~Finisher()
	{
		queue.Stop();
		thread.join();
	}
	void Run()
	{
		int frame;
		while (unsigned char *slot = queue.NextToFinish(&frame)) {
			int value = FrameIn(slot) + 1000;
			std::memcpy(slot, &value, sizeof(value));
			queue.Finished(slot, frame + 7);
		}
	}
};

} // namespace

TEST(the_player_takes_only_finished_frames_in_order)
{
	FrameQueue queue(4, sizeof(int), 100, true);
	queue.Reset(10);
	Decoder decoder(queue);
	Finisher finisher(queue);
	for (int wanted = 10; wanted < 30; wanted++) {
		FrameQueue::Result r = queue.Take(wanted);
		CHECK(r.slot != nullptr);
		CHECK_EQ(r.frame, wanted);
		CHECK_EQ(FrameIn(r.slot), wanted + 1000);
		CHECK_EQ(r.tag, (unsigned)wanted + 7);
		queue.Release(r.slot);
	}
}

TEST(a_finished_queue_skips_to_a_later_frame)
{
	FrameQueue queue(4, sizeof(int), 100, true);
	queue.Reset(0);
	Decoder decoder(queue);
	Finisher finisher(queue);
	queue.Release(queue.Take(0).slot);
	FrameQueue::Result r = queue.Take(60);
	CHECK_EQ(r.frame, 60);
	CHECK_EQ(FrameIn(r.slot), 1060);
	queue.Release(r.slot);
	queue.Reset(3);
	r = queue.Take(3);
	CHECK_EQ(r.frame, 3);
	CHECK_EQ(FrameIn(r.slot), 1003);
	queue.Release(r.slot);
}

TEST(reset_drops_frames_waiting_to_be_finished)
{
	FrameQueue queue(2, sizeof(int), 100, true);
	queue.Reset(0);
	int frame = -1;
	unsigned char *a = queue.NextToDecode(&frame);
	CHECK_EQ(frame, 0);
	queue.Decoded(a, true);
	queue.Reset(5);
	unsigned char *b = queue.NextToDecode(&frame);
	CHECK_EQ(frame, 5);
	queue.Decoded(b, true);
	// frame 0 was dropped, so 5 is the one to finish
	CHECK(queue.NextToFinish(&frame) == b);
	CHECK_EQ(frame, 5);
	queue.Finished(b, 1);
	FrameQueue::Result r = queue.Take(5);
	CHECK(r.slot == b);
	CHECK_EQ(r.tag, 1u);
}

TEST(the_end_waits_for_frames_being_finished)
{
	FrameQueue queue(3, sizeof(int), 3, true);
	queue.Reset(1);
	Decoder decoder(queue);
	Finisher finisher(queue);
	FrameQueue::Result r = queue.Take(2);
	CHECK_EQ(r.frame, 2);
	CHECK_EQ(FrameIn(r.slot), 1002);
	queue.Release(r.slot);
	r = queue.Take(3);
	CHECK(r.slot == nullptr);
	CHECK(!r.failed);
}

TEST(slots_for_a_frame_size)
{
	CHECK_EQ(FrameQueue::SlotsFor(1920u * 1080 * 4), (size_t)8);
	CHECK_EQ(FrameQueue::SlotsFor(3840u * 2160 * 4), (size_t)8);
	CHECK_EQ(FrameQueue::SlotsFor(4000u * 3000 * 4), (size_t)5);
	CHECK_EQ(FrameQueue::SlotsFor(7680u * 4320 * 4), (size_t)4);
	CHECK_EQ(FrameQueue::SlotsFor(0), (size_t)8);
}

TEST(a_dropped_frame_is_never_taken)
{
	FrameQueue queue(2, sizeof(int), 100, true);
	queue.Reset(0);
	int frame = -1;
	unsigned char *a = queue.NextToDecode(&frame);
	queue.Decoded(a, true);
	unsigned char *b = queue.NextToDecode(&frame);
	queue.Decoded(b, true);
	CHECK(queue.NextToFinish(&frame) == a);
	queue.Drop(a);
	CHECK(queue.NextToFinish(&frame) == b);
	CHECK_EQ(frame, 1);
	queue.Finished(b, 3);
	// frame 0 is gone, so the player gets the next one
	FrameQueue::Result r = queue.Take(0);
	CHECK(r.slot == b);
	CHECK_EQ(r.frame, 1);
}
