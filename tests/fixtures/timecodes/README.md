# Timecodes fixtures

Inputs for `hikari_application_timecodes_file_tests` (FA21 #648), each muxed by mkvmerge to check where the frames really start.

- `v1-gaps.txt`: mkvmerge's v1 spelling (`# timestamp format v1`, lowercase `assume`), with ranges that leave gaps.
- `v1-aegisub.txt`: Aegisub's v1 spelling, with a comment and a blank line.
- `v2-fractions.txt`: v2 with fractional milliseconds, a blank line and a comment.
- `v4-unsorted.txt`: v4, whose times are out of order.

`make/run.sh` muxes a test clip with each input (ffmpeg and mkvmerge) and saves mkvextract's v2 list as `<name>.mkvmerge.txt`. `mkvmerge-version.txt` records the version used.
