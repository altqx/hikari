#!/usr/bin/env bash
# H2 (#125) guided listening session on a Linux desktop (PipeWire/PulseAudio).
#
# A person listens to Line audition through the real PortAudio editor output
# (I3/N6, via hikari_h2_audition) and to general playback in the real
# application (N5/V1), including device switch and device loss, and answers
# what they heard. Everything is recorded under ~/hikari-h2-results/<date>/:
# results.json, results.md, the driver's event log, the application's log,
# device lists before and after, and the fixture with its hashes.
#
#   tools/human/h2-listening/h2-session.sh            the real session
#   tools/human/h2-listening/h2-session.sh --dry-run  no sound: a null ALSA
#       device, no desktop audio changes, the app offscreen without audio,
#       answers recorded as not observed. For checking the script itself.
#
# See README.md next to this script.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
BUILD_DIR="$ROOT/out/build/ubuntu-x64-release"
OUT=""
DRY_RUN=0
AUDITION_BIN=""
APP_BIN=""
HELPER_BIN=""
NO_BUILD=0
SKIP_APP=0
SKIP_AUDITION=0

usage() {
    sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'
    cat <<EOF
Options:
  --dry-run            no sound and no desktop audio changes (see above)
  --build-dir DIR      build tree (default: $BUILD_DIR)
  --out DIR            results folder (default: ~/hikari-h2-results/<date>)
  --audition-bin PATH  a prebuilt hikari_h2_audition
  --app PATH           the application (default: <build>/src/app/hikarisub)
  --helper PATH        the media helper (default: <build>/src/helpers/media/hikari-media-helper)
  --no-build           do not build hikari_h2_audition when it is missing
  --skip-app           Line audition part only
  --skip-audition      application part only
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
    --dry-run) DRY_RUN=1 ;;
    --build-dir) BUILD_DIR="$2"; shift ;;
    --out) OUT="$2"; shift ;;
    --audition-bin) AUDITION_BIN="$2"; shift ;;
    --app) APP_BIN="$2"; shift ;;
    --helper) HELPER_BIN="$2"; shift ;;
    --no-build) NO_BUILD=1 ;;
    --skip-app) SKIP_APP=1 ;;
    --skip-audition) SKIP_AUDITION=1 ;;
    -h | --help) usage; exit 0 ;;
    *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

APP_BIN="${APP_BIN:-$BUILD_DIR/src/app/hikarisub}"
HELPER_BIN="${HELPER_BIN:-$BUILD_DIR/src/helpers/media/hikari-media-helper}"
AUDITION_BIN="${AUDITION_BIN:-$BUILD_DIR/tools/human/h2-listening/hikari_h2_audition}"

# ---------------------------------------------------------------- terminal

if [[ -t 1 ]]; then
    B=$'\e[1m' R=$'\e[31m' G=$'\e[32m' Y=$'\e[33m' C=$'\e[36m' N=$'\e[0m'
else
    B="" R="" G="" Y="" C="" N=""
fi
say() { printf '%s\n' "$*"; }
info() { printf '%s\n' "${C}$*${N}"; }
warn() { printf '%s\n' "${Y}$*${N}"; }
die() { printf '%s\n' "${R}error: $*${N}" >&2; exit 1; }
heading() { printf '\n%s\n%s\n' "${B}== $* ==${N}" ""; }
now_iso() { date --iso-8601=ns | sed 's/,\([0-9]\{3\}\)[0-9]*/.\1/'; }

pause_enter() {
    if ((DRY_RUN)); then return 0; fi
    read -r -p "${B}${1:-Press Enter to continue.}${N} " _ </dev/tty
}

# ---------------------------------------------------------------- preflight

for tool in jq python3; do
    command -v "$tool" >/dev/null || die "$tool is required"
done
command -v ffmpeg >/dev/null || die "ffmpeg is required to make the fixture video"
HAVE_PACTL=0
command -v pactl >/dev/null && HAVE_PACTL=1
((HAVE_PACTL || DRY_RUN)) || die "pactl (pipewire-pulse or pulseaudio-utils) is required for the device steps"

[[ -x "$HELPER_BIN" ]] || die "media helper not found: $HELPER_BIN (build the project first)"
if ((!SKIP_APP)); then [[ -x "$APP_BIN" ]] || die "application not found: $APP_BIN"; fi
if ((!SKIP_AUDITION)) && [[ ! -x "$AUDITION_BIN" ]]; then
    if ((NO_BUILD)); then die "hikari_h2_audition not found: $AUDITION_BIN"; fi
    info "Building hikari_h2_audition (one small target) in $BUILD_DIR ..."
    cmake --build "$BUILD_DIR" --target hikari_h2_audition || die "could not build hikari_h2_audition"
    [[ -x "$AUDITION_BIN" ]] || die "hikari_h2_audition was built but is not at $AUDITION_BIN"
fi

DATE="$(date +%F)"
if [[ -z "$OUT" ]]; then
    if ((DRY_RUN)); then OUT="${TMPDIR:-/tmp}/hikari-h2-dryrun/$DATE"; else OUT="$HOME/hikari-h2-results/$DATE"; fi
fi
if [[ -e "$OUT/results.json" ]]; then
    n=2
    while [[ -e "$OUT-$n/results.json" ]]; do n=$((n + 1)); done
    OUT="$OUT-$n"
fi
mkdir -p "$OUT"/{env,fixture,app} || die "cannot create $OUT"
OUT="$(cd "$OUT" && pwd)"
STEPS="$OUT/steps.jsonl"
ACTIONS="$OUT/actions.log"
: >"$STEPS"
: >"$ACTIONS"
SESSION_STARTED="$(now_iso)"

