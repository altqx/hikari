#!/usr/bin/env bash
# Records a Linux performance host's inventory (H3 #126; docs/qt/performance.md,
# "Reference class and binding"): CPU, cores/threads, RAM, storage, GPU and
# driver, displays, OS/kernel, compositor and session type, graphics/audio
# backend and devices, power profile/governor, and the pinned software build
# (git revision, compiler, Qt, FFmpeg, FFMS2, libass, PortAudio).
#
#   tools/perf/host-inventory.sh [--build-dir DIR] [--out-dir DIR]
#
# Writes <out-dir>/<hostname>.json and <hostname>.md (default out-dir:
# docs/qt/perf/hosts). Read-only: it changes nothing on the host. Fields it
# cannot read (no tool, needs root) are recorded as null, never guessed.
# A recorded inventory binds a host; it does not calibrate it.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="$ROOT/out/build/ubuntu-x64-release"
OUT_DIR="$ROOT/docs/qt/perf/hosts"
while [[ $# -gt 0 ]]; do
    case "$1" in
    --build-dir) BUILD_DIR="$2"; shift ;;
    --out-dir) OUT_DIR="$2"; shift ;;
    -h | --help) sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done
command -v jq >/dev/null || { echo "jq is required" >&2; exit 1; }
[[ "$(uname -s)" == Linux ]] || { echo "Linux only; record a Windows host by hand" >&2; exit 1; }

have() { command -v "$1" >/dev/null 2>&1; }
# s <cmd...>: the command's trimmed output, or empty.
s() { "$@" 2>/dev/null | sed -e 's/[[:space:]]*$//' || true; }
j() { jq -R -s 'rtrimstr("\n") | if . == "" then null else . end'; } # text -> JSON string or null
num() { [[ "$1" =~ ^[0-9]+([.][0-9]+)?$ ]] && echo "$1" || echo null; }

HOST="$(hostname)"

# --- CPU
lscpu_j='{}'
have lscpu && lscpu_j="$(lscpu -J 2>/dev/null | jq '[.lscpu[] | {(.field | rtrimstr(":")): .data}] | add // {}')"
cpu_model="$(jq -r '."Model name" // empty' <<<"$lscpu_j")"
[[ -n "$cpu_model" ]] || cpu_model="$(grep -m1 '^model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ //')"
sockets="$(jq -r '."Socket(s)" // empty' <<<"$lscpu_j")"
cores_per_socket="$(jq -r '."Core(s) per socket" // empty' <<<"$lscpu_j")"
threads_per_core="$(jq -r '."Thread(s) per core" // empty' <<<"$lscpu_j")"
physical=$(( ${sockets:-1} * ${cores_per_socket:-0} ))
logical="$(nproc --all)"
cpu_json="$(jq -n --arg model "$cpu_model" --argjson physical "$physical" --argjson logical "$logical" \
    --argjson tpc "$(num "$threads_per_core")" --argjson lscpu "$lscpu_j" \
    '{model: $model, physicalCores: $physical, logicalCores: $logical, threadsPerCore: $tpc,
      maxMHz: $lscpu."CPU max MHz", minMHz: $lscpu."CPU min MHz", boost: $lscpu."Frequency boost",
      microcode: $lscpu."Microcode version", l3: $lscpu."L3 cache", virtualization: $lscpu."Virtualization"}')"

# --- memory
mem_kib="$(awk '/^MemTotal:/ {print $2}' /proc/meminfo)"
swap_kib="$(awk '/^SwapTotal:/ {print $2}' /proc/meminfo)"
mem_json="$(jq -n --argjson m "$mem_kib" --argjson s "$swap_kib" \
    '{totalGiB: ($m / 1048576 * 100 | round / 100), swapGiB: ($s / 1048576 * 100 | round / 100),
      speedAndModules: null, note: "module speed and layout need root (dmidecode); not read"}')"

