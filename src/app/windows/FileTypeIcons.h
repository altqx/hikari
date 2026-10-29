/***************************************************************
 * Copyright (c) 2026, altqx
 *
 * HikariSub is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * HikariSub is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with HikariSub.  If not, see <http://www.gnu.org/licenses/>.
 **************************************************************/

// Plain #defines only: the resource compiler includes this file.
// Explorer shows an executable's lowest-id icon group, so IDI_HIKARISUB_APP keeps the lowest id.
// File type ids are written into the registry as icon resource ids: append, never renumber,
// and never reuse 114-126.

#pragma once

#define IDI_HIKARISUB_APP          1
#define IDI_HIKARISUB_SMALL        2

#define IDI_FILETYPE_FIRST     109

#define IDI_FILETYPE_ASS       109
#define IDI_FILETYPE_SSA       110
#define IDI_FILETYPE_SRT       111
#define IDI_FILETYPE_SUB       112
#define IDI_FILETYPE_TXT       113

#define IDI_FILETYPE_LAST      113