# ---------------------------------------------------------------- desktop audio actions

# Desktop changes go through act(): logged, skipped in a dry run, undone on exit.
act() {
    printf '%s %s %s\n' "$(now_iso)" "$( ((DRY_RUN)) && echo would-run || echo run)" "$*" >>"$ACTIONS"
    if ((DRY_RUN)); then
        info "(dry run, not run) $*"
        return 0
    fi
    "$@"
}

ORIGINAL_SINK=""
declare -A ORIGINAL_PROFILE=()
if ((HAVE_PACTL)); then ORIGINAL_SINK="$(pactl get-default-sink 2>/dev/null || true)"; fi

restore_desktop() {
    ((DRY_RUN)) && return 0
    local card
    for card in "${!ORIGINAL_PROFILE[@]}"; do
        pactl set-card-profile "$card" "${ORIGINAL_PROFILE[$card]}" 2>/dev/null &&
            printf '%s restore card %s %s\n' "$(now_iso)" "$card" "${ORIGINAL_PROFILE[$card]}" >>"$ACTIONS"
    done
    ORIGINAL_PROFILE=()
    if [[ -n "$ORIGINAL_SINK" && "$(pactl get-default-sink 2>/dev/null)" != "$ORIGINAL_SINK" ]]; then
        pactl set-default-sink "$ORIGINAL_SINK" 2>/dev/null &&
            printf '%s restore default sink %s\n' "$(now_iso)" "$ORIGINAL_SINK" >>"$ACTIONS"
    fi
}

sinks_json() { ((HAVE_PACTL)) && pactl -f json list sinks 2>/dev/null || echo '[]'; }
cards_json() { ((HAVE_PACTL)) && pactl -f json list cards 2>/dev/null || echo '[]'; }

# ---------------------------------------------------------------- the PortAudio driver

DRIVER_LOG="$OUT/driver.jsonl"
DRIVER_PID=""
DRIVER_HOME=""
LOOP_PID=""
APP_PID=""

driver_start() {
    : >"$DRIVER_LOG"
    local fifo="$OUT/.driver.in"
    rm -f "$fifo"
    mkfifo "$fifo"
    if ((DRY_RUN)); then
        # A private ALSA configuration whose null PCM swallows everything.
        DRIVER_HOME="$(mktemp -d)"
        printf 'pcm.hikari_null {\n    type null\n}\n' >"$DRIVER_HOME/.asoundrc"
        HOME="$DRIVER_HOME" "$AUDITION_BIN" --helper "$HELPER_BIN" <"$fifo" >"$DRIVER_LOG" 2>"$OUT/driver.stderr.log" &
    else
        "$AUDITION_BIN" --helper "$HELPER_BIN" <"$fifo" >"$DRIVER_LOG" 2>"$OUT/driver.stderr.log" &
    fi
    DRIVER_PID=$!
    exec 7>"$fifo"
    rm -f "$fifo"
    driver_wait ready 0 10 >/dev/null || die "the audition driver did not start (see $OUT/driver.stderr.log)"
}

drv() { printf '%s\n' "$*" >&7; }
drv_lines() { wc -l <"$DRIVER_LOG"; }

# driver_wait <event regex> <from line> <timeout s>: prints the first matching event after the line.
driver_wait() {
    local pattern="$1" from="$2" timeout="$3" deadline line
    deadline=$(($(date +%s%N) + timeout * 1000000000))
    while (($(date +%s%N) < deadline)); do
        line="$(tail -n +"$((from + 1))" "$DRIVER_LOG" 2>/dev/null | jq -c --arg p "^($pattern)\$" 'select(.event | test($p))' 2>/dev/null | head -n 1)"
        if [[ -n "$line" ]]; then
            printf '%s\n' "$line"
            return 0
        fi
        sleep 0.1
    done
    return 1
}

# Events after a line, as a JSON array (glitch floods are summarized by count).
driver_events() {
    local from="$1" to="${2:-}"
    if [[ -z "$to" ]]; then to="$(drv_lines)"; fi
    sed -n "$((from + 1)),${to}p" "$DRIVER_LOG" | jq -s -c '
        (map(select(.event == "glitch")) | length) as $g
        | map(select(.event != "glitch" and .event != "devices"))
          + (if $g > 0 then [{event: "glitch-summary", count: $g}] else [] end)'
}

driver_devices() {
    local from
    from="$(drv_lines)"
    drv devices
    driver_wait devices "$from" 15
}

driver_stop() {
    loop_stop
    if [[ -n "$DRIVER_PID" ]]; then
        drv quit 2>/dev/null
        exec 7>&-
        for _ in $(seq 30); do kill -0 "$DRIVER_PID" 2>/dev/null || break; sleep 0.1; done
        kill "$DRIVER_PID" 2>/dev/null
        DRIVER_PID=""
    fi
    [[ -n "$DRIVER_HOME" ]] && rm -rf "$DRIVER_HOME"
    DRIVER_HOME=""
}

# Keeps the whole fixture playing (it is 20 s long) until loop_stop.
loop_start() {
    loop_stop
    local from
    from="$(drv_lines)"
    drv play 0 20000
    (
        while sleep 0.2; do
            local n
            n="$(drv_lines)"
            if tail -n +"$((from + 1))" "$DRIVER_LOG" 2>/dev/null | jq -e -s 'any(.[]; .event == "finished" and .drained)' >/dev/null 2>&1; then
                from="$n"
                drv play 0 20000
            fi
        done
    ) &
    LOOP_PID=$!
}