# --- storage: every disk, and the disk under the repository and build tree
disks='[]'
have lsblk && disks="$(lsblk -d -J -o NAME,MODEL,SIZE,ROTA,TRAN,TYPE 2>/dev/null | jq '[.blockdevices[] | select(.type == "disk") | {name, model, size, rotational: .rota, transport: .tran}]')"
backing() { # <path>: filesystem and the physical disk(s) under it
    local src fstype disk
    read -r src fstype < <(df --output=source,fstype "$1" 2>/dev/null | tail -n 1)
    disk="$(lsblk -s -n -l -o NAME,TYPE,MODEL,TRAN "$src" 2>/dev/null | awk '$2 == "disk" {print $1 " " $3 " " $4 " " $5 " " $6}' | sed 's/ *$//' | paste -sd ';')"
    jq -n --arg p "$1" --arg src "$src" --arg fs "$fstype" --arg disk "$disk" \
        '{path: $p, source: $src, filesystem: $fs, disk: (if $disk == "" then null else $disk end)}'
}
storage_json="$(jq -n --argjson disks "$disks" --argjson repo "$(backing "$ROOT")" --argjson build "$(backing "$BUILD_DIR")" \
    '{disks: $disks, repository: $repo, buildTree: $build}')"

# --- GPU and driver
gpus='[]'
if have lspci; then
    gpus="$(lspci -nnk 2>/dev/null | awk '
        /^[0-9a-f]/ { if (dev != "") print dev "\t" drv; dev = ""; drv = "" }
        /^[0-9a-f].*(VGA compatible controller|3D controller|Display controller)/ { dev = $0 }
        /Kernel driver in use:/ && dev != "" { sub(/.*: /, ""); drv = $0 }
        END { if (dev != "") print dev "\t" drv }' |
        jq -R -s '[split("\n")[] | select(length > 0) | split("\t") | {device: .[0], kernelDriver: (.[1] // null)}]')"
fi
nvidia=null
have nvidia-smi && nvidia="$(nvidia-smi --query-gpu=name,driver_version,memory.total,power.limit --format=csv,noheader 2>/dev/null | j)"
gl_renderer=null gl_version=null
if have glxinfo && [[ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]]; then
    gl_renderer="$(glxinfo -B 2>/dev/null | sed -n 's/^OpenGL renderer string: //p' | j)"
    gl_version="$(glxinfo -B 2>/dev/null | sed -n 's/^OpenGL core profile version string: //p' | j)"
fi
vulkan=null
have vulkaninfo && vulkan="$(vulkaninfo --summary 2>/dev/null | sed -n 's/^[[:space:]]*deviceName[[:space:]]*= //p' | paste -sd ';' | j)"
integrated="$(jq '[.[] | .device | test("Radeon Graphics|UHD|Iris|Integrated|Vega|Renoir|Cezanne|Raphael|Phoenix"; "i")] | any' <<<"$gpus")"
gpu_json="$(jq -n --argjson gpus "$gpus" --argjson nvidia "$nvidia" --argjson glr "$gl_renderer" --argjson glv "$gl_version" \
    --argjson vk "$vulkan" --argjson integrated "$integrated" \
    '{devices: $gpus, nvidiaSmi: $nvidia, openglRenderer: $glr, openglVersion: $glv, vulkanDevices: $vk, integratedPresent: $integrated}')"

# --- displays and compositor
displays='[]' compositor=null
if have hyprctl && [[ -n "${HYPRLAND_INSTANCE_SIGNATURE:-}" ]]; then
    displays="$(hyprctl monitors -j 2>/dev/null | jq '[.[] | {name, description: "\(.make) \(.model)", width, height, refreshRate, scale, transform, vrr}]')" # no serial numbers
    compositor="$(hyprctl version -j 2>/dev/null | jq -c '{name: "Hyprland", version: .tag, commit: .commit}')"
elif have wlr-randr && [[ -n "${WAYLAND_DISPLAY:-}" ]]; then
    displays="$(wlr-randr --json 2>/dev/null || echo '[]')"
elif have xrandr && [[ -n "${DISPLAY:-}" ]]; then
    displays="$(xrandr --current 2>/dev/null | grep -E ' connected|\*' | jq -R -s 'split("\n") | map(select(length > 0))')"
fi
[[ "$compositor" != null ]] || compositor="$(jq -n --arg d "${XDG_CURRENT_DESKTOP:-}" '{name: (if $d == "" then null else $d end), version: null}')"

# --- OS
os_json="$(jq -n --arg pretty "$(. /etc/os-release 2>/dev/null; echo "${PRETTY_NAME:-}")" \
    --arg build "$(. /etc/os-release 2>/dev/null; echo "${BUILD_ID:-${VERSION_ID:-}}")" \
    --arg kernel "$(uname -r)" --arg arch "$(uname -m)" '{name: $pretty, build: $build, kernel: $kernel, arch: $arch}')"
