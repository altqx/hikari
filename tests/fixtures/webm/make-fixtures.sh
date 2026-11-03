#!/bin/sh
# How the committed files were made (FFmpeg n9.0.2 with libvpx, libaom, libvorbis and libopus).
# Frame n shows n as a 4x4 luma barcode, the one tests/support/media/media_fixture.cpp draws.
set -eu
cd "$(dirname "$0")"
barcode="geq=lum='255*mod(floor(N/pow(2,floor(Y*4/H)*4+floor(X*4/W))),2)':cb=128:cr=128"
frames="color=gray:s=128x96:r=24:d=2,format=yuv420p,$barcode"
tone="sine=frequency=440:sample_rate=48000:duration=2,aformat=channel_layouts=stereo"
encode() {
    out=$1; shift
    ffmpeg -nostdin -loglevel error -y "$@" -map_metadata -1 -fflags +bitexact -flags +bitexact "$out"
}
vp9() { echo -c:v libvpx-vp9 -lossless 1 -g 12 -row-mt 0; }

encode vp8-vorbis.webm -f lavfi -i "$frames" -f lavfi -i "$tone" \
    -c:v libvpx -crf 4 -b:v 2M -g 12 -auto-alt-ref 0 -c:a libvorbis -q:a 2
encode vp9-opus.webm -f lavfi -i "$frames" -f lavfi -i "$tone" $(vp9) -c:a libopus -b:a 48k
encode av1-opus.webm -f lavfi -i "$frames" -f lavfi -i "$tone" \
    -c:v libaom-av1 -aom-params lossless=1 -cpu-used 8 -g 12 -c:a libopus -b:a 48k
# The left half is opaque, the right half transparent.
encode vp9-alpha.webm -f lavfi -i "color=gray:s=128x96:r=24:d=2,format=yuva420p,$barcode:a='255*lt(X,W/2)'" \
    $(vp9) -pix_fmt yuva420p
# Frame intervals 30, 50, 70 ms, repeated, as in the vfr.mkv fixture.
encode vp9-vfr.webm -f lavfi -i "$frames" \
    -vf "settb=1/1000,setpts=(150*floor(N/3)+30*gte(mod(N\,3)\,1)+50*gte(mod(N\,3)\,2))/(1000*TB)" -fps_mode passthrough -enc_time_base 1/1000 $(vp9)
encode tone.opus -f lavfi -i "$tone" -c:a libopus -b:a 48k
encode tone.mka -f lavfi -i "$tone" -c:a libvorbis -q:a 2
