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

// Subtitles read from a Matroska file (FFMS_GetSubtitles in
// Thirdparty/Build/FFMS2/indexing_additional.cpp, which Demux::GetSubtitles
// calls) for a file shaped like the karaoke file whose first line was lost:
// the track's first blocks, very long \p1 drawings, sit in one cluster, more
// blocks of the same time in the next, and the last cue of that time names
// the next cluster. Every block must arrive, and the first line, assembled as
// Demux does, must draw where it is placed.

#include "check.h"

#include "../Thirdparty/ffms2/include/ffms.h"

extern "C" {
#include <ass/ass.h>
}

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

using Bytes = std::string;

void PutId(Bytes &out, uint32_t id)
{
	if (id > 0xFFFFFF)
		out.push_back(char(id >> 24));
	if (id > 0xFFFF)
		out.push_back(char(id >> 16));
	if (id > 0xFF)
		out.push_back(char(id >> 8));
	out.push_back(char(id));
}

// EBML element with an 8-byte size
Bytes Element(uint32_t id, const Bytes &payload)
{
	Bytes out;
	PutId(out, id);
	out.push_back(char(0x01));
	for (int i = 6; i >= 0; i--)
		out.push_back(char(uint64_t(payload.size()) >> (8 * i)));
	return out + payload;
}

// unsigned integer element, always 8 bytes, so a position does not change sizes
Bytes Uint(uint32_t id, uint64_t value)
{
	Bytes payload;
	for (int i = 7; i >= 0; i--)
		payload.push_back(char(value >> (8 * i)));
	return Element(id, payload);
}

const char kCodecPrivate[] =
	"[Script Info]\r\n"
	"ScriptType: v4.00+\r\n"
	"PlayResX: 1920\r\n"
	"PlayResY: 1080\r\n"
	"ScaledBorderAndShadow: yes\r\n"
	"\r\n"
	"[V4+ Styles]\r\n"
	"Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
	"Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
	"MarginR, MarginV, Encoding\r\n"
	"Style: Opening TH,Arial,75,&H00FFFFFF,&H000000FF,&H00000000,&H8B000000,0,0,0,0,100,100,0,0,1,0,0,2,25,25,25,0\r\n"
	"\r\n"
	"[Events]\r\n"
	"Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\r\n";

// One strip of the scanline line: the whole sentence as a \p1 drawing of
// about 38.7 KB, seen only through its shadow (fill and border at alpha FE),
// with twelve \t and a horizontal \clip strip, as the karaoke file has it.
std::string ScanlineText(int clipTop, int clipBottom)
{
	std::string text = "{\\an7\\pos(960,1017)\\shad0.1\\blur0.8\\bord3\\1a&HFE&\\3a&HFE&\\4a&H00&\\4c&HFFFFFF&"
		"\\clip(0," + std::to_string(clipTop) + ",1920," + std::to_string(clipBottom) + ")\\fscx100\\fscy100";
	for (int i = 0; i < 3; i++) {
		int base = 960 * (i + 1);
		text += "\\t(" + std::to_string(base) + "," + std::to_string(base + 42) + ",2.09,\\xshad-10.37)";
		text += "\\t(" + std::to_string(base + 41) + "," + std::to_string(base + 84) + ",0.01,\\xshad-7.14)";
		text += "\\t(" + std::to_string(base + 83) + "," + std::to_string(base + 126) + ",3.19,\\xshad-0.88)";
		text += "\\t(" + std::to_string(base + 125) + ".13," + std::to_string(base + 125) + ".13,\\xshad0)";
	}
	text += "\\p1}";
	// glyph-sized closed shapes across x -228..228, y -32..14
	for (int i = 0; text.size() < 38700; i++) {
		double x = -228 + (i % 76) * 6.0, y = -32 + ((i / 76) % 4) * 12.0;
		char shape[256];
		std::snprintf(shape, sizeof(shape),
			"m %.2f %.2f b %.2f %.2f %.2f %.2f %.2f %.2f l %.2f %.2f b %.2f %.2f %.2f %.2f %.2f %.2f l %.2f %.2f ",
			x, y, x + 1.5, y - 0.5, x + 3.5, y - 0.5, x + 4.5, y, x + 4.5, y + 10, x + 3.5, y + 10.5, x + 1.5,
			y + 10.5, x, y + 10, x, y);
		text += shape;
	}
	return text;
}