session_json="$(jq -n --arg type "${XDG_SESSION_TYPE:-}" --arg desktop "${XDG_CURRENT_DESKTOP:-}" \
    --arg wayland "${WAYLAND_DISPLAY:-}" --arg x11 "${DISPLAY:-}" --argjson compositor "$compositor" \
    '{type: $type, desktop: $desktop, waylandDisplay: $wayland, x11Display: $x11, compositor: $compositor}')"

# --- graphics backend the application gets, and audio
graphics_json="$(jq -n --arg qpa "${QT_QPA_PLATFORM:-}" --arg rhi "${QSG_RHI_BACKEND:-}" --arg wayland "${WAYLAND_DISPLAY:-}" \
    '{qtPlatform: (if $qpa != "" then $qpa elif $wayland != "" then "wayland (default)" else "xcb (default)" end),
      sceneGraphBackend: (if $rhi != "" then $rhi else "opengl (Qt default on Linux)" end),
      environment: {QT_QPA_PLATFORM: $qpa, QSG_RHI_BACKEND: $rhi}}')"
pipewire="$(have pactl && pactl info 2>/dev/null | sed -n 's/^Server Name: //p' | j || echo null)"
default_sink="$(have pactl && pactl get-default-sink 2>/dev/null | j || echo null)"
sinks='[]'
have pactl && sinks="$(pactl -f json list sinks 2>/dev/null | jq '[.[] | {name, description, sampleSpec: .sample_specification, state}]' 2>/dev/null || echo '[]')"
alsa_cards="$(s cat /proc/asound/cards | j)"
audio_json="$(jq -n --argjson server "$pipewire" --argjson def "$default_sink" --argjson sinks "$sinks" --argjson cards "$alsa_cards" \
    '{server: $server, defaultSink: $def, sinks: $sinks, alsaCards: $cards,
      editorOutput: "PortAudio, ALSA host API (the Linux build links libasound only)",
      generalPlayer: "Qt Multimedia (FFmpeg backend; PipeWire or PulseAudio audio sink)"}')"

