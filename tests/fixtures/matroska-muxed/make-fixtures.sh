#!/bin/sh
# How the committed files were made (mkvmerge v102.0, FFmpeg n9.0.2). Neither writes S_TEXT/ASCII.
set -eu
cd "$(dirname "$0")"
s=sources
# mkvmerge exits 1 after warnings (it sorts the SubRip cues by start time).
mux() {
    out=$1; shift
    rc=0; mkvmerge -q --deterministic 20261013 -o "$out" "$@" || rc=$?
    [ "$rc" -le 1 ]
}
ffmpeg -nostdin -loglevel error -y -f lavfi -i color=black:s=16x16:r=1:d=10 -c:v ffv1 -fflags +bitexact -flags +bitexact "$s/video.mkv"
mux ass-sync7.mkv --sync 0:7 --language 0:pol --track-name "0:Dialogi" "$s/muxed.ass"
mux ass-stretch.mkv "$s/video.mkv" --sync 0:0,1001/1000 "$s/muxed.ass"
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
mux webvtt-only.mkv "$s/video.mkv" "$s/muxed.vtt"
printf 'not a Matroska file\n' > not-matroska.mkv
rm "$s/video.mkv"