loop_stop() {
    if [[ -n "$LOOP_PID" ]]; then
        kill "$LOOP_PID" 2>/dev/null
        wait "$LOOP_PID" 2>/dev/null
        LOOP_PID=""
        drv stop 2>/dev/null
    fi
}

cleanup() {
    loop_stop
    if [[ -n "$APP_PID" ]] && kill -0 "$APP_PID" 2>/dev/null; then kill "$APP_PID" 2>/dev/null; fi
    driver_stop
    restore_desktop
}
trap cleanup EXIT
trap 'exit 130' INT TERM

# ---------------------------------------------------------------- answers

# record <id> <part> <title> <expected> <answer> <note> <started> <events json> [extra json]
record() {
    local extra="${9:-}"
    [[ -n "$extra" ]] || extra='{}'
    jq -n -c --arg id "$1" --arg part "$2" --arg title "$3" --arg expected "$4" --arg answer "$5" \
        --arg note "$6" --arg started "$7" --arg answered "$(now_iso)" --argjson events "${8:-[]}" \
        --argjson extra "$extra" \
        '{id: $id, part: $part, title: $title, expected: $expected, answer: $answer, note: $note,
          started: $started, answered: $answered, driverEvents: $events} + $extra' >>"$STEPS"
    case "$5" in
    yes) say "${G}recorded: yes${N}" ;;
    no) say "${R}recorded: no${N}" ;;
    *) say "${Y}recorded: $5${N}" ;;
    esac
}

# ask <question>: sets ANSWER (yes|no|skipped|replay|not-observed) and NOTE.
ask() {
    ANSWER="" NOTE=""
    if ((DRY_RUN)); then
        ANSWER="not-observed" NOTE="dry run: nothing was played to a listener"
        return
    fi
    local reply
    while true; do
        read -r -p "${B}$1 [y]es / [n]o / [r]eplay / [s]kip: ${N}" reply </dev/tty
        case "${reply,,}" in
        y | yes) ANSWER=yes; break ;;
        n | no) ANSWER=no; break ;;
        r | replay) ANSWER=replay; return ;;
        s | skip) ANSWER=skipped; break ;;
        esac
    done
    read -r -p "Note (what you heard; Enter for none): " NOTE </dev/tty
}

