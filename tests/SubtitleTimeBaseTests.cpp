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

// Subtitle and chapter times from containers whose time base is not 1/1000
// (FFMS_GetSubtitles and FFMS_GetChapters in
// Thirdparty/Build/FFMS2/indexing_additional.cpp): MP4 and MOV with mov_text,
// MPEG-TS at 1/90000 and QuickTime chapters at 1/600, written here with
// libavformat. Times must reach the callback in milliseconds, each end
// rounded to the nearest one, on the container's own clock. The Matroska
// files with another TimestampScale are in MatroskaSubtitlesTests.cpp.

#include "check.h"

#include "../Thirdparty/ffms2/include/ffms.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
}

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

struct Packet {
	int64_t pts, duration; // in the stream's time base
	std::string data;
};

struct Chapter {
	int64_t start, end; // milliseconds
	const char *title;
};

std::string TempPath(const char *suffix)
{
	std::string path = std::string("/tmp/hikarisub-timebase-XXXXXX") + suffix;
	std::vector<char> buffer(path.begin(), path.end());
	buffer.push_back('\0');
	int fd = mkstemps(buffer.data(), int(std::strlen(suffix)));
	if (fd < 0)
		return {};
	close(fd);
	return buffer.data();
}

// One subtitle stream of `codec` at `timeBase` in a `format` file, the
// packets as given. Empty when the muxer did not keep the time base, so a
// test cannot pass on a file that does not have the shape it names.
std::string Write(const char *format, const char *suffix, AVCodecID codec, AVRational timeBase,
	const std::vector<Packet> &packets, const char *options = nullptr, const std::vector<Chapter> &chapters = {})
{
	std::string path = TempPath(suffix);
	if (path.empty())
		return {};
	AVFormatContext *out = nullptr;
	if (avformat_alloc_output_context2(&out, nullptr, format, path.c_str()) < 0)
		return {};
	AVStream *stream = avformat_new_stream(out, nullptr);
	stream->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
	stream->codecpar->codec_id = codec;
	stream->time_base = timeBase;
	for (size_t i = 0; i < chapters.size(); i++) {
		AVChapter *chapter = static_cast<AVChapter *>(av_mallocz(sizeof(AVChapter)));
		chapter->id = int64_t(i);
		chapter->time_base = AVRational{ 1, 1000 };
		chapter->start = chapters[i].start;
		chapter->end = chapters[i].end;
		av_dict_set(&chapter->metadata, "title", chapters[i].title, 0);
		av_dynarray_add(&out->chapters, reinterpret_cast<int *>(&out->nb_chapters), chapter);
	}
	AVDictionary *settings = nullptr;
	if (options)
		av_dict_parse_string(&settings, options, "=", ",", 0);
	bool good = avio_open(&out->pb, path.c_str(), AVIO_FLAG_WRITE) >= 0 &&
		avformat_write_header(out, &settings) >= 0 && av_cmp_q(stream->time_base, timeBase) == 0;
	av_dict_free(&settings);
	for (size_t i = 0; good && i < packets.size(); i++) {
		AVPacket *packet = av_packet_alloc();
		good = av_new_packet(packet, int(packets[i].data.size())) >= 0;
		if (good) {
			std::memcpy(packet->data, packets[i].data.data(), packets[i].data.size());
			packet->pts = packet->dts = packets[i].pts;
			packet->duration = packets[i].duration;
			packet->flags = AV_PKT_FLAG_KEY;
			good = av_interleaved_write_frame(out, packet) >= 0;
		}
		av_packet_free(&packet);
	}
	if (good)
		good = av_write_trailer(out) >= 0;
	avio_closep(&out->pb);
	avformat_free_context(out);
	if (!good) {
		std::remove(path.c_str());
		return {};
	}
	return path;
}

struct Time {
	int64_t start, duration;
};

// What the file holds, read back with libavformat: the stream's time base.
AVRational StreamTimeBase(const std::string &path)
{
	AVFormatContext *in = nullptr;
	AVRational timeBase{ 0, 1 };
	if (avformat_open_input(&in, path.c_str(), nullptr, nullptr) < 0)
		return timeBase;
	if (avformat_find_stream_info(in, nullptr) >= 0 && in->nb_streams > 0)
		timeBase = in->streams[0]->time_base;
	avformat_close_input(&in);
	return timeBase;
}

int FFMS_CC OnSubtitle(int64_t start, int64_t duration, int64_t, const char *, void *opaque)
{
	static_cast<std::vector<Time> *>(opaque)->push_back({ start, duration });
	return 0;
}

