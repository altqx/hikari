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
#include "../HikariSub/AudioCommit.h"

TEST(an_audio_commit_goes_to_the_next_line_with_the_toggle_on)
{
	CHECK(AudioCommitGoesToNextLine(true));
}

TEST(an_audio_commit_keeps_the_active_line_with_the_toggle_off)
{
	// "Go to the next line after applying changes" off: the Commit button,
	// Enter and G save the times and stay on the Line
	CHECK(!AudioCommitGoesToNextLine(false));
}