# choose <prompt> <options...>: sets CHOICE to the chosen option ("" when skipped).
choose() {
    local prompt="$1"
    shift
    CHOICE=""
    if ((DRY_RUN)); then
        CHOICE="${1:-}"
        return
    fi
    local i=1 opt reply
    for opt in "$@"; do say "  $i) $opt"; i=$((i + 1)); done
    say "  s) skip"
    while true; do
        read -r -p "${B}$prompt ${N}" reply </dev/tty
        [[ "$reply" == s ]] && return
        if [[ "$reply" =~ ^[0-9]+$ ]] && ((reply >= 1 && reply <= $#)); then
            CHOICE="${!reply}"
            return
        fi
    done
}

# audition_step <id> <title> <expected> <start ms> <end ms>
audition_step() {
    local id="$1" title="$2" expected="$3" start="$4" end="$5" from started report
    heading "$id  $title"
    say "Plays [$start ms, $end ms) of the fixture through the PortAudio output."
    say "Expected: $expected"
    while true; do
        pause_enter "Press Enter to play."
        started="$(now_iso)"
        from="$(drv_lines)"
        drv play "$start" "$end"
        report="$(driver_wait 'finished|failed|error' "$from" $(((end - start) / 1000 + 10)))" || report=""
        if [[ -n "$report" ]]; then
            say "driver: $(jq -r '[.event, (if .drained != null then "drained=\(.drained) frames=\(.outputFrames) rate=\(.outputRate) tail=\(.tailAfterLogicalStopMs|floor)ms latency=\(.outputLatency*1000|floor)ms underruns=\(.underruns)" else (.reason // .error) end)] | join(" ")' <<<"$report")"
        else
            warn "driver: no report within the expected time"
        fi
        ask "Did you hear exactly that?"
        [[ "$ANSWER" == replay ]] && continue
        record "$id" audition "$title" "$expected" "$ANSWER" "$NOTE" "$started" "$(driver_events "$from")"
        return
    done
}

# ---------------------------------------------------------------- session

heading "H2 listening session ($( ((DRY_RUN)) && echo "DRY RUN, no sound" || echo live))"
say "Results: $OUT"
say "Build:   $BUILD_DIR ($(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown))"

# Environment and device lists before anything plays.
{
    echo "session: $SESSION_STARTED"
    echo "git: $(git -C "$ROOT" rev-parse HEAD 2>/dev/null) $(git -C "$ROOT" status --porcelain 2>/dev/null | wc -l) changed files"
    echo "kernel: $(uname -srvm)"
    echo "desktop: ${XDG_CURRENT_DESKTOP:-?} session ${XDG_SESSION_TYPE:-?} wayland ${WAYLAND_DISPLAY:-none} display ${DISPLAY:-none}"
    [[ -r /etc/os-release ]] && grep -E '^(PRETTY_NAME|VERSION_ID)=' /etc/os-release
    command -v pipewire >/dev/null && pipewire --version 2>/dev/null | tail -n 1
    command -v wireplumber >/dev/null && wireplumber --version 2>/dev/null | tail -n 1
    ((HAVE_PACTL)) && pactl info 2>/dev/null
} >"$OUT/env/system.txt"
sinks_json >"$OUT/env/sinks-before.json"
cards_json >"$OUT/env/cards-before.json"
command -v wpctl >/dev/null && wpctl status >"$OUT/env/wpctl-before.txt" 2>&1

info "Making the quiet fixture (about -24 dBFS peak) ..."
python3 "$HERE/make_fixture.py" "$OUT/fixture" >"$OUT/fixture/levels.json" || die "fixture generation failed"
FIX="$OUT/fixture"
say "Fixture levels (dBFS peak): $(cat "$FIX/levels.json")"
jq -e '[.[]] | all(. <= -20)' "$FIX/levels.json" >/dev/null || die "fixture louder than -20 dBFS; refusing to play it"

if ((!SKIP_AUDITION)); then
    driver_start
    DEVICES="$(driver_devices)" || die "the driver did not list devices"
    jq '.devices' <<<"$DEVICES" >"$OUT/env/portaudio-devices-before.json"
    say "PortAudio outputs (the app's PortAudio owner):"
    jq -r '.devices[] | "  \(.id)  [\(.hostApi)\(if .isDefault then ", default" else "" end), \(.maxChannels) ch, \(.defaultSampleRate|floor) Hz]"' <<<"$DEVICES"
fi
if ((HAVE_PACTL)); then
    say "Desktop sinks (pactl), default: $ORIGINAL_SINK"
    jq -r '.[] | "  \(.name)  \(.description)  [\(.state), volume \(.volume | to_entries | map(.value.value_percent) | first)]"' "$OUT/env/sinks-before.json"
fi

heading "Volume first"
warn "${B}Before any sound plays: set a LOW volume.${N}"
say "The fixture is quiet (peaks at -24 dBFS), but your output chain may be loud."
say "Turn the desktop volume down to about 20-30 %, and hold headphones away from"
say "your ears for the first sound; bring them closer once you know the level."
say "Raw hardware devices (names ending in (hw:X,Y)) bypass the desktop volume;"
say "the session plays them 12 dB quieter, but still start low."
if ((HAVE_PACTL)) && command -v wpctl >/dev/null; then
    say "Current default output volume: $(wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null)"
fi
if ((!DRY_RUN)); then
    while true; do
        read -r -p "${B}Type 'low' once the volume is low: ${N}" reply </dev/tty
        [[ "$reply" == low ]] && break
    done
fi
if ((DRY_RUN)); then
    record setup.volume setup "Volume set low before any sound" "the listener confirms a low volume" not-observed "dry run" "$SESSION_STARTED"
else
    record setup.volume setup "Volume set low before any sound" "the listener confirms a low volume" yes \
        "default output $(wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null || echo "volume unknown")" "$SESSION_STARTED"
fi

# ---------------------------------------------------------------- part A: Line audition (PortAudio)

if ((!SKIP_AUDITION)); then
    heading "Part A: Line audition through the PortAudio editor output"
    say "The application does not offer Line audition yet (it arrives with the Audio"
    say "playback card), so this part plays the fixture through the same backend"
    say "objects (PortAudioOutput + LineAudition + the media helper) from a small driver."
    say ""
    say "The fixture has three Lines (3-5 s, 9-11 s, 15-17 s). Around each Line:"
    say "  1 s before the Line   a steady LOW HUM"
    say "  the Line itself       a RISING WHOOP (a 2 s sweep from low to high)"
    say "  1 s after the Line    EIGHT FAST TICKS"
    say "The file starts with three short beeps; everything else is silence."

    # A1: the output device. The desktop route (ALSA 'pipewire', 'pulse' or 'default') follows the desktop volume.
    route_ids=()
    if ((DRY_RUN)); then
        route_ids=("$(jq -r '.devices[] | select(.name == "hikari_null") | .id' <<<"$DEVICES")")
        [[ -n "${route_ids[0]}" ]] || die "dry run: the null ALSA PCM did not appear"
    else
        mapfile -t route_ids < <(jq -r '.devices[] | select(.name == "pipewire" or .name == "pulse" or .name == "default") | .id' <<<"$DEVICES")
        mapfile -t other_ids < <(jq -r '.devices[] | select(.name != "pipewire" and .name != "pulse" and .name != "default") | .id' <<<"$DEVICES")
        route_ids+=("${other_ids[@]}")
    fi
    heading "A1  Output device"
    say "Choose the output for the audition steps. The desktop routes (pipewire, pulse,"
    say "default) play on the desktop's default output at the desktop volume."
    choose "Device:" "${route_ids[@]}"
    DEVICE="$CHOICE"
    if [[ -z "$DEVICE" ]]; then
        record A1.device audition "Open the PortAudio output" "a device opens" skipped "no device chosen" "$(now_iso)"
    else
        from="$(drv_lines)"
        started="$(now_iso)"
        if [[ "$DEVICE" == *"(hw:"* ]]; then drv gain -12; else drv gain 0; fi
        drv open "$DEVICE"
        opened="$(driver_wait 'opened|error' "$from" 10)" || opened='{"event":"error","error":"timeout"}'
        say "driver: $opened"
        drv media "$FIX/h2-48k.mkv"
        media="$(driver_wait 'media|error' "$from" 60)" || media='{"event":"error","error":"timeout"}'
        record A1.device audition "Open the PortAudio output and the 48 kHz fixture" \
            "the device opens at a negotiated format; the fixture's audio opens" \
            "$(jq -r 'if .event == "opened" then "yes" else "no" end' <<<"$opened")" "$DEVICE" "$started" \
            "$(driver_events "$from")"

        audition_step A2.line "Play the Line (Line 1, 3.000-5.000 s)" \
            "one rising whoop, about 2 s; no hum before it, no ticks after it; no click at either end" 3000 5000
        audition_step A3.before "Play 500 ms before the start (2.500-3.000 s)" \
            "only the low hum, half a second; no whoop" 2500 3000
        audition_step A4.first "Play the first 500 ms (3.000-3.500 s)" \
            "the low beginning of the whoop only; no hum" 3000 3500
        audition_step A5.last "Play the last 500 ms (4.500-5.000 s)" \
            "the high end of the whoop only; no ticks" 4500 5000
        audition_step A6.after "Play 500 ms after the end (5.000-5.500 s)" \
            "four fast ticks only; no whoop" 5000 5500

        heading "A7  Resampled source (44.1 kHz fixture on the 48 kHz output)"
        say "First Line 2 from the 48 kHz file as a reference, then the same Line from the 44.1 kHz"
        say "file, which the media helper resamples to the output's rate. They should sound identical."
        audition_step A7.reference "Reference: Line 2 from the 48 kHz fixture (9.000-11.000 s)" \
            "the same whoop as in A2" 9000 11000
        from="$(drv_lines)"
        drv media "$FIX/h2-44k1.mkv"
        driver_wait 'media|error' "$from" 60 >/dev/null
        audition_step A7.resampled "Line 2 from the 44.1 kHz fixture (9.000-11.000 s)" \
            "identical to the reference: same pitch, same length, no crackle" 9000 11000

        heading "A8  Stop while playing"
        say "Plays Line 1 onward to the end of the file; press Enter while the whoop sounds."
        say "Expected: the sound stops at once (within a fraction of a second); no lingering tail, no click."
        while true; do
            pause_enter "Press Enter to start."
            started="$(now_iso)"
            from="$(drv_lines)"
            drv play 3000 20000
            if ((DRY_RUN)); then sleep 0.2; else read -r -p "${B}Press Enter to STOP.${N} " _ </dev/tty; fi
            stop_at="$(now_iso)"
            drv stop
            report="$(driver_wait 'finished|failed' "$from" 5)" || report='{"event":"no report"}'
            say "driver: $(jq -r '"\(.event) drained=\(.drained) logicalStopAt=\(.logicalStopAt)"' <<<"$report" 2>/dev/null)"
            ask "Did the sound stop at once when you pressed Enter?"
            [[ "$ANSWER" == replay ]] && continue
            record A8.stop audition "Stop during playback" "sound stops promptly; the report says not drained" \
                "$ANSWER" "$NOTE" "$started" "$(driver_events "$from")" "$(jq -n --arg s "$stop_at" '{stopPressed: $s}')"
            break
        done

        # A9: the desktop's default output changes while the desktop route plays.
        heading "A9  Default output changed while playing"
        if [[ "$DEVICE" == *"(hw:"* ]] && ((!DRY_RUN)); then
            say "A9 needs a desktop route (pipewire/pulse/default); the chosen device is a raw hardware device."
            record A9.switch audition "Default output changed while playing" "-" skipped "raw hardware device chosen in A1" "$(now_iso)"
        else
            mapfile -t others < <(jq -r --arg d "$ORIGINAL_SINK" '.[] | select(.name != $d) | .name' "$OUT/env/sinks-before.json")
            ((DRY_RUN)) && others=("dry-run-other-sink")
            say "The fixture loops through the PortAudio output; then the desktop default output"
            say "changes from $ORIGINAL_SINK to the sink you choose. Listen to both devices."
            choose "Switch to:" "${others[@]}"
            target="$CHOICE"
            if [[ -z "$target" ]]; then
                record A9.switch audition "Default output changed while playing" "-" skipped "no other sink" "$(now_iso)"
            else
                pause_enter "Press Enter to start playing (then wait until you hear it)."
                started="$(now_iso)"
                from="$(drv_lines)"
                loop_start
                pause_enter "Press Enter to switch the default output now."
                switch_at="$(now_iso)"
                act pactl set-default-sink "$target"
                ((DRY_RUN)) && sleep 0.5 || sleep 4
                pause_enter "Listen for a few seconds, then press Enter to stop."
                loop_stop
                driver_wait 'finished|failed' "$from" 5 >/dev/null
                say "What happened? Expected per the contract: nothing certified yet; record what you hear:"
                say "  did the sound move to the new device, stay on the old one, or stop? any glitch or gap?"
                ask "Did the sound continue (on either device) without a gap or glitch?"
                [[ "$ANSWER" == replay ]] && ANSWER=skipped
                act pactl set-default-sink "$ORIGINAL_SINK"
                record A9.switch audition "Default output changed while playing (desktop route)" \
                    "observation: where the sound goes after the default changes, and any gap" "$ANSWER" "$NOTE" "$started" \
                    "$(driver_events "$from")" "$(jq -n --arg s "$switch_at" --arg t "$target" --arg o "$ORIGINAL_SINK" '{switchAt: $s, from: $o, to: $t}')"
            fi
        fi

        # A10: an explicitly chosen other output.
        heading "A10  Choose another output"
        say "Reopen the output on a different PortAudio device (another sink, or a raw hardware"
        say "device; raw devices play 12 dB quieter and bypass the desktop volume)."
        mapfile -t alt < <(jq -r --arg d "$DEVICE" '.devices[] | select(.id != $d) | .id' <<<"$DEVICES")
        ((DRY_RUN)) && alt=("$DEVICE")
        choose "Device:" "${alt[@]}"
        ALT="$CHOICE"
        if [[ -z "$ALT" ]]; then
            record A10.choose audition "Play Line 1 on another chosen output" "-" skipped "none chosen" "$(now_iso)"
        else
            from="$(drv_lines)"
            if [[ "$ALT" == *"(hw:"* ]]; then drv gain -12; else drv gain 0; fi
            drv open "$ALT"
            say "driver: $(driver_wait 'opened|error' "$from" 10 || echo 'no answer')"
            drv media "$FIX/h2-48k.mkv"
            driver_wait 'media|error' "$from" 60 >/dev/null
            audition_step A10.choose "Play Line 1 on $ALT" "the whoop from that device (and not from the previous one)" 3000 5000
        fi

        # A11: the device disappears while playing, then comes back.
        heading "A11  Device lost while playing, then reconnected"
        say "Pick how the device goes away:"
        say "  unplug        you unplug a USB/Bluetooth output yourself"
        say "  disable-card  this script switches a sound card's profile to 'off' and back"
        if ((DRY_RUN)); then
            how=simulate
        else
            choose "Method:" unplug disable-card
            how="$CHOICE"
        fi
        if [[ -z "$how" ]]; then
            record A11.loss audition "Device lost while playing" "-" skipped "skipped" "$(now_iso)"
        else
            say "Choose the output to play on while it goes away. A raw hardware device (hw:X,Y) of the"
            say "card you remove shows a PortAudio device loss; a desktop route shows what the desktop does."
            mapfile -t lossdev < <(jq -r '.devices[].id' <<<"$DEVICES")
            if ((DRY_RUN)); then CHOICE="$DEVICE"; else choose "Device:" "${lossdev[@]}"; fi
            LOSSDEV="$CHOICE"
            card=""
            if [[ "$how" == disable-card ]]; then
                mapfile -t cardnames < <(jq -r '.[].name' "$OUT/env/cards-before.json")
                choose "Card to disable:" "${cardnames[@]}"
                card="$CHOICE"
            fi
            if [[ -z "$LOSSDEV" || ("$how" == disable-card && -z "$card") ]]; then
                record A11.loss audition "Device lost while playing" "-" skipped "no device or card chosen" "$(now_iso)"
            else
                from="$(drv_lines)"
                if [[ "$LOSSDEV" == *"(hw:"* ]]; then drv gain -12; else drv gain 0; fi
                drv open "$LOSSDEV"
                say "driver: $(driver_wait 'opened|error' "$from" 10 || echo 'no answer')"
                drv media "$FIX/h2-48k.mkv"
                driver_wait 'media|error' "$from" 60 >/dev/null
                pause_enter "Press Enter to start playing (then wait until you hear it)."
                started="$(now_iso)"
                if [[ "$how" == simulate ]]; then
                    # The null device is unpaced: let one play drain, then fail
                    # the stream while its callbacks run on silence.
                    drv play 0 20000
                    driver_wait finished "$from" 10 >/dev/null
                else
                    loop_start
                fi
                case "$how" in
                unplug) pause_enter "Unplug the device now, then press Enter." ;;
                disable-card)
                    pause_enter "Press Enter to switch $card off."
                    ORIGINAL_PROFILE["$card"]="$(jq -r --arg c "$card" '.[] | select(.name == $c) | .active_profile' "$OUT/env/cards-before.json")"
                    act pactl set-card-profile "$card" off
                    ;;
                simulate) drv simulate-loss ;;
                esac
                lost_at="$(now_iso)"
                lost="$(driver_wait 'device-lost|failed' "$from" 10)" || lost=""
                loop_stop
                say "driver: ${lost:-no device-loss report within 10 s}"
                ask "Did the sound stop (or move) without the driver hanging, crashing or looping noise?"
                [[ "$ANSWER" == replay ]] && ANSWER=skipped
                record A11.loss audition "Device lost while playing ($how)" \
                    "the output reports the loss (device-lost/failed) and goes quiet; no hang, no noise" "$ANSWER" "$NOTE" \
                    "$started" "$(driver_events "$from")" "$(jq -n --arg l "$lost_at" --arg d "$LOSSDEV" --arg c "$card" '{lostAt: $l, device: $d, card: $c}')"
                sinks_json >"$OUT/env/sinks-while-lost.json"

                case "$how" in
                unplug) pause_enter "Plug the device back in, wait until the desktop shows it, then press Enter." ;;
                disable-card)
                    pause_enter "Press Enter to switch $card back on."
                    act pactl set-card-profile "$card" "${ORIGINAL_PROFILE[$card]}"
                    unset 'ORIGINAL_PROFILE[$card]'
                    sleep 2
                    ;;
                esac
                from="$(drv_lines)"
                started="$(now_iso)"
                DEVICES2="$(driver_devices)" || DEVICES2='{"devices":[]}'
                jq '.devices' <<<"$DEVICES2" >"$OUT/env/portaudio-devices-after-reconnect.json"
                drv open "$LOSSDEV"
                reopened="$(driver_wait 'opened|error' "$from" 10)" || reopened='{"event":"error","error":"timeout"}'
                say "reopen by id: $reopened"
                drv media "$FIX/h2-48k.mkv"
                driver_wait 'media|error' "$from" 60 >/dev/null
                record A11.reopen audition "Reopen the same device id after reconnecting" "the id resolves again and opens" \
                    "$(jq -r 'if .event == "opened" then "yes" else "no" end' <<<"$reopened")" "$LOSSDEV" "$started" "$(driver_events "$from")"
                audition_step A11.after "Play Line 1 after reconnecting" "the whoop, from the reconnected device" 3000 5000
            fi
        fi
    fi
    driver_stop
