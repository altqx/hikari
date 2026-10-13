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

#include "FrameExchange.h"

#include <cstring>
#include <utility>

unsigned char *FrameExchange::BeginWrite(int width, int height, int pitch)
{
	int back;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		back = m_back;
	}
	// only Publish moves m_back, and only the producer calls it
	Buffer &buffer = m_buffers[back];
	buffer.data.resize((size_t)height * (size_t)pitch);
	buffer.width = width;
	buffer.height = height;
	buffer.pitch = pitch;
	return buffer.data.data();
}

void FrameExchange::Publish(int time)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_buffers[m_back].time = time;
	std::swap(m_back, m_ready);
	m_hasReady = true;
}

bool FrameExchange::TakeLatest()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (!m_hasReady)
		return false;
	std::swap(m_front, m_ready);
	m_hasReady = false;
	m_hasFront = true;
	++m_serial;
	return true;
}

void FrameExchange::ShowCopy(const unsigned char *data, int width, int height, int pitch, int time)
{
	int front;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_hasReady = false;
		front = m_front;
	}
	// the front buffer is the consumer's own, so it is filled unlocked
	Buffer &buffer = m_buffers[front];
	size_t bytes = (size_t)height * (size_t)pitch;
	buffer.data.resize(bytes);
	if (buffer.data.data() != data)
		std::memcpy(buffer.data.data(), data, bytes);
	buffer.width = width;
	buffer.height = height;
	buffer.pitch = pitch;
	buffer.time = time;
	std::lock_guard<std::mutex> lock(m_mutex);
	m_hasFront = true;
	++m_serial;
}

FrameExchange::Frame FrameExchange::Front() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	Frame frame;
	frame.serial = m_serial;
	if (!m_hasFront)
		return frame;
	const Buffer &buffer = m_buffers[m_front];
	frame.data = buffer.data.data();
	frame.width = buffer.width;
	frame.height = buffer.height;
	frame.pitch = buffer.pitch;
	frame.time = buffer.time;
	return frame;
}

void FrameExchange::Clear()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_hasReady = false;
	m_hasFront = false;
	++m_serial;
}