# --- power
governors="$(cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor 2>/dev/null | sort | uniq -c | awk '{print $2 " x" $1}' | paste -sd ',' | j)"
epp="$(cat /sys/devices/system/cpu/cpu*/cpufreq/energy_performance_preference 2>/dev/null | sort -u | paste -sd ',' | j)"
driver="$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_driver 2>/dev/null | j)"
pstate="$(cat /sys/devices/system/cpu/amd_pstate/status 2>/dev/null | j)"
boost="$(cat /sys/devices/system/cpu/cpufreq/boost 2>/dev/null | j)"
profile="$(have powerprofilesctl && powerprofilesctl get 2>/dev/null | j || echo null)"
supplies="$(ls /sys/class/power_supply 2>/dev/null | paste -sd ',' | j)"
ac=null
for p in /sys/class/power_supply/*; do
    [[ -r "$p/type" && "$(cat "$p/type")" == Mains ]] && ac="$(cat "$p/online" 2>/dev/null | j)"
done
temp="$(have sensors && sensors 2>/dev/null | grep -E '^(Tctl|Package id 0):' | head -n 1 | awk '{print $2}' | j || echo null)"
power_json="$(jq -n --argjson profile "$profile" --argjson gov "$governors" --argjson epp "$epp" --argjson drv "$driver" \
    --argjson pstate "$pstate" --argjson boost "$boost" --argjson supplies "$supplies" --argjson ac "$ac" --argjson temp "$temp" \
    '{powerProfile: $profile, scalingDriver: $drv, governors: $gov, energyPerformancePreference: $epp, amdPstate: $pstate,
      boost: $boost, powerSupplies: $supplies, acOnline: $ac,
      acNote: (if $supplies == null then "no battery or mains supply reported: a desktop on mains power" else null end),
      cpuTemperatureAtInventory: $temp}')"

# --- pinned software build
cache="$BUILD_DIR/CMakeCache.txt"
cachev() { sed -n "s/^$1:[A-Z]*=//p" "$cache" 2>/dev/null | head -n 1; }
# The build tree's own source tree (it may be another worktree than this script's).
SRC_DIR="$(cachev CMAKE_HOME_DIRECTORY)"
[[ -n "$SRC_DIR" ]] || SRC_DIR="$ROOT"
rev="$(git -C "$SRC_DIR" rev-parse HEAD 2>/dev/null)"
dirty="$(git -C "$SRC_DIR" status --porcelain --untracked-files=no 2>/dev/null | wc -l)"
built_at() { [[ -e "$1" ]] && date -r "$1" --iso-8601=seconds || true; }
binaries="$(jq -n --arg app "$(built_at "$BUILD_DIR/src/app/hikarisub")" \
    --arg helper "$(built_at "$BUILD_DIR/src/helpers/media/hikari-media-helper")" \
    --arg core "$(built_at "$BUILD_DIR/tests/perf/hikari_core_perf")" --arg ui "$(built_at "$BUILD_DIR/tests/perf/hikari_ui_perf")" \
    --arg head "$(git -C "$SRC_DIR" log -1 --format=%cI 2>/dev/null)" \
    '{hikarisub: $app, mediaHelper: $helper, coreperf: $core, uiPerf: $ui, sourceHeadCommitted: $head}')"
# Commits the source tree gained after the oldest of those binaries was built:
# the binaries cannot contain them, so the pinned revision is approximate.
oldest="$(for f in src/app/hikarisub src/helpers/media/hikari-media-helper tests/perf/hikari_core_perf tests/perf/hikari_ui_perf; do
    [[ -e "$BUILD_DIR/$f" ]] && date -r "$BUILD_DIR/$f" +%s; done | sort -n | head -n 1)"
after_build='[]'
[[ -n "$oldest" ]] && after_build="$(git -C "$SRC_DIR" log --since="@$oldest" --format='%h %cI %s' 2>/dev/null | jq -R -s 'split("\n") | map(select(length > 0))')"
cxx="$(cachev CMAKE_CXX_COMPILER)"
cxx_version="$(sed -n 's/^set(CMAKE_CXX_COMPILER_VERSION "\(.*\)")/\1/p' "$BUILD_DIR"/CMakeFiles/*/CMakeCXXCompiler.cmake 2>/dev/null | head -n 1)"
gnu_version=""
[[ -n "$cxx" && -x "$cxx" ]] && gnu_version="$("$cxx" -dM -E -x c++ /dev/null 2>/dev/null | sed -n 's/^#define __VERSION__ "\(.*\)"/\1/p')"
qt_dir="$(cachev Qt6_DIR)"
qt_version="$(grep -oE '[0-9]+\.[0-9]+\.[0-9]+' <<<"$qt_dir" | head -n 1)"
qt_ffmpeg=""
if [[ -n "$qt_dir" ]]; then
    qt_lib="$(cd "$qt_dir/../.." 2>/dev/null && pwd)"
    qt_ffmpeg="$(ls "$qt_lib"/libav*.so.*.*.* 2>/dev/null | xargs -r -n1 basename | sort | paste -sd ',')"
fi
vcpkg_status="$BUILD_DIR/vcpkg_installed/vcpkg/status"
ports='{}'
if [[ -r "$vcpkg_status" ]]; then
    ports="$(awk -v RS= '
        /\nFeature:/ { next }
        { pkg = ""; ver = ""; pv = ""; arch = ""
          n = split($0, l, "\n")
          for (i = 1; i <= n; i++) {
              if (l[i] ~ /^Package: /) pkg = substr(l[i], 10)
              if (l[i] ~ /^Version: /) ver = substr(l[i], 10)
              if (l[i] ~ /^Port-Version: /) pv = substr(l[i], 15)
              if (l[i] ~ /^Architecture: /) arch = substr(l[i], 15)
          }
          if (pkg != "" && $0 ~ /Status: install ok installed/) print pkg "\t" ver (pv != "" ? "#" pv : "") "\t" arch }' "$vcpkg_status" |
        jq -R -s '[split("\n")[] | select(length > 0) | split("\t") | {(.[0]): .[1]}] | add // {}')"