struct Block {
	int64_t start, duration;
	std::string data; // ReadOrder,Layer,Style,Name,MarginL,MarginR,MarginV,Effect,Text
};

const int kStrips = 6;

std::vector<Block> FirstCluster()
{
	std::vector<Block> blocks;
	for (int i = 0; i < kStrips; i++) {
		int top = (i == 0) ? 0 : 979 + i * 10, bottom = (i == kStrips - 1) ? 1080 : 989 + i * 10;
		blocks.push_back({ 401, 3830, std::to_string(100 + i) + ",1,Opening TH,thai,0,0,0,fx.scanline," +
			ScanlineText(top, bottom) });
	}
	return blocks;
}

std::vector<Block> SecondCluster()
{
	return { { 401, 3830, "0,0,Opening TH,,0,0,0,fx,{\\an5\\pos(960,100)}romaji" },
		{ 5731, 2000, "1,0,Opening TH,,0,0,0,,next line" } };
}

Bytes Cluster(int64_t time, const std::vector<Block> &blocks)
{
	Bytes payload = Uint(0xE7, uint64_t(time));
	for (const Block &b : blocks) {
		Bytes block;
		block.push_back(char(0x81)); // track 1
		int16_t relative = int16_t(b.start - time);
		block.push_back(char(relative >> 8));
		block.push_back(char(relative));
		block.push_back(0);
		block += b.data;
		payload += Element(0xA0, Element(0xA1, block) + Uint(0x9B, uint64_t(b.duration)));
	}
	return Element(0x1F43B675, payload);
}

Bytes CuePoint(int64_t time, uint64_t clusterPosition)
{
	return Element(0xBB, Uint(0xB3, uint64_t(time)) + Element(0xB7, Uint(0xF7, 1) + Uint(0xF1, clusterPosition)));
}

// Cues before the clusters: one per block, those of the first cluster first,
// so the last cue at 0:00:00.401 names the second cluster.
Bytes Cues(uint64_t first, uint64_t second)
{
	Bytes points;
	for (const Block &b : FirstCluster())
		points += CuePoint(b.start, first);
	for (const Block &b : SecondCluster())
		points += CuePoint(b.start, second);
	return Element(0x1C53BB6B, points);
}

std::string WriteScanlineMatroska()
{
	Bytes ebml = Element(0x1A45DFA3, Uint(0x4286, 1) + Uint(0x42F7, 1) + Uint(0x42F2, 4) + Uint(0x42F3, 8) +
		Element(0x4282, "matroska") + Uint(0x4287, 4) + Uint(0x4285, 2));
	Bytes head = Element(0x1549A966, Uint(0x2AD7B1, 1000000) + Element(0x4D80, "hikarisub_tests") +
		Element(0x5741, "hikarisub_tests"));
	head += Element(0x1654AE6B, Element(0xAE, Uint(0xD7, 1) + Uint(0x73C5, 1001) + Uint(0x83, 0x11) +
		Uint(0x9C, 0) + Element(0x86, "S_TEXT/ASS") + Element(0x63A2, kCodecPrivate)));
	Bytes first = Cluster(0, FirstCluster());
	Bytes second = Cluster(292, SecondCluster());
	// positions are from the start of the Segment's data
	uint64_t firstPosition = head.size() + Cues(0, 0).size();
	Bytes segment = head + Cues(firstPosition, firstPosition + first.size()) + first + second;

	char path[] = "/tmp/hikarisub-mkv-XXXXXX";
	int fd = mkstemp(path);
	if (fd < 0)
		return {};
	close(fd);
	std::ofstream out(path, std::ios::binary);
	Bytes file = ebml + Element(0x18538067, segment);
	out.write(file.data(), std::streamsize(file.size()));
	return out ? std::string(path) : std::string();
}

struct Read {
	std::vector<Block> blocks;
};

int FFMS_CC OnSubtitle(int64_t start, int64_t duration, int64_t, const char *line, void *opaque)
{
	static_cast<Read *>(opaque)->blocks.push_back({ start, duration, line });
	return 0;
}

