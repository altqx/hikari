//  Copyright (c) 2016 - 2026, Marcin Drob
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

// The columns of a Dialogue as flags, apart from SubsDialogue.h so that code
// without wxWidgets can use them.
enum {
	LAYER = 1,
	START = 2,
	END = 4,
	STYLE = 8,
	ACTOR = 16,
	MARGINL = 32,
	MARGINR = 64,
	MARGINV = 128,
	EFFECT = 256,
	CPS = 512,
	TXT = 1024,
	TXTTL = 2048,
	COMMENT = 4096,
	WRAPS = 8192
};
