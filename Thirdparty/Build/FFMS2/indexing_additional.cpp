//  Copyright (c) 2021 Drob Marcin
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  THE SOFTWARE.

#include "../../ffms2/include/ffms.h"
#include "../../ffms2/src/core/indexing.h"

static const AVRational kMilliseconds = { 1, 1000 };


const char* FFMS_Indexer::GetTrackName(int Track)
{
	AVDictionary* d = FormatContext->streams[Track]->metadata;
	AVDictionaryEntry* t = av_dict_get(d, "title", nullptr, 0);
	if (t) {
		return t->value;
	}
	return nullptr;
}

const char* FFMS_Indexer::GetTrackLanguage(int Track)
{
	AVDictionary* d = FormatContext->streams[Track]->metadata;
	AVDictionaryEntry* t = av_dict_get(d, "language", nullptr, 0);
	if (t) {
		return t->value;
	}
	return nullptr;
}

FFMS_Chapters* FFMS_Indexer::GetChapters()
{
	if (!FormatContext->nb_chapters)
		return nullptr;

	FFMS_Chapters *Chapters = new FFMS_Chapters;
	Chapters->Chapters = new FFMS_Chapter[FormatContext->nb_chapters];
	Chapters->NumOfChapters = FormatContext->nb_chapters;
	for (int i = 0; i < FormatContext->nb_chapters; i++) {
		AVChapter* chapter = FormatContext->chapters[i];
		// Milliseconds, cut toward zero as the old division by time_base.den
		// / 1000 cut Matroska's nanoseconds. That division was right only for
		// a den that is a multiple of 1000 and a num of 1, and divided by zero
		// for a den under 1000: QuickTime's chapter track counts in 1/600.
		Chapters->Chapters[i].Start = av_rescale_q_rnd(chapter->start, chapter->time_base, kMilliseconds,
			AVRounding(AV_ROUND_ZERO | AV_ROUND_PASS_MINMAX));
		Chapters->Chapters[i].End = av_rescale_q_rnd(chapter->end, chapter->time_base, kMilliseconds,
			AVRounding(AV_ROUND_ZERO | AV_ROUND_PASS_MINMAX));
		AVDictionaryEntry* e = av_dict_get(chapter->metadata, "title", nullptr, 0);
		Chapters->Chapters[i].Title = (e) ? e->value : nullptr;
	}
	return Chapters;
}

FFMS_Attachment* FFMS_Indexer::GetAttachment(int Track)
{
	if (FormatContext->streams[Track]->codecpar->codec_type == AVMEDIA_TYPE_ATTACHMENT) {
		FFMS_Attachment* Attachment = new FFMS_Attachment();
		AVStream* stream = FormatContext->streams[Track];
		AVDictionary* d = stream->metadata;
		AVDictionaryEntry* tf = av_dict_get(d, "filename", 0, 0);
		AVDictionaryEntry* tm = av_dict_get(d, "mimetype", 0, 0);
		Attachment->Filename = (tf) ? tf->value : nullptr;
		Attachment->Mimetype = (tm) ? tm->value : nullptr;
		Attachment->DataSize = stream->codecpar->extradata_size;
		Attachment->Data = stream->codecpar->extradata;
		return Attachment;
	}
	return nullptr;
}

