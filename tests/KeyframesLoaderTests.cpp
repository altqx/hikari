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

// The keyframes files KeyframeLoader reads, in the formats Aegisub reads
// (libaegisub/common/keyframe.cpp): Aegisub's own, whose second line is
// "fps N" and no keyframe, and the DivX map files, whose lines say a frame's
// type with one of the letters I, P or B.

#include "check.h"
#include "../HikariSub/KeyframesLoader.h"
#include "../HikariSub/OpennWrite.h"
#include "../HikariSub/Timebase.h"

#include <wx/ffile.h>
#include <wx/filename.h>

#include <cstdio>
#include <string>
#include <vector>

// OpennWrite.cpp needs the whole application; the loader only reads a file
// with it, so the test reads it here as UTF-8.
OpenWrite::OpenWrite() : isfirst(true) {}
OpenWrite::~OpenWrite() {}
bool OpenWrite::FileOpen(const wxString &filename, wxString *riddenText, bool)
{
	wxFFile f(filename, L"rb");
	return f.IsOpened() && f.ReadAll(riddenText, wxConvUTF8);
}

namespace {

std::vector<int> Load(const std::string &contents, const Timebase &tb)
{
	wxString path = wxFileName::CreateTempFileName(L"hikarisub-kf");
	{
		wxFFile f(path, L"wb");
		f.Write(contents.data(), contents.size());
	}
	std::vector<int> keyframes;
	KeyframeLoader loader(path, &keyframes, tb);
	wxRemoveFile(path);
	return keyframes;
}

void CheckFrames(const std::vector<int> &ms, const std::vector<int> &frames, const Timebase &tb)
{
	CHECK_EQ(ms.size(), frames.size());
	for (size_t i = 0; i < ms.size() && i < frames.size(); i++)
		CHECK_EQ(ms[i], tb.MsAt(frames[i]));
}

} // namespace

TEST(KeyframesAegisubFpsLineIsNoKeyframe)
{
	Timebase tb = Timebase::FromFps(25.f, 0);
	CheckFrames(Load("# keyframe format v1\nfps 0\n12\n40\n", tb), { 12, 40 }, tb);
	CheckFrames(Load("# keyframe format v1\nfps 23.976\n12\n40\n", tb), { 12, 40 }, tb);
	// a number with spaces after it is still a keyframe
	CheckFrames(Load("# keyframe format v1\nfps 0\n12 \n40\t\n", tb), { 12, 40 }, tb);
	// a file with no keyframes is reported as invalid by VideoBox::OpenKeyframes
	CheckFrames(Load("# keyframe format v1\nfps 0\n", tb), {}, tb);
}

TEST(KeyframesDivxMapFindsFrameTypes)
{
	Timebase tb = Timebase::FromFps(25.f, 0);
	CheckFrames(Load("##map version 1\nI\nP\nB\nB\nI\nP\n", tb), { 0, 4 }, tb);
}

TEST(KeyframesXvidStatsFindFrameTypes)
{
	Timebase tb = Timebase::FromFps(25.f, 0);
	CheckFrames(Load("# XviD 2pass stat file\ni 1 2\np 1 2\nb 1 2\ni 1 2\n", tb), { 0, 3 }, tb);
}
