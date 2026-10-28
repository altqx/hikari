#!/usr/bin/env bash
# Runs the implemented performance-harness workloads (H3 #126) on this host and
# stores raw results with the CPU contention seen during each run.
#
#   tools/perf/run-observations.sh [--build-dir DIR] [--out DIR] [--label NAME]
#                                  [--core-bin PATH] [--ui-bin PATH] [--diagnostics]
#
# Workloads: hikari_core_perf (perf.core) and hikari_ui_perf (perf.ui). Each
# runs five repetitions of at least 200 timed operations after ten seconds of
# warmup, reporting nearest-rank p95/p99/max per run (tests/support/perf).
# --diagnostics adds the single-shot timing observations printed by backend
# tests (helper startup, frame transfer, simulated-device audition tail);
# they are not harness workloads.
#
# A sampler records the load average, CPU pressure and the busiest other
# processes every two seconds. tools/perf/summarize-observations.py marks a
# run contaminated when other work competed for the CPU, and compares each
# workload with docs/qt/performance.md. Every result is an uncalibrated
# observation unless the host is calibrated (tests/perf/reference-hosts.json).
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="$ROOT/out/build/ubuntu-x64-release"
OUT=""
LABEL="run-$(date +%H%M%S)"
CORE_BIN="" UI_BIN=""
DIAGNOSTICS=0
while [[ $# -gt 0 ]]; do
    case "$1" in
    --build-dir) BUILD_DIR="$2"; shift ;;
    --out) OUT="$2"; shift ;;
    --label) LABEL="$2"; shift ;;
    --core-bin) CORE_BIN="$2"; shift ;;
    --ui-bin) UI_BIN="$2"; shift ;;
    --diagnostics) DIAGNOSTICS=1 ;;
    -h | --help) sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done
CORE_BIN="${CORE_BIN:-$BUILD_DIR/tests/perf/hikari_core_perf}"
UI_BIN="${UI_BIN:-$BUILD_DIR/tests/perf/hikari_ui_perf}"
OUT="${OUT:-$ROOT/docs/qt/perf/observations/$(date +%F)/$LABEL}"
REFERENCE_HOSTS="$ROOT/tests/perf/reference-hosts.json"
for b in "$CORE_BIN" "$UI_BIN"; do [[ -x "$b" ]] || { echo "missing $b" >&2; exit 1; }; done
command -v jq >/dev/null || { echo "jq is required" >&2; exit 1; }
mkdir -p "$OUT/raw"

snapshot() { # one JSON line: time, load, CPU pressure, temperature, the busiest processes
    local la psi temp top
    read -r la < /proc/loadavg
    psi="$(sed -n 's/^some avg10=\([0-9.]*\).*/\1/p' /proc/pressure/cpu 2>/dev/null)"
    temp="$(sensors 2>/dev/null | grep -E '^(Tctl|Package id 0):' | head -n 1 | awk '{print $2}' | tr -d '+°C')"
    # top's second iteration measures the last second, not the process lifetime.
    top="$(top -b -n 2 -d 1 -w 512 -o %CPU 2>/dev/null | awk '/^top -/{n++} n == 2 && $1 ~ /^[0-9]+$/ && $9+0 >= 5 {print $1 "\t" $9 "\t" $12}' | head -n 8 |
        jq -R -s '[split("\n")[] | select(length > 0) | split("\t") | {pid: (.[0] | tonumber), cpu: (.[1] | tonumber), command: .[2]}]')"
    jq -n -c --arg at "$(date --iso-8601=ns)" --arg la "$la" --arg psi "${psi:-}" --arg temp "${temp:-}" --argjson top "${top:-[]}" \
        '{at: $at, load1: ($la | split(" ")[0] | tonumber), load5: ($la | split(" ")[1] | tonumber), load15: ($la | split(" ")[2] | tonumber),
          cpuPressureSome10: (if $psi == "" then null else ($psi | tonumber) end), cpuTempC: (if $temp == "" then null else ($temp | tonumber) end),
          busiest: $top}'
}

