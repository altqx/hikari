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

// What committing the Line editor's fields does, with Apply changes
// (Ctrl+Enter) or with Enter.
enum class EditCommit {
	// nothing to send: Apply leaves the Line, the visual tool and the audio
	// display as they are (no undo step, the file not modified)
	NOTHING,
	// the Line would end before it starts: nothing is sent, the fields keep
	// what was typed, and the editor says "End is before Start."
	REFUSED,
	// the changed fields are written to the Line, and the views follow it
	SEND,
};

// anyChange: a field differs from the Line, so a commit has something to
// send. start and end: the Line's times with the typed ones.
inline EditCommit PlanEditCommit(bool anyChange, int start, int end)
{
	if (!anyChange)
		return EditCommit::NOTHING;
	if (end < start)
		return EditCommit::REFUSED;
	return EditCommit::SEND;
}

// Saving while typing (Editor: save after this many characters) writes the
// fields as they are so far. It waits while the Line would end before it
// starts, so that the commit can still refuse it; leaving the Line sends
// what waited, as before, and the Line's End is then set to its Start.
inline bool SaveWhileTyping(int start, int end)
{
	return end >= start;
}
