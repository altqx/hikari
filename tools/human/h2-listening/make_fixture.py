#!/usr/bin/env python3
"""Quiet H2 listening fixture (#125): distinct markers at about -24 dBFS peak.

Writes into <out dir>:
  h2-48k.wav, h2-44k1.wav      the soundtrack at 48 kHz and 44.1 kHz (stereo)
  h2-48k.mkv, h2-44k1.mkv      the same audio (PCM) under a counting test video
  h2-script.ass                three Lines timed to the markers, naming the video
  h2-fixture.json              the timeline, level and hashes

Timeline (20 s): 0.0-0.6 s start marker (three short 1 kHz beeps). For each
Line [S, E): [S-1000, S) a steady low hum (300 Hz), [S, E) a rising whoop
(500 -> 1500 Hz sweep), [E, E+1000) a run of eight fast ticks (2 kHz blips).
Silence elsewhere. Every sound has 10 ms fades, so a click is never part of
the fixture. Uses only the Python standard library plus the ffmpeg CLI.
"""

import argparse
import hashlib
import json
import math
import os
import shutil
import struct
import subprocess
import sys
import wave

PEAK_DBFS = -24.0
LINES = [(3000, 5000), (9000, 11000), (15000, 17000)]
DURATION_MS = 20000
HUM_HZ, SWEEP_FROM_HZ, SWEEP_TO_HZ, TICK_HZ, BEEP_HZ = 300.0, 500.0, 1500.0, 2000.0, 1000.0


def segments():
    """(start ms, end ms, kind, params) for every sound in the fixture."""
    out = [(i * 200, i * 200 + 100, "beep", {"hz": BEEP_HZ}) for i in range(3)]
    for s, e in LINES:
        out.append((s - 1000, s, "hum", {"hz": HUM_HZ}))
        out.append((s, e, "sweep", {"from": SWEEP_FROM_HZ, "to": SWEEP_TO_HZ}))
        out.extend((e + k * 125, e + k * 125 + 8, "tick", {"hz": TICK_HZ}) for k in range(8))
    return out


def render(rate):
    amp = 10 ** (PEAK_DBFS / 20.0)
    n = DURATION_MS * rate // 1000
    buf = [0.0] * n
    for s_ms, e_ms, kind, p in segments():
        a, b = s_ms * rate // 1000, e_ms * rate // 1000
        length = b - a
        fade = min(int(0.010 * rate), length // 2)
        if kind == "tick":
            fade = length // 2  # a Hann-like blip
        phase = 0.0
        for i in range(length):
            if kind == "sweep":
                f = p["from"] + (p["to"] - p["from"]) * (i / length)
            else:
                f = p["hz"]
            phase += 2 * math.pi * f / rate
            env = 1.0
            if i < fade:
                env = 0.5 - 0.5 * math.cos(math.pi * i / fade)
            elif i >= length - fade:
                env = 0.5 - 0.5 * math.cos(math.pi * (length - i) / fade)
            buf[a + i] = amp * env * math.sin(phase)
    return buf


def write_wav(path, rate, samples):
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(rate)
        frames = bytearray()
        for v in samples:
            q = max(-32768, min(32767, int(round(v * 32767))))
            frames += struct.pack("<hh", q, q)
        w.writeframes(bytes(frames))


def peak_dbfs(samples):
    peak = max(abs(v) for v in samples)
    return 20 * math.log10(peak) if peak > 0 else -math.inf


def ass_time(ms):
    cs = ms // 10
    return f"{cs // 360000}:{cs // 6000 % 60:02d}:{cs // 100 % 60:02d}.{cs % 100:02d}"


def write_script(path, video_name):
    events = []
    for n, (s, e) in enumerate(LINES, 1):
        events.append(f"Dialogue: 0,{ass_time(s)},{ass_time(e)},Default,,0,0,0,,"
                      f"Line {n}: rising whoop (hum before, ticks after)")
    text = "\n".join([
        "[Script Info]",
        "; H2 listening fixture (tools/human/h2-listening)",
        "ScriptType: v4.00+",
        "PlayResX: 640",
        "PlayResY: 360",
        f"Video File: {video_name}",
        f"Audio File: {video_name}",
        "",
        "[V4+ Styles]",
        "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
        "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
        "MarginR, MarginV, Encoding",
        "Style: Default,Arial,28,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,0,2,20,20,20,1",
        "",
        "[Events]",
        "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text",
        *events,
        "",
    ])
    with open(path, "w", encoding="utf-8-sig", newline="\r\n") as f:
        f.write(text)


def mux(wav, mkv):
    subprocess.run([
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
        "-f", "lavfi", "-i", f"testsrc=size=640x360:rate=24000/1001:duration={DURATION_MS / 1000}",
        "-i", wav, "-map", "0:v", "-map", "1:a",
        "-c:v", "mpeg4", "-q:v", "4", "-g", "48", "-c:a", "pcm_s16le", "-shortest", mkv,
    ], check=True)


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("out_dir")
    ap.add_argument("--no-video", action="store_true", help="WAV files only (no ffmpeg)")
    args = ap.parse_args()
    os.makedirs(args.out_dir, exist_ok=True)
    files = {}
    for rate, tag in ((48000, "48k"), (44100, "44k1")):
        samples = render(rate)
        peak = peak_dbfs(samples)
        if peak > -20.0:
            sys.exit(f"refusing: the {tag} fixture peaks at {peak:.1f} dBFS (limit -20)")
        wav = os.path.join(args.out_dir, f"h2-{tag}.wav")
        write_wav(wav, rate, samples)
        files[f"h2-{tag}.wav"] = {"rate": rate, "peakDbfs": round(peak, 2)}
        if not args.no_video:
            if not shutil.which("ffmpeg"):
                sys.exit("ffmpeg is required to make the video fixtures (or pass --no-video)")
            mkv = os.path.join(args.out_dir, f"h2-{tag}.mkv")
            mux(wav, mkv)
            files[f"h2-{tag}.mkv"] = {"rate": rate, "peakDbfs": round(peak, 2)}
    write_script(os.path.join(args.out_dir, "h2-script.ass"), "h2-48k.mkv")
    files["h2-script.ass"] = {}
    for name in files:
        files[name]["sha256"] = sha256(os.path.join(args.out_dir, name))
    meta = {
        "peakDbfs": PEAK_DBFS,
        "durationMs": DURATION_MS,
        "lines": [{"n": n, "startMs": s, "endMs": e} for n, (s, e) in enumerate(LINES, 1)],
        "segments": [{"startMs": s, "endMs": e, "kind": k, **p} for s, e, k, p in segments()],
        "files": files,
    }
    with open(os.path.join(args.out_dir, "h2-fixture.json"), "w") as f:
        json.dump(meta, f, indent=1)
    print(json.dumps({name: v.get("peakDbfs") for name, v in files.items() if "peakDbfs" in v}))


if __name__ == "__main__":
    main()