sampler() { # <file>
    while true; do snapshot >>"$1"; sleep 1; done
}

run_workload() { # <name> <binary> [env...]
    local name="$1" bin="$2"
    shift 2
    local samples="$OUT/raw/$name-samples.jsonl" pid spid started ended code
    : >"$samples"
    echo "== $name ($(date +%T)), load $(cut -d' ' -f1-3 /proc/loadavg)"
    snapshot >"$OUT/raw/$name-before.json"
    sampler "$samples" &
    spid=$!
    started="$(date --iso-8601=ns)"
    env "$@" "$bin" "$OUT/raw/$name-report.json" "$REFERENCE_HOSTS" >"$OUT/raw/$name.stdout" 2>"$OUT/raw/$name.stderr" &
    pid=$!
    echo "$pid" >"$OUT/raw/$name.pid"
    wait "$pid"
    code=$?
    ended="$(date --iso-8601=ns)"
    kill "$spid" 2>/dev/null
    wait "$spid" 2>/dev/null
    snapshot >"$OUT/raw/$name-after.json"
    jq -n --arg name "$name" --arg bin "$bin" --arg sha "$(sha256sum "$bin" | cut -d' ' -f1)" \
        --arg built "$(date -r "$bin" --iso-8601=seconds)" --arg started "$started" --arg ended "$ended" \
        --argjson code "$code" --argjson pid "$pid" \
        '{name: $name, binary: $bin, sha256: $sha, builtAt: $built, started: $started, ended: $ended, exitCode: $code, pid: $pid}' \
        >"$OUT/raw/$name-run.json"
    cat "$OUT/raw/$name.stdout"
}

{
    echo "label: $LABEL"
    echo "started: $(date --iso-8601=seconds)"
    echo "host: $(hostname)"
    echo "kernel: $(uname -r)"
    echo "governors: $(cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor 2>/dev/null | sort | uniq -c | tr -s ' ' | paste -sd ',')"
    echo "power profile: $(powerprofilesctl get 2>/dev/null || echo unknown)"
    echo "build dir: $BUILD_DIR"
} >"$OUT/environment.txt"

run_workload perf.core "$CORE_BIN"
run_workload perf.ui "$UI_BIN" QT_QPA_PLATFORM=offscreen

if ((DIAGNOSTICS)); then
    # Single-shot timings printed by backend tests on the build's own fixtures.
    # The PortAudio test stays skipped: it plays a loud ramp unless pointed at
    # a null device, and HIKARI_TEST_AUDIO_DEVICE is cleared here.
    diag() { # <name> <binary> <gtest filter>
        local name="$1" bin="$2" filter="$3"
        [[ -x "$bin" ]] || { echo "skip $name: $bin missing"; return; }
        echo "== diagnostic $name"
        snapshot >"$OUT/raw/diag-$name-before.json"
        env -u HIKARI_TEST_AUDIO_DEVICE QT_QPA_PLATFORM=offscreen "$bin" --gtest_filter="$filter" \
            >"$OUT/raw/diag-$name.stdout" 2>"$OUT/raw/diag-$name.stderr"
        echo "exit $?" >>"$OUT/raw/diag-$name.stdout"
        snapshot >"$OUT/raw/diag-$name-after.json"
        grep -E ' ms|MiB' "$OUT/raw/diag-$name.stderr" || true
    }
    for i in 1 2 3 4 5; do
        diag "indexed-source-$i" "$BUILD_DIR/tests/backends/hikari_backends_indexed_source_tests" \
            'Fixture.FrameTransferObservation:Fixture.HelperLossClosesTheGenerationUntilAnExplicitRestart'
        diag "line-audition-$i" "$BUILD_DIR/tests/backends/hikari_backends_line_audition_tests" \
            'Audition.StopTailAndDrainAreSeparateMoments'
    done
fi

echo "ended: $(date --iso-8601=seconds)" >>"$OUT/environment.txt"
echo "raw results in $OUT"
