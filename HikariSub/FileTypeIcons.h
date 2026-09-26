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

// Icon resource ids for HikariSub.exe.
//
// Plain #defines only: resource.rc includes this, so no C++ here.
//
// IDI_HIKARISUB_APP must keep the lowest id. Explorer shows an executable's
// first icon group, ordered by numeric id with string-named ones last.
//
// The file type ids go into the registry (negated, as resource ids) by
// Registry::AddFileAssociation, so they are part of the on-disk format:
// append, never renumber. They match the ids the retired Icons_dll used.

#pragma once

#define IDI_HIKARISUB_APP          1
#define IDI_HIKARISUB_SMALL        2

#define IDI_FILETYPE_FIRST     109

#define IDI_FILETYPE_ASS       109
#define IDI_FILETYPE_SSA       110
#define IDI_FILETYPE_SRT       111
#define IDI_FILETYPE_SUB       112
#define IDI_FILETYPE_TXT       113
#define IDI_FILETYPE_MKV       114
#define IDI_FILETYPE_MP4       115
#define IDI_FILETYPE_AVI       116
#define IDI_FILETYPE_OGM       117
#define IDI_FILETYPE_WMV       118
#define IDI_FILETYPE_ASF       119
#define IDI_FILETYPE_RMVB      120
#define IDI_FILETYPE_RM        121
#define IDI_FILETYPE_3GP       122
#define IDI_FILETYPE_MPG       123
#define IDI_FILETYPE_MPEG      124
#define IDI_FILETYPE_TS        125
#define IDI_FILETYPE_M2TS      126

#define IDI_FILETYPE_LAST      126
