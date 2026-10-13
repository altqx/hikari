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

#include <cassert>

// A pointer that remembers whether it owns what it points to. The audio
// display opens its own provider for an audio file, but plays a video's audio
// from the video's provider, which the video frees: only an owned object is
// deleted here, and the owner of a borrowed one makes the holder let go of it
// before freeing it.
template <typename T>
class OwnedOrBorrowed
{
public:
	OwnedOrBorrowed() = default;
	OwnedOrBorrowed(const OwnedOrBorrowed &) = delete;
	OwnedOrBorrowed &operator=(const OwnedOrBorrowed &) = delete;
	~OwnedOrBorrowed() { Reset(); }

	// Holds object, which is deleted here. Owning the object already borrowed
	// takes it over from its owner, which must not delete it any more.
	void Own(T *object)
	{
		if (object != pointer)
			Reset();
		pointer = object;
		owned = object != nullptr;
	}

	// Holds object, which its owner deletes. An object held as owned cannot
	// be borrowed back: nobody else would delete it.
	void Borrow(T *object)
	{
		assert(!object || object != pointer || !owned);
		if (object != pointer)
			Reset();
		pointer = object;
		owned = false;
	}

	// Lets go of the object, deleting it when it is owned.
	void Reset()
	{
		if (owned)
			delete pointer;
		pointer = nullptr;
		owned = false;
	}

	// True when object is held and its owner is someone else, who has to
	// make the holder let go of it before freeing it.
	bool Borrows(const T *object) const
	{
		return pointer && pointer == object && !owned;
	}

	bool IsOwned() const { return owned; }
	T *Get() const { return pointer; }
	T *operator->() const { return pointer; }
	operator T *() const { return pointer; }

private:
	T *pointer = nullptr;
	bool owned = false;
};