fi
fingerprint="$cpu_model|$logical|Linux $(uname -r)|gcc $gnu_version"
build_json="$(jq -n --arg rev "$rev" --argjson dirty "${dirty:-0}" --arg dir "$BUILD_DIR" --arg src "$SRC_DIR" \
    --argjson binaries "$binaries" --argjson afterBuild "$after_build" --arg type "$(cachev CMAKE_BUILD_TYPE)" \
    --arg cxx "$cxx" --arg cxxv "$cxx_version" --arg gnuv "$gnu_version" --arg qt "$qt_version" --arg qtdir "$qt_dir" \
    --arg qtff "$qt_ffmpeg" --argjson ports "$ports" --arg fp "$fingerprint" \
    '{gitRevision: $rev, uncommittedFiles: $dirty, sourceTree: $src, buildTree: $dir, buildType: $type, binariesBuiltAt: $binaries,
      commitsAfterOldestBinary: $afterBuild,
      compiler: {path: $cxx, version: $cxxv, gnuVersion: $gnuv},
      qt: {version: $qt, dir: $qtdir, multimediaFfmpegLibraries: $qtff},
      ffmpeg: $ports.ffmpeg, ffms2: $ports["hikari-ffms2"], libass: $ports.libass, portaudio: $ports.portaudio,
      freetype: $ports.freetype, harfbuzz: $ports.harfbuzz, fribidi: $ports.fribidi, fontconfig: $ports.fontconfig,
      luajit: $ports.luajit, kddockwidgets: $ports.kddockwidgets,
      perfHarnessFingerprint: $fp}')"

