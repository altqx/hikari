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

// The file types HikariSub can register itself for: one table replacing what
// used to be four orderings kept aligned by hand (two identical copies of
// extensions[]/extensionsDesc[] in OptionsDialog.cpp, Icons_dll's resource id
// sequence, and the icon filenames), with nothing checking them.

#pragma once

#include "FileTypeIcons.h"
#include <wx/string.h>
#include <wx/translation.h>
#include <cstddef>
#include <iterator>

enum class HikariSubFileKind
{
	Subtitle,
	Video,
};

struct HikariSubFileType
{
	// Extension, dot included.
	const wchar_t   *extension;
	// Icon id in HikariSub.exe, from FileTypeIcons.h.
	int              iconResourceId;
	// freedesktop type for the Linux .desktop file; repeats, so de-duplicate
	// when building a MimeType= list. nullptr where none fits.
	const char      *mimeType;
	// Untranslated; call HikariSubFileTypeDescription(), which has to run after
	// wxLocale is up.
	const wchar_t   *description;
	HikariSubFileKind  kind;
};

constexpr size_t kHikariSubFileTypeCount = 18;

// Order drives the list in the options page; it no longer reaches the
// registry. Mime types are what shared-mime-info actually resolves these to,
// not the spellings you might expect (video/matroska, video/vnd.avi,
// application/vnd.ms-asf). .txt has none on purpose: it is text/plain, and
// claiming that would make HikariSub a handler for every text file.

inline constexpr HikariSubFileType kHikariSubFileTypes[] =
{
	{ L".ass",  IDI_FILETYPE_ASS,  "text/x-ssa",                    wxTRANSLATE(L"ASS subtitles"),  HikariSubFileKind::Subtitle },
	{ L".ssa",  IDI_FILETYPE_SSA,  "text/x-ssa",                    wxTRANSLATE(L"SSA subtitles"),  HikariSubFileKind::Subtitle },
	{ L".srt",  IDI_FILETYPE_SRT,  "application/x-subrip",          wxTRANSLATE(L"SRT subtitles"),  HikariSubFileKind::Subtitle },
	{ L".sub",  IDI_FILETYPE_SUB,  "text/x-microdvd",               wxTRANSLATE(L"SUB subtitles"),  HikariSubFileKind::Subtitle },
	{ L".txt",  IDI_FILETYPE_TXT,  nullptr,                         wxTRANSLATE(L"TXT subtitles"),  HikariSubFileKind::Subtitle },
	{ L".mkv",  IDI_FILETYPE_MKV,  "video/matroska",                wxTRANSLATE(L"Video MKV"),   HikariSubFileKind::Video    },
	{ L".mp4",  IDI_FILETYPE_MP4,  "video/mp4",                     wxTRANSLATE(L"Video MP4"),   HikariSubFileKind::Video    },
	{ L".avi",  IDI_FILETYPE_AVI,  "video/vnd.avi",                 wxTRANSLATE(L"Video AVI"),   HikariSubFileKind::Video    },
	{ L".ogm",  IDI_FILETYPE_OGM,  "video/x-ogm+ogg",               wxTRANSLATE(L"Video OGM"),   HikariSubFileKind::Video    },
	{ L".wmv",  IDI_FILETYPE_WMV,  "video/x-ms-wmv",                wxTRANSLATE(L"Video WMV"),   HikariSubFileKind::Video    },
	{ L".asf",  IDI_FILETYPE_ASF,  "application/vnd.ms-asf",        wxTRANSLATE(L"Video ASF"),   HikariSubFileKind::Video    },
	{ L".rmvb", IDI_FILETYPE_RMVB, "application/vnd.rn-realmedia",  wxTRANSLATE(L"Video RMVB"),  HikariSubFileKind::Video    },
	{ L".rm",   IDI_FILETYPE_RM,   "application/vnd.rn-realmedia",  wxTRANSLATE(L"Video RM"),    HikariSubFileKind::Video    },
	{ L".3gp",  IDI_FILETYPE_3GP,  "video/3gpp",                    wxTRANSLATE(L"Video 3GP"),   HikariSubFileKind::Video    },
	{ L".mpg",  IDI_FILETYPE_MPG,  "video/mpeg",                    wxTRANSLATE(L"Video MPG"),   HikariSubFileKind::Video    },
	{ L".mpeg", IDI_FILETYPE_MPEG, "video/mpeg",                    wxTRANSLATE(L"Video MPEG"),  HikariSubFileKind::Video    },
	// .ts collides with Qt Linguist and TypeScript at the same glob weight.
	{ L".ts",   IDI_FILETYPE_TS,   "video/mp2t",                    wxTRANSLATE(L"Video TS"),    HikariSubFileKind::Video    },
	{ L".m2ts", IDI_FILETYPE_M2TS, "video/mp2t",                    wxTRANSLATE(L"Video M2TS"),  HikariSubFileKind::Video    },
};

// The compiler cannot see resource.rc, so check what it can: ids contiguous
// and ascending from FIRST to LAST, which is what resource.rc declares.
static_assert(std::size(kHikariSubFileTypes) == kHikariSubFileTypeCount,
	"kHikariSubFileTypeCount is out of step with the table");
static_assert(kHikariSubFileTypes[0].iconResourceId == IDI_FILETYPE_FIRST,
	"the table must start at IDI_FILETYPE_FIRST");
static_assert(kHikariSubFileTypes[kHikariSubFileTypeCount - 1].iconResourceId == IDI_FILETYPE_LAST,
	"the table must end at IDI_FILETYPE_LAST");
static_assert(IDI_FILETYPE_LAST - IDI_FILETYPE_FIRST + 1 == (int)kHikariSubFileTypeCount,
	"the icon id range does not cover exactly the table");
static_assert(IDI_HIKARISUB_APP < IDI_FILETYPE_FIRST,
	"the application icon must keep the lowest id, or Explorer will show a "
	"file type icon for HikariSub.exe itself");

static constexpr bool HikariSubFileTypeIdsAscend()
{
	for (size_t i = 1; i < kHikariSubFileTypeCount; ++i)
	{
		if (kHikariSubFileTypes[i].iconResourceId != kHikariSubFileTypes[i - 1].iconResourceId + 1)
			return false;
	}
	return true;
}
static_assert(HikariSubFileTypeIdsAscend(),
	"icon ids must be contiguous and ascending, matching resource.rc");

//  Translated description for kHikariSubFileTypes[index].
wxString HikariSubFileTypeDescription(size_t index);
