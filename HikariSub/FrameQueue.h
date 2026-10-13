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

#include <condition_variable>
#include <mutex>
#include <vector>

// Frames a decoder thread decodes ahead of playback. The decoder asks for
// the next frame to decode and a slot to put it in; the player takes the
// frame it wants, and every earlier frame is dropped and never decoded.
// With finishing on, a second thread finishes each decoded frame, in order,
// before the player can take it: it draws the subtitles on it.
class FrameQueue
{
public:
	struct Result
	{
		// nullptr at the end of the video, on a failed frame or when stopped
		unsigned char *slot = nullptr;
		int frame = -1;
		bool failed = false;
		// what the finishing thread passed to Finished
		unsigned tag = 0;
	};

	FrameQueue(size_t slots, size_t frameBytes, int frameCount, bool finishing = false);

	// How many frames to decode ahead: 8, fewer for frames so large that
	// 8 would take more than 256 MB, and never fewer than 4.
	static size_t SlotsFor(size_t frameBytes);

	// Decoder side: waits for a free slot and returns it with the frame to
	// decode into it, or nullptr once stopped.
	unsigned char *NextToDecode(int *frame);
	void Decoded(unsigned char *slot, bool ok);

	// Finishing side: waits for the next decoded frame, or nullptr once stopped.
	unsigned char *NextToFinish(int *frame);
	void Finished(unsigned char *slot, unsigned tag);
	// a frame not worth finishing, as it would be shown too late
	void Drop(unsigned char *slot);

	// Player side: waits for wanted, or the first later frame decoded.
	Result Take(int wanted);
	void Release(unsigned char *slot);

	// Decoding starts over at frame, as after a seek or when playing again.
	void Reset(int frame);
	void Stop();
	size_t FrameBytes() const { return m_slots.empty() ? 0 : m_slots[0].data.size(); }
	int FrameCount() const { return m_frameCount; }
	bool Finishing() const { return m_finishing; }

private:
	enum State { FREE, DECODING, DECODED, FINISHING, READY, TAKEN };
	struct Slot
	{
		std::vector<unsigned char> data;
		int frame = -1;
		State state = FREE;
		unsigned generation = 0;
		unsigned tag = 0;
	};

	Slot *Find(unsigned char *data);

	std::mutex m_mutex;
	std::condition_variable m_changed;
	std::vector<Slot> m_slots;
	int m_frameCount;
	bool m_finishing;
	int m_next = 0;
	int m_wanted = 0;
	unsigned m_generation = 0;
	bool m_failed = false;
	bool m_stopped = false;
};