fi

# ---------------------------------------------------------------- part B: the application

if ((!SKIP_APP)); then
    heading "Part B: General playback in the application"
    APPDIR="$OUT/app"
    mkdir -p "$APPDIR"/{config,data,cache}
    say "The application starts with private settings in $APPDIR (your own settings are untouched)"
    say "on $FIX/h2-script.ass, whose Script Info names the fixture video."
    APP_ENV=(XDG_CONFIG_HOME="$APPDIR/config" XDG_DATA_HOME="$APPDIR/data" XDG_CACHE_HOME="$APPDIR/cache"
        QT_MESSAGE_PATTERN='%{time yyyy-MM-ddTHH:mm:ss.zzz} %{type} %{category}: %{message}'
        QT_LOGGING_RULES='qt.multimedia.pipewire.audiosink=true;qt.multimedia.pipewire.devicemonitor=true;qt.multimedia.pulseaudio.output=true;qt.multimedia.pulseaudio.engine=true;qt.multimedia.ffmpeg.audiorenderer=true')
    if ((DRY_RUN)); then
        # No window and no reachable audio server: the player has no output device.
        mkdir -p -m 700 "$APPDIR/runtime"
        APP_ENV+=(QT_QPA_PLATFORM=offscreen PULSE_SERVER=unix:/nonexistent PIPEWIRE_REMOTE=/nonexistent
            XDG_RUNTIME_DIR="$APPDIR/runtime" HOME="$APPDIR")
    fi
    started="$(now_iso)"
    env "${APP_ENV[@]}" "$APP_BIN" "$FIX/h2-script.ass" >"$APPDIR/app.log" 2>&1 &
    APP_PID=$!
    sleep 3
    if kill -0 "$APP_PID" 2>/dev/null; then app_up=running; else app_up="exited early (see app/app.log)"; fi
    if ((DRY_RUN)); then
        # Offscreen: the process starting is all a dry run can show.
        record B0.launch app "Application starts on the fixture script" "the process starts (offscreen)" \
            "$( [[ "$app_up" == running ]] && echo yes || echo no)" "dry run, offscreen: $app_up" "$started"
    else
        ask "Did the window open with the three Lines in the Grid?"
        [[ "$ANSWER" == replay ]] && ANSWER=skipped
        record B0.launch app "Application starts on the fixture script" "the window opens with the three Lines" \
            "$ANSWER" "$NOTE [process: $app_up]" "$started"
    fi

    app_step() { # <id> <title> <instructions> <expected>
        local id="$1" title="$2" how="$3" expected="$4" started
        heading "$id  $title"
        say "$how"
        say "Expected: $expected"
        started="$(now_iso)"
        while true; do
            ask "Did you hear/see exactly that?"
            [[ "$ANSWER" == replay ]] && { say "Do it again, then answer."; continue; }
            break
        done
        if ! kill -0 "$APP_PID" 2>/dev/null && ((!DRY_RUN)); then NOTE="$NOTE [the application is no longer running]"; fi
        record "$id" app "$title" "$expected" "$ANSWER" "$NOTE" "$started" '[]'
    }

    app_step B1.video "Load the associated video" \
        "If the Video panel offers the associated files, accept them (load). Wait until frame 0 shows the test pattern with its counter." \
        "the counting test video appears; the first Line's subtitle shows at 3-5 s"
    app_step B2.audition-ui "Line audition in the application" \
        "Look at the Audio panel and its menus for 'Play line' / 'Play 500 ms before' etc. (This build is not expected to have them yet.)" \
        "answer 'no' if the application offers no Line audition; describe anything you find"
    app_step B3.play "Play from the start" \
        "Press Play in the Video panel (or Space with the video focused) and let it run past 6 s." \
        "three beeps at once, then hum (2-3 s), whoop (3-5 s) while Line 1 shows, ticks (5-6 s); sound in step with the counter"
    app_step B4.pause "Pause and Stop" \
        "While the sound plays, press Pause; then press Play again; then Stop." \
        "Pause silences at once; Play resumes from the paused place; Stop silences and returns to frame 0"
    app_step B5.seek "Seek to a Line and play" \
        "Click Line 2 in the Grid (the video jumps to 9.00 s), press Play for 3 s, Pause. Then drag the seek bar to about 14 s and Play." \
        "after clicking Line 2: the whoop straight away (no hum first); after seeking to 14 s: hum, then whoop at 15 s"
    if ((HAVE_PACTL)) || ((DRY_RUN)); then
        heading "B6  Default output changed while the application plays"
        mapfile -t others < <(jq -r --arg d "$ORIGINAL_SINK" '.[] | select(.name != $d) | .name' "$OUT/env/sinks-before.json")
        ((DRY_RUN)) && others=("dry-run-other-sink")
        choose "Switch to:" "${others[@]}"
        target="$CHOICE"
        if [[ -n "$target" ]]; then
            say "Press Play in the application (from the start) and wait until you hear it."
            pause_enter "Then press Enter here to switch the default output to $target."
            switch_at="$(now_iso)"
            act pactl set-default-sink "$target"
            app_step B6.switch "Default output changed while playing" \
                "Listen to both devices for a few seconds, then Pause." \
                "observation: the sound moves to $target, stays on the old device, or stops; record any gap or glitch"
            act pactl set-default-sink "$ORIGINAL_SINK"
        else
            record B6.switch app "Default output changed while playing" "-" skipped "no other sink" "$(now_iso)"
        fi
        heading "B7  Device lost while the application plays"
        if ((DRY_RUN)); then CHOICE=disable-card; else choose "Method:" unplug disable-card; fi
        how="$CHOICE"
        if [[ -n "$how" ]]; then
            card=""
            if [[ "$how" == disable-card ]]; then
                mapfile -t cardnames < <(jq -r '.[].name' "$OUT/env/cards-before.json")
                ((DRY_RUN)) && cardnames=("dry-run-card")
                choose "Card to disable (the one playing):" "${cardnames[@]}"
                card="$CHOICE"
            fi
            say "Press Play in the application and wait until you hear it."
            case "$how" in
            unplug) pause_enter "Unplug the playing device now, then press Enter." ;;
            disable-card)
                pause_enter "Press Enter to switch $card off."
                ORIGINAL_PROFILE["$card"]="$(jq -r --arg c "$card" '.[] | select(.name == $c) | .active_profile' "$OUT/env/cards-before.json")"
                act pactl set-card-profile "$card" off
                ;;
            esac
            app_step B7.loss "Device lost while playing" "Watch the application for a few seconds (video still moving? window responsive?)." \
                "observation: the sound stops or moves; the application keeps running and stays responsive"
            case "$how" in
            unplug) pause_enter "Plug the device back in, wait until the desktop shows it, then press Enter." ;;
            disable-card)
                act pactl set-card-profile "$card" "${ORIGINAL_PROFILE[$card]}"
                unset 'ORIGINAL_PROFILE[$card]'
                ;;
            esac
            app_step B7.after "Play again after reconnecting" "Pause (if needed), then Play." \
                "sound again, on the device you expect (record which one)"
        else
            record B7.loss app "Device lost while playing" "-" skipped "skipped" "$(now_iso)"
        fi
    fi
    heading "B8  Close"
    say "If the application's Log window appeared at any point, copy its text into the note below."
    if ((!DRY_RUN)); then
        pause_enter "Close the application window (do not save), then press Enter."
    fi
    if kill -0 "$APP_PID" 2>/dev/null; then
        kill "$APP_PID" 2>/dev/null
        sleep 1
    fi
    wait "$APP_PID" 2>/dev/null
    app_exit=$?
    APP_PID=""
    ask "Anything else worth recording about the application (log window text, errors)?"
    [[ "$ANSWER" == replay ]] && ANSWER=skipped
    record B8.close app "Application closed" "closes cleanly" "$ANSWER" "$NOTE" "$(now_iso)" '[]' \
        "$(jq -n --argjson c "$app_exit" '{exitStatus: $c}')"