// Every packet of a subtitle track, in file order, from the start of the
// file. The indexer's own FormatContext cannot be rewound for this: a seek to
// 0 goes to the track's first index entry, and libavformat keeps one entry
// per timestamp, the position the last cue of that time names. When the
// track's first blocks share a timestamp but sit in two clusters, the seek
// lands in the later cluster and the blocks in the earlier one are never
// read. A karaoke file lost all 38 copies of its first line that way: 38
// blocks at 0:00:00.401 in the first cluster, 401 more at that time in the
// second, and the last of the cues at that time naming the second.
// A demuxer of its own, opened as the indexer's was, reads every block.
void FFMS_Indexer::GetSubtitles(int Track, GetSubtitlesCallback IC, void* ICPrivate)
{
	if (Track < 0 || Track >= (int)FormatContext->nb_streams)
		return;
	AVStream* stream = FormatContext->streams[Track];
	if (stream->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE)
		return;

	AVFormatContext* Reader = nullptr;
	if (avformat_open_input(&Reader, FormatContext->url, FormatContext->iformat, nullptr) != 0)
		return;
	// The same stream numbers as the indexer's, which ran this too
	if (avformat_find_stream_info(Reader, nullptr) < 0 || Track >= (int)Reader->nb_streams ||
		Reader->streams[Track]->codecpar->codec_id != stream->codecpar->codec_id) {
		avformat_close_input(&Reader);
		return;
	}
	for (unsigned int i = 0; i < Reader->nb_streams; i++)
		Reader->streams[i]->discard = ((int)i == Track) ? AVDISCARD_DEFAULT : AVDISCARD_ALL;

	// Times go to the callback in milliseconds, which is what Demux::GetSubtitles
	// reads them as. av_read_frame leaves AVPacket.time_base at {0, 1}: a
	// packet's times are in its stream's time_base, which is 1/1000 only in
	// Matroska with the default TimestampScale. Dividing by
	// AVPacket.time_base.den (1) kept them in those units, so an MP4 or MOV
	// track, an MPEG-TS one (1/90000) or a Matroska file with another
	// TimestampScale gave times hundreds of times too large.
	const AVRational TimeBase = Reader->streams[Track]->time_base;
	// The progress total; FFmpeg leaves a Matroska subtitle track's duration
	// AV_NOPTS_VALUE, which passes through as before.
	const int64_t Total = av_rescale_q_rnd(Reader->streams[Track]->duration, TimeBase, kMilliseconds,
		AVRounding(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
	AVPacket* Packet = av_packet_alloc();
	if (Packet) {
		while (av_read_frame(Reader, Packet) >= 0) {
			if (Packet->stream_index == Track) {
				if (Packet->size < 2) {
					av_packet_unref(Packet);
					continue;
				}
				// On the container's own clock, as FFMS2 gives the video's
				// frame times: no start_time is taken off. A packet without a
				// pts uses its dts, and one with neither starts at 0, where the
				// old arithmetic also put it once Demux cut it to an int.
				int64_t Start = (Packet->pts != AV_NOPTS_VALUE) ? Packet->pts : Packet->dts;
				if (Start == AV_NOPTS_VALUE)
					Start = 0;
				int64_t Duration = Packet->duration;
				if (Duration < 0 || (Start > 0 && Duration > INT64_MAX - Start))
					Duration = 0;
				// Both ends rounded to the nearest millisecond and the duration
				// taken between them, so the end is the nearest millisecond to
				// the packet's end rather than a sum of two roundings.
				const int64_t StartMs = av_rescale_q(Start, TimeBase, kMilliseconds);
				const int64_t EndMs = av_rescale_q(Start + Duration, TimeBase, kMilliseconds);
				if (IC(StartMs, EndMs - StartMs, Total, (char*)Packet->data, ICPrivate) == 1)
					break;
			}
			av_packet_unref(Packet);
		}
		av_packet_unref(Packet);
		av_packet_free(&Packet);
	}
	avformat_close_input(&Reader);
}

const char* FFMS_Indexer::GetSubtitleExtradata(int Track)
{
	AVStream* stream = FormatContext->streams[Track];
	if (stream->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
		if (stream->codecpar->extradata_size) {
			return (char*)stream->codecpar->extradata;
		}
	}
	return nullptr;
}

const char* FFMS_Indexer::GetSubtitleFormat(int Track)
{
	AVCodecID CodecID = FormatContext->streams[Track]->codecpar->codec_id;
	if (CodecID == AV_CODEC_ID_ASS)
		return "ass";
	else if (CodecID == AV_CODEC_ID_SSA)
		return "ssa";
	else {
		const AVCodec *codec = avcodec_find_decoder(CodecID);
		if (codec)
			return codec->name;
	}
	return "";
}

FFMS_API(const char*) FFMS_GetTrackLanguage(FFMS_Indexer* Indexer, int Track) 
{
	return Indexer->GetTrackLanguage(Track);
}

FFMS_API(FFMS_Chapters*) FFMS_GetChapters(FFMS_Indexer* Indexer)
{
	return Indexer->GetChapters();
}

FFMS_API(void) FFMS_FreeChapters(FFMS_Chapters** Chapters)
{
	delete[](*Chapters)->Chapters;
	delete (*Chapters);
}

FFMS_API(FFMS_Attachment*) FFMS_GetAttachment(FFMS_Indexer* Indexer, int Track)
{
	return Indexer->GetAttachment(Track);
}

FFMS_API(void) FFMS_FreeAttachment(FFMS_Attachment** Attachment)
{
	delete (*Attachment);
}

FFMS_API(const char*) FFMS_GetSubtitleExtradata(FFMS_Indexer* Indexer, int Track)
{
	return Indexer->GetSubtitleExtradata(Track);
}

FFMS_API(const char*) FFMS_GetSubtitleFormat(FFMS_Indexer* Indexer, int Track)
{
	return Indexer->GetSubtitleFormat(Track);
}

FFMS_API(void) FFMS_GetSubtitles(FFMS_Indexer* Indexer, int Track, GetSubtitlesCallback IC, void* ICPrivate)
{
	return Indexer->GetSubtitles(Track, IC, ICPrivate);
}

FFMS_API(const char*) FFMS_GetTrackName(FFMS_Indexer* Indexer, int Track)
{
	return Indexer->GetTrackName(Track);
}