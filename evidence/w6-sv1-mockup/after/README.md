# SV1 #365 evidence

Real app (out/build/sv1 at 68c2f1024), Linux, Xvfb at 1920x1080 (100 %) and 3840x2160 (QT_SCALE_FACTOR=2), clip 640x360, Rotate Z tool. "before" is qt 6c0d80daa; "after" has Settings > Video > Preview subtitles at = Screen size (sharp).

- crops/: before (top) and after (bottom) at native pixels: full-s2 (full screen, 200 %), full-sign-s1 (full screen, 100 %), panel-sign/line s1 and s2.
- shots/: the whole screens.
- bench.txt, perf-report-subtitles.json: tests/perf/hikari_subtitle_perf (perf.subtitles); heavy script, 24 signs with blur, borders, rotation, fades, vector clips and drawings. Uncalibrated host under a 12-17 load average from other builds, so observations only.
