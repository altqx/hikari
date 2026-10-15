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

#include <string>
#include <string_view>

// The name of the folder, in the replace backup folder, that the backup of
// the subtitles at subtitlesPath is made in when Find in subtitles replaces
// in them: one for each folder, so that the backups of same-named files from
// other folders, as ep01/en.ass and ep02/en.ass, do not replace each other,
// and a file keeps one backup, of its last replace. The name of the
// subtitles' folder, cut short and with what no folder name may hold made
// '_', then the hex of a hash of the folder's whole path, as "ep01 1a2b3c4d".
std::wstring ReplaceBackupFolderName(std::wstring_view subtitlesPath);