# --- against the accepted reference class (four physical cores, 8 GiB, SSD, integrated graphics, 60 Hz)
class_json="$(jq -n --argjson cpu "$cpu_json" --argjson mem "$mem_json" --argjson st "$storage_json" --argjson gpu "$gpu_json" \
    --argjson disp "$displays" '
    {physicalCores: {reference: 4, host: $cpu.physicalCores, matches: ($cpu.physicalCores == 4)},
     ramGiB: {reference: 8, host: $mem.totalGiB, matches: (($mem.totalGiB | floor) <= 8)},
     ssd: {reference: true, host: (($st.repository.disk // "") | test("nvme|SSD"; "i")), matches: (($st.repository.disk // "") | test("nvme|SSD"; "i"))},
     integratedGraphics: {reference: true, host: $gpu.integratedPresent, matches: $gpu.integratedPresent},
     refresh60Hz: {reference: 60, host: ([$disp[]? | .refreshRate? // empty] | map(floor)), matches: ([$disp[]? | .refreshRate? // empty] | length > 0 and all(. >= 59.5 and . <= 60.5))}}')"

mkdir -p "$OUT_DIR"
JSON="$OUT_DIR/$HOST.json"
jq -n --arg host "$HOST" --arg at "$(date --iso-8601=seconds)" \
    --argjson cpu "$cpu_json" --argjson mem "$mem_json" --argjson storage "$storage_json" --argjson gpu "$gpu_json" \
    --argjson displays "$displays" --argjson os "$os_json" --argjson session "$session_json" \
    --argjson graphics "$graphics_json" --argjson audio "$audio_json" --argjson power "$power_json" \
    --argjson build "$build_json" --argjson class "$class_json" \
    '{host: $host, recordedAt: $at, platform: "linux", cpu: $cpu, memory: $mem, storage: $storage, gpu: $gpu,
      displays: $displays, os: $os, session: $session, graphicsBackend: $graphics, audio: $audio, power: $power,
      softwareBuild: $build, referenceClass: $class,
      calibration: {status: "uncalibrated",
                    note: "No measurement tooling calibration, presentation traces or A/V loopback capture are bound to this host; its harness results are observations."}}' >"$JSON"

MD="$OUT_DIR/$HOST.md"
jq -r '
def v: if . == null then "not recorded" elif type == "array" then (if length == 0 then "none" else map(tostring) | join(", ") end) else tostring end;
def yn: if . then "yes" else "**no**" end;
"# Performance host: \(.host) (Linux)",
"",
"Recorded \(.recordedAt) by `tools/perf/host-inventory.sh`. Machine-readable copy: `\(.host).json`.",
"Calibration: **\(.calibration.status)**. \(.calibration.note)",
"",
"## Hardware",
"",
"| Item | Value |",
"| --- | --- |",
"| CPU | \(.cpu.model) |",
"| Cores / threads | \(.cpu.physicalCores) physical / \(.cpu.logicalCores) logical (\(.cpu.threadsPerCore) per core); max \(.cpu.maxMHz) MHz; boost \(.cpu.boost | v); L3 \(.cpu.l3 | v) |",
"| RAM | \(.memory.totalGiB) GiB (swap \(.memory.swapGiB) GiB); \(.memory.note) |",
"| Storage (repository) | \(.storage.repository.disk | v) via \(.storage.repository.source) (\(.storage.repository.filesystem)) |",
"| Storage (build tree) | \(.storage.buildTree.disk | v) via \(.storage.buildTree.source) (\(.storage.buildTree.filesystem)) |",
"| Other disks | \([.storage.disks[] | "\(.name) \(.model // "") \(.size) \(if .rotational then "HDD" else "SSD" end)"] | join("; ")) |",
"| GPU | \([.gpu.devices[] | "\(.device) [driver \(.kernelDriver // "?")]"] | join("; ")) |",
"| GPU driver | \(.gpu.nvidiaSmi // .gpu.openglVersion | v) |",
"| OpenGL | \(.gpu.openglRenderer | v), \(.gpu.openglVersion | v) |",
"| Vulkan devices | \(.gpu.vulkanDevices | v) |",
"| Displays | \([.displays[]? | if type == "object" then "\(.name) \(.description // "") \(.width)x\(.height) @ \(.refreshRate) Hz, scale \(.scale)" else tostring end] | join("; ")) |",
"",
"## Software",
"",
"| Item | Value |",
"| --- | --- |",
"| OS | \(.os.name) (build \(.os.build | v)), kernel \(.os.kernel) \(.os.arch) |",
"| Session | \(.session.type) on \(.session.desktop); compositor \(.session.compositor.name // "?") \(.session.compositor.version // "") |",
"| Qt graphics | platform \(.graphicsBackend.qtPlatform); scene graph \(.graphicsBackend.sceneGraphBackend) |",
"| Audio server | \(.audio.server | v); default sink `\(.audio.defaultSink | v)` |",
"| Audio sinks | \([.audio.sinks[] | "\(.description) (\(.sampleSpec))"] | join("; ")) |",
"| Editor output | \(.audio.editorOutput) |",
"| General player | \(.audio.generalPlayer) |",
"| Power | profile \(.power.powerProfile | v); \(.power.scalingDriver | v) (\(.power.amdPstate | v)); governors \(.power.governors | v); EPP \(.power.energyPerformancePreference | v); boost \(.power.boost | v); \(.power.acNote // "AC online: \(.power.acOnline | v)") |",
"| CPU temperature at inventory | \(.power.cpuTemperatureAtInventory | v) |",
"",
"## Pinned build",
"",
"| Item | Value |",
"| --- | --- |",
"| Git revision | `\(.softwareBuild.gitRevision)` in `\(.softwareBuild.sourceTree)` (\(.softwareBuild.uncommittedFiles) modified tracked files when recorded; HEAD committed \(.softwareBuild.binariesBuiltAt.sourceHeadCommitted)) |",
"| Binaries built | hikarisub \(.softwareBuild.binariesBuiltAt.hikarisub | v); media helper \(.softwareBuild.binariesBuiltAt.mediaHelper | v); hikari_core_perf \(.softwareBuild.binariesBuiltAt.coreperf | v); hikari_ui_perf \(.softwareBuild.binariesBuiltAt.uiPerf | v) |",
"| Commits newer than the oldest binary | \(.softwareBuild.commitsAfterOldestBinary | if length == 0 then "none: the binaries are at or after the revision" else join("; ") end) |",
"| Build tree | `\(.softwareBuild.buildTree)` (\(.softwareBuild.buildType)) |",
"| Compiler | \(.softwareBuild.compiler.path) = GCC \(.softwareBuild.compiler.gnuVersion) |",
"| Qt | \(.softwareBuild.qt.version); Qt Multimedia FFmpeg: \(.softwareBuild.qt.multimediaFfmpegLibraries | v) |",
"| FFmpeg (FFMS2 helper) | \(.softwareBuild.ffmpeg | v) |",
"| FFMS2 | \(.softwareBuild.ffms2 | v) |",
"| libass | \(.softwareBuild.libass | v) (FreeType \(.softwareBuild.freetype | v), HarfBuzz \(.softwareBuild.harfbuzz | v), FriBidi \(.softwareBuild.fribidi | v), Fontconfig \(.softwareBuild.fontconfig | v)) |",
"| PortAudio | \(.softwareBuild.portaudio | v) |",
"| Perf harness fingerprint | `\(.softwareBuild.perfHarnessFingerprint)` |",
"",
"## Against the reference class",
"",
"The accepted class is four physical cores, 8 GiB RAM, SSD, integrated graphics and a 60 Hz display (docs/qt/performance.md).",
"",
"| Item | Class | This host | Matches |",
"| --- | --- | --- | --- |",
(.referenceClass | to_entries[] | "| \(.key) | \(.value.reference) | \(.value.host | v) | \(.value.matches | yn) |")
' "$JSON" >"$MD"

echo "wrote $JSON"
echo "wrote $MD"
