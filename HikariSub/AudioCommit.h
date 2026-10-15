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

// Whether committing the audio times with the Commit button, Enter or G goes
// on to the next Line. nextLineOnCommit is the "Go to the next line after
// applying changes" toggle (AUDIO_NEXT_LINE_ON_COMMIT); with it off a commit
// saves the times and keeps the active Line.
inline bool AudioCommitGoesToNextLine(bool nextLineOnCommit)
{
	return nextLineOnCommit;
}