FFMS_Indexer *Indexer(const std::string &path)
{
	FFMS_Init(0, 1);
	char message[1024];
	FFMS_ErrorInfo error{ FFMS_ERROR_SUCCESS, FFMS_ERROR_SUCCESS, sizeof(message), message };
	return FFMS_CreateIndexer(path.c_str(), &error);
}

std::vector<Time> ReadTimes(const std::string &path)
{
	std::vector<Time> times;
	FFMS_Indexer *indexer = Indexer(path);
	if (!indexer)
		return times;
	if (FFMS_GetNumTracksI(indexer) > 0 && FFMS_GetTrackTypeI(indexer, 0) == FFMS_TYPE_SUBTITLE)
		FFMS_GetSubtitles(indexer, 0, &OnSubtitle, &times);
	FFMS_CancelIndexing(indexer);
	return times;
}

void CheckTimes(const std::vector<Time> &times, const std::vector<Time> &expected)
{
	CHECK_EQ(times.size(), expected.size());
	for (size_t i = 0; i < times.size() && i < expected.size(); i++) {
		CHECK_EQ(times[i].start, expected[i].start);
		CHECK_EQ(times[i].duration, expected[i].duration);
	}
}

// A mov_text sample: a 16-bit length, then the text.
std::string MovText(const std::string &text)
{
	return std::string(1, char(text.size() >> 8)) + char(text.size() & 0xFF) + text;
}

} // namespace

TEST(subtitle_times_mp4_mov_text_at_1_600_are_milliseconds)
{
	// 1001/600 s is 1668.33 ms and the cue ends at 1008/600 s, 1680 ms;
	// 3000/600 s is 5000 ms and it ends at 3901/600 s, 6501.67 ms.
	std::string path = Write("mp4", ".mp4", AV_CODEC_ID_MOV_TEXT, AVRational{ 1, 600 },
		{ { 1001, 7, MovText("one") }, { 3000, 901, MovText("two") } });
	CHECK(!path.empty());
	CHECK_EQ(StreamTimeBase(path).den, 600);
	std::vector<Time> times = ReadTimes(path);
	std::remove(path.c_str());
	// The muxer fills the gaps with empty samples, which are 2 bytes and so
	// handed over too: before the first cue and between the two.
	CheckTimes(times, { { 0, 1668 }, { 1668, 12 }, { 1680, 3320 }, { 5000, 1502 } });
}

TEST(subtitle_times_mov_mov_text_at_1_90000_are_milliseconds)
{
	// 111111 ticks is 1234.57 ms and the cue ends at 156156, 1735.07 ms: a
	// 500 ms cue, where rounding 45045 ticks (500.5 ms) on its own gives 501.
	std::string path = Write("mov", ".mov", AV_CODEC_ID_MOV_TEXT, AVRational{ 1, 90000 },
		{ { 111111, 45045, MovText("one") }, { 222222, 90000, MovText("two") } });
	CHECK(!path.empty());
	CHECK_EQ(StreamTimeBase(path).den, 90000);
	std::vector<Time> times = ReadTimes(path);
	std::remove(path.c_str());
	CheckTimes(times, { { 0, 1235 }, { 1235, 500 }, { 1735, 734 }, { 2469, 1000 } });
}

TEST(subtitle_times_mpegts_at_1_90000_are_milliseconds_on_the_stream_clock)
{
	// A DVB teletext stream, which MPEG-TS keeps as a subtitle stream at
	// 1/90000; mpegts_copyts keeps the times as written, and MPEG-TS carries
	// no duration. The times stay on the transport stream's clock (the file
	// starts at 1.23 s), as FFMS2 gives the video's frame times.
	std::string teletext(46, char(0xFF));
	teletext[0] = 0x10; // EBU data, the PES data_identifier
	std::string path = Write("mpegts", ".ts", AV_CODEC_ID_DVB_TELETEXT, AVRational{ 1, 90000 },
		{ { 111111, 0, teletext }, { 222222, 0, teletext }, { 8100045, 0, teletext } }, "mpegts_copyts=1");
	CHECK(!path.empty());
	CHECK_EQ(StreamTimeBase(path).den, 90000);
	std::vector<Time> times = ReadTimes(path);
	std::remove(path.c_str());
	CheckTimes(times, { { 1235, 0 }, { 2469, 0 }, { 90001, 0 } });
}