std::vector<Block> ReadTrack(const std::string &path)
{
	Read read;
	FFMS_Init(0, 1);
	char message[1024];
	FFMS_ErrorInfo error{ FFMS_ERROR_SUCCESS, FFMS_ERROR_SUCCESS, sizeof(message), message };
	FFMS_Indexer *indexer = FFMS_CreateIndexer(path.c_str(), &error);
	if (!indexer)
		return {};
	if (FFMS_GetTrackTypeI(indexer, 0) == FFMS_TYPE_SUBTITLE)
		FFMS_GetSubtitles(indexer, 0, &OnSubtitle, &read);
	FFMS_CancelIndexing(indexer);
	return read.blocks;
}

// Demux::GetSubtitles' line for an ASS block (Demux.cpp), times in ASS form.
std::string AssTime(int64_t ms)
{
	char out[32];
	ms = ms / 10 * 10;
	std::snprintf(out, sizeof(out), "%d:%02d:%02d.%02d", int(ms / 3600000), int(ms / 60000 % 60),
		int(ms / 1000 % 60), int(ms / 10 % 100));
	return out;
}

std::string DialogueOf(const Block &b)
{
	std::string rest = b.data.substr(b.data.find(',') + 1); // without ReadOrder
	size_t comma = rest.find(',');
	std::string layer = rest.substr(0, comma);
	rest = rest.substr(comma + 1);
	int64_t start = (b.start + 5) / 10 * 10, end = (b.start + b.duration + 5) / 10 * 10;
	return "Dialogue: " + layer + "," + AssTime(start) + "," + AssTime(end) + "," + rest + "\r\n";
}

} // namespace

TEST(matroska_subtitles_keep_blocks_before_the_cued_cluster)
{
	std::string path = WriteScanlineMatroska();
	CHECK(!path.empty());
	std::vector<Block> blocks = ReadTrack(path);
	std::remove(path.c_str());

	std::vector<Block> expected = FirstCluster();
	for (const Block &b : SecondCluster())
		expected.push_back(b);
	CHECK_EQ(blocks.size(), expected.size());
	for (size_t i = 0; i < blocks.size() && i < expected.size(); i++) {
		CHECK_EQ(blocks[i].start, expected[i].start);
		CHECK_EQ(blocks[i].duration, expected[i].duration);
		CHECK(blocks[i].data == expected[i].data);
	}
	// the drawings arrive whole
	CHECK(!blocks.empty() && blocks[0].data.size() > 38700);
}

TEST(matroska_subtitles_scanline_line_draws_under_its_position)
{
	std::string path = WriteScanlineMatroska();
	CHECK(!path.empty());
	std::vector<Block> blocks = ReadTrack(path);
	std::remove(path.c_str());

	std::string script = kCodecPrivate;
	for (const Block &b : blocks)
		script += DialogueOf(b);

	ASS_Library *library = ass_library_init();
	ass_set_message_cb(library, [](int, const char *, va_list, void *) {}, nullptr);
	ASS_Renderer *renderer = ass_renderer_init(library);
	// a drawing is laid out with the style's font too, so some font must be found
	ass_set_fonts(renderer, nullptr, "sans-serif", ASS_FONTPROVIDER_AUTODETECT, nullptr, 1);
	ass_set_frame_size(renderer, 1920, 1080);
	ASS_Track *track = ass_read_memory(library, script.data(), script.size(), nullptr);
	CHECK(track != nullptr);
	long inside = 0, outside = 0;
	if (track) {
		// 0:00:02: between the \t sweeps, the shadow at rest
		int change = 0;
		for (ASS_Image *img = ass_render_frame(renderer, track, 2000, &change); img; img = img->next) {
			if ((img->color & 0xFF) == 0xFF)
				continue;
			for (int y = 0; y < img->h; y++) {
				for (int x = 0; x < img->w; x++) {
					if (!img->bitmap[y * img->stride + x])
						continue;
					int px = img->dst_x + x, py = img->dst_y + y;
					bool line = px >= 720 && px < 1200 && py >= 975 && py < 1040;
					bool romaji = py < 200;
					if (line)
						inside++;
					else if (!romaji)
						outside++;
				}
			}
		}
		ass_free_track(track);
	}
	ass_renderer_done(renderer);
	ass_library_done(library);
	// the strips of the drawing under \pos(960,1017), nothing elsewhere
	CHECK(inside > 5000);
	CHECK_EQ(outside, 0L);
}
