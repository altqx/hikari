#!/bin/sh
# Y9: Matroska files written by real muxers from the sources in sources/,
# for the legacy Demux probe (tools/legacy-capture, target
# legacy_demux_capture) and the rewrite's replay of its observations
# (MatroskaCapture.ReplaysTheLegacyDemuxProbe). mkvmerge (MKVToolNix) muxes
# every file (FFmpeg makes the tiny video track). Neither writes S_TEXT/ASCII,
# the track FFmpeg reads as "text" (both write plain text as S_TEXT/UTF8), so
# that codec is covered only by the byte-written hikari_mkv_fixture files.
# The files are committed; this records how they were made (mkvmerge v102.0,
# FFmpeg n9.0.2); --deterministic keeps reruns byte-identical.
set -eu
cd "$(dirname "$0")"
s=sources
# mkvmerge exits 1 after warnings (it sorts the SubRip cues by start time).
mux() {
    out=$1; shift
    rc=0; mkvmerge -q --deterministic 20261013 -o "$out" "$@" || rc=$?
    [ "$rc" -le 1 ]
}
# A 16x16 FFV1 video, ten frames at 1 fps, so the subtitle packets are read
# interleaved with video packets as in a real release.
ffmpeg -nostdin -loglevel error -y -f lavfi -i color=black:s=16x16:r=1:d=10 -c:v ffv1 -fflags +bitexact -flags +bitexact "$s/video.mkv"
# One ASS track and nothing else, delayed 7 ms: the times are no longer
# whole centiseconds.
mux ass-sync7.mkv --sync 0:7 --language 0:pol --track-name "0:Dialogi" "$s/muxed.ass"
# One ASS track stretched by 1001/1000: odd milliseconds in start and duration.
mux ass-stretch.mkv "$s/video.mkv" --sync 0:0,1001/1000 "$s/muxed.ass"
# Every text codec mkvmerge writes, WebVTT, and attachments: font MIME types
# Demux keeps (font/ttf, font/otf, application/x-truetype-font,
# application/vnd.ms-opentype), one it leaves out, and names with folders.
mux multi.mkv "$s/video.mkv" \
    --language 0:jpn --track-name "0:Signs" "$s/muxed.ass" \
    --language 0:eng --track-name "0:Old SSA" "$s/muxed.ssa" \
    --language 0:pol --track-name "0:Napisy" "$s/muxed.srt" \
    --language 0:fre "$s/muxed.vtt" \
    --attachment-mime-type font/ttf --attachment-name "a.ttf" --attach-file "$s/a.ttf" \
    --attachment-mime-type font/otf --attachment-name "fonts/b.otf" --attach-file "$s/b.otf" \
    --attachment-mime-type application/x-truetype-font --attachment-name 'dir\c.TTF' --attach-file "$s/a.ttf" \
    --attachment-mime-type application/vnd.ms-opentype --attachment-name "..d.otf" --attach-file "$s/b.otf" \
    --attachment-mime-type text/plain --attachment-name "notes.txt" --attach-file "$s/notes.txt"
# No text track Demux reads: video and WebVTT only.
mux webvtt-only.mkv "$s/video.mkv" "$s/muxed.vtt"
# Bytes FFMS2 cannot index.
printf 'not a Matroska file\n' > not-matroska.mkv
rm "$s/video.mkv"