TEST(chapter_times_quicktime_at_1_600_are_milliseconds)
{
	// QuickTime's chapter track at the movie's 1/600, which the old division
	// by time_base.den / 1000 (0 here) could not take. 1234 ms is stored as
	// 740/600 s (1233.33 ms) and 2468 as 1481/600 (2468.33), cut toward zero
	// as Matroska's nanoseconds are.
	std::string path = Write("mov", ".mov", AV_CODEC_ID_MOV_TEXT, AVRational{ 1, 1000 },
		{ { 0, 2468, MovText("one") } }, "movie_timescale=600", { { 0, 1234, "A" }, { 1234, 2468, "B" } });
	CHECK(!path.empty());
	FFMS_Indexer *indexer = path.empty() ? nullptr : Indexer(path);
	CHECK(indexer != nullptr);
	FFMS_Chapters *chapters = indexer ? FFMS_GetChapters(indexer) : nullptr;
	CHECK(chapters != nullptr);
	if (chapters) {
		CHECK_EQ(chapters->NumOfChapters, 2);
		if (chapters->NumOfChapters == 2) {
			CHECK_EQ(chapters->Chapters[0].Start, int64_t(0));
			CHECK_EQ(chapters->Chapters[0].End, int64_t(1233));
			CHECK_EQ(chapters->Chapters[1].Start, int64_t(1233));
			CHECK_EQ(chapters->Chapters[1].End, int64_t(2468));
			CHECK(std::string(chapters->Chapters[1].Title) == "B");
		}
		FFMS_FreeChapters(&chapters);
	}
	if (indexer)
		FFMS_CancelIndexing(indexer);
	std::remove(path.c_str());
}

TEST(chapter_times_matroska_nanoseconds_are_cut_to_milliseconds)
{
	// Matroska keeps chapters in nanoseconds; 1234.567 ms stays 1234 and
	// 2469.999 stays 2469, as the old division gave.
	std::string path = TempPath(".mkv");
	CHECK(!path.empty());
	AVFormatContext *out = nullptr;
	CHECK(avformat_alloc_output_context2(&out, nullptr, "matroska", path.c_str()) >= 0);
	if (!out)
		return;
	AVStream *stream = avformat_new_stream(out, nullptr);
	stream->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
	stream->codecpar->codec_id = AV_CODEC_ID_SUBRIP;
	const int64_t bounds[][2] = { { 0, 1234567 }, { 1234567, 2469999 } }; // microseconds
	const char *titles[] = { "A", "B" };
	for (int i = 0; i < 2; i++) {
		AVChapter *chapter = static_cast<AVChapter *>(av_mallocz(sizeof(AVChapter)));
		chapter->id = i + 1;
		chapter->time_base = AVRational{ 1, 1000000 };
		chapter->start = bounds[i][0];
		chapter->end = bounds[i][1];
		av_dict_set(&chapter->metadata, "title", titles[i], 0);
		av_dynarray_add(&out->chapters, reinterpret_cast<int *>(&out->nb_chapters), chapter);
	}
	bool good = avio_open(&out->pb, path.c_str(), AVIO_FLAG_WRITE) >= 0 && avformat_write_header(out, nullptr) >= 0;
	if (good) {
		AVPacket *packet = av_packet_alloc();
		av_new_packet(packet, 5);
		std::memcpy(packet->data, "line!", 5);
		packet->pts = packet->dts = av_rescale_q(100, AVRational{ 1, 1000 }, stream->time_base);
		packet->duration = av_rescale_q(1000, AVRational{ 1, 1000 }, stream->time_base);
		good = av_interleaved_write_frame(out, packet) >= 0 && av_write_trailer(out) >= 0;
		av_packet_free(&packet);
	}
	avio_closep(&out->pb);
	avformat_free_context(out);
	CHECK(good);

	FFMS_Indexer *indexer = Indexer(path);
	CHECK(indexer != nullptr);
	FFMS_Chapters *read = indexer ? FFMS_GetChapters(indexer) : nullptr;
	CHECK(read != nullptr);
	if (read) {
		CHECK_EQ(read->NumOfChapters, 2);
		if (read->NumOfChapters == 2) {
			CHECK_EQ(read->Chapters[0].Start, int64_t(0));
			CHECK_EQ(read->Chapters[0].End, int64_t(1234));
			CHECK_EQ(read->Chapters[1].Start, int64_t(1234));
			CHECK_EQ(read->Chapters[1].End, int64_t(2469));
		}
		FFMS_FreeChapters(&read);
	}
	if (indexer)
		FFMS_CancelIndexing(indexer);
	std::remove(path.c_str());
}