fi

# ---------------------------------------------------------------- results

restore_desktop
sinks_json >"$OUT/env/sinks-after.json"
cards_json >"$OUT/env/cards-after.json"
command -v wpctl >/dev/null && wpctl status >"$OUT/env/wpctl-after.txt" 2>&1

jq -s --arg started "$SESSION_STARTED" --arg ended "$(now_iso)" --arg host "$(hostname)" \
    --arg rev "$(git -C "$ROOT" rev-parse HEAD 2>/dev/null)" --argjson dry "$DRY_RUN" \
    --arg kernel "$(uname -sr)" --arg session "${XDG_SESSION_TYPE:-?}" --arg desktop "${XDG_CURRENT_DESKTOP:-?}" \
    --slurpfile fixture "$FIX/h2-fixture.json" '
    {card: "H2 #125", platform: "linux", dryRun: ($dry == 1), host: $host, gitRev: $rev, kernel: $kernel,
     sessionType: $session, desktop: $desktop, started: $started, ended: $ended,
     fixture: $fixture[0],
     summary: (group_by(.answer) | map({key: .[0].answer, value: length}) | from_entries),
     steps: .}' "$STEPS" >"$OUT/results.json"

{
    echo "# H2 listening results ($(hostname), $DATE)"
    echo
    if ((DRY_RUN)); then echo "**Dry run: nothing was played to a listener; every answer is not observed.**"; echo; fi
    echo "- Card: [H2 #125](https://github.com/altqx/hikari/issues/125); automated records: I3 #115, N6 #110 (coverage ledger rows)."
    echo "- Build: \`$(git -C "$ROOT" rev-parse HEAD 2>/dev/null)\` ($BUILD_DIR)"
    echo "- System: $(uname -sr), ${XDG_CURRENT_DESKTOP:-?} on ${XDG_SESSION_TYPE:-?}; $(grep -m1 '^Server Name' "$OUT/env/system.txt" 2>/dev/null | cut -d: -f2- | sed 's/^ //')"
    echo "- Default output at start: \`${ORIGINAL_SINK:-?}\`"
    echo "- Session: $SESSION_STARTED to $(now_iso)"
    echo "- Fixture: peak $(jq -r '.peakDbfs' "$FIX/h2-fixture.json") dBFS; hashes in \`fixture/h2-fixture.json\`"
    echo
    echo "| Step | What | Expected | Heard | Note |"
    echo "| --- | --- | --- | --- | --- |"
    jq -r '.steps[] | "| \(.id) | \(.title) | \(.expected) | **\(.answer)** | \(.note | gsub("\\|"; "/") | gsub("\n"; " ")) |"' "$OUT/results.json"
    echo
    echo "Summary: $(jq -r '.summary | to_entries | map("\(.key) \(.value)") | join(", ")' "$OUT/results.json")."
    echo
    echo "A \"no\" reopens the matching automated card (I3 for audition content, N6 for device handling)."
    echo "A skipped or not-observed step stays open; it is never a pass."
    echo
    echo "Files: \`results.json\` (all answers with timestamps and driver events), \`driver.jsonl\` (every"
    echo "PortAudio driver event), \`app/app.log\` (the application's output), \`env/\` (device lists"
    echo "before, during and after), \`actions.log\` (desktop audio changes made by the script)."
} >"$OUT/results.md"

heading "Done"
say "Results: $OUT/results.md"
say "         $OUT/results.json"
jq -r '.summary' "$OUT/results.json"
