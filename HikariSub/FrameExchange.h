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
#include <mutex>
#include <vector>

// Hands finished video frames from the thread that makes them to the one
// that shows them, through three buffers: the producer fills one while the
// other two hold the newest finished frame and the frame on screen. Neither
// side waits for the other longer than it takes to swap two indices, so a
// slow paint never holds up the next frame, and a frame that is not taken
// before the next one is published is dropped.
//
// One producer thread at a time writes; the consumer (the UI thread) takes,
// shows copies and reads the front buffer.
class FrameExchange
{
public:
	struct Frame
	{
		const unsigned char *data = nullptr;
		int width = 0;
		int height = 0;
		int pitch = 0;
		int time = -1;
		// changes whenever the front frame does
		unsigned serial = 0;
	};

	// Producer: a buffer of height * pitch bytes to fill, the producer's
	// own until Publish.
	unsigned char *BeginWrite(int width, int height, int pitch);
	// Producer: the buffer from BeginWrite becomes the newest frame.
	void Publish(int time);

	// Consumer: brings the newest published frame to the front; false
	// when nothing new was published since the last call.
	bool TakeLatest();
	// Consumer: puts a copy of data on screen, dropping a published frame
	// not taken yet so it cannot replace this one.
	void ShowCopy(const unsigned char *data, int width, int height, int pitch, int time);
	// Consumer: the frame on screen; data is nullptr when there is none.
	Frame Front() const;
	// Consumer: forgets every frame.
	void Clear();

private:
	struct Buffer
	{
		std::vector<unsigned char> data;
		int width = 0;
		int height = 0;
		int pitch = 0;
		int time = -1;
	};
	mutable std::mutex m_mutex;
	Buffer m_buffers[3];
	// a permutation of 0, 1, 2; only the producer touches m_back's buffer,
	// only the consumer m_front's
	int m_back = 0;
	int m_ready = 1;
	int m_front = 2;
	bool m_hasReady = false;
	bool m_hasFront = false;
	unsigned m_serial = 0;
};
