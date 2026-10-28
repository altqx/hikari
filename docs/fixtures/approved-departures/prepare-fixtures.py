"""Prepare small original specification inputs; does not execute an application."""
from pathlib import Path
import hashlib
import json

ROOT = Path(__file__).resolve().parent
INPUTS = ROOT / "inputs"
INPUTS.mkdir(exist_ok=True)
files = []

def put(name, content, encoding="utf-8", newline="LF"):
    data = content.encode(encoding) if isinstance(content, str) else content
    path = INPUTS / name
    path.write_bytes(data)
    files.append({"path": "inputs/" + name, "bytes": len(data),
                  "sha256": hashlib.sha256(data).hexdigest(),
                  "encoding": encoding, "newlines": newline,
                  "provenance": "Original synthetic specimen authored for this specification; no user files or external assets."})

def js(name, obj):
    put(name, json.dumps(obj, ensure_ascii=False, indent=2) + "\n")

style_header = "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
def style(name):
    return f"Style: {name},Arial,24,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,0,2,10,10,10,1\n"
events = "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
base = "[Script Info]\nTitle: PROTOTYPE compatibility specimen\nScriptType: v4.00+\nPlayResX: 1280\nPlayResY: 720\nYCbCr Matrix: TV.601\n"

opaque = (base + style_header + style("Default") + events
          + "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Gate\n"
          + "\n[Hikari Synthetic Opaque]\n; Preserve order and exact whitespace\nToken:  first:second  \nraw-without-colon\n"
          + "\n[Fonts]\nfontname: PROTOTYPE-NOT-A-VALID-FONT.ttf\nOpaque!!payload,not-a-font\n"
          + "\n[Hikari Synthetic Opaque]\nToken: repeated-section\n"
          + events + "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,港 — ميناء\n")
put("unknown-sections.ass", b"\xef\xbb\xbf" + opaque.replace("\n", "\r\n").encode("utf-8"), "UTF-8 BOM", "CRLF")
put("conversion-source.ass", base + style_header + style("Default") + style("Signs") + events
    + "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\i1}Gate{\\i0}\n"
    + "Dialogue: 0,0:00:03.00,0:00:04.00,Signs,,0,0,0,,{\\pos(640,90)}HARBOR\n"
    + "Comment: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,Original synthetic note\n"
    + "[Hikari Synthetic Opaque]\nKey: retained in source\n")

for suffix in ("A", "B"):
    put(f"PROTOTYPE_{suffix}.ass", base + style_header + style("Default") + events
        + f"Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Document {suffix}\n")
def session(second_audio):
    rows = ["[HikariSub]", "[Close session]"]
    for index, suffix in enumerate(("A", "B")):
        rows += [f"Tab: {index}", "Video: ", "Position: 0", "FFMS2: 1",
                 f"Subtitles: PROTOTYPE_{suffix}.ass", "Active: 0", "Scroll: 0", "Editor: 1"]
        if index == 0:
            rows.append("Audio: PROTOTYPE_A.wav")
        elif second_audio is not None:
            rows.append("Audio: " + second_audio)
    return "\r\n".join(rows) + "\r\n"
put("audio-omitted.kls", session(None), newline="CRLF")
put("audio-explicit.kls", session("PROTOTYPE_B.wav"), newline="CRLF")
put("rate-A.sub", "{24}{48}Rate belongs to Document A\n")
put("rate-B.sub", "{24}{48}Rate belongs to Document B\n")
js("equality.json", {"unit": "microseconds", "integer_encoding": "Decimal strings for exact fixture interchange; not a production IPC decision.", "legacy_representable_cases": [
    {"lhs": str(value), "rhs": str(value), "expected": {"ge": True, "le": True, "eq": True, "gt": False, "lt": False}}
    for value in [0, 1000, 1123000]], "new_typed_range_cases": [
    {"lhs": str(value), "rhs": str(value), "expected": {"ge": True, "le": True, "eq": True, "gt": False, "lt": False}}
    for value in [-(2**63), -1, 2**63-1]],
    "note": "Typed-range cases do not imply the legacy millisecond type can represent these values."})
js("fps-isolation.json", {"raw_start_frame": 24, "raw_end_frame": 48,
    "steps": [
        {"document": "A", "rate": {"numerator": 24, "denominator": 1}, "expected_exact_seconds": {"start": [1, 1], "end": [2, 1]}},
        {"document": "B", "rate": {"numerator": 24000, "denominator": 1001}, "expected_exact_seconds": {"start": [1001, 1000], "end": [1001, 500]}},
        {"document": "B", "rate": None, "expected": "B raw frames remain 24/48; B time unresolved; A remains 1/2 seconds."},
        {"document": "B", "rate": {"numerator": 25, "denominator": 1}, "expected_exact_seconds": {"start": [24, 25], "end": [48, 25]}, "expected_A": "unchanged"}],
    "constraints": "Rate changes are explicit fixture operations, never automatic effects of attaching media. Frame range/end-inclusive export details are outside this case."})
js("audio-frame-offset.json", {"unit": "microseconds", "lookup": "frameAtOrAfter",
    "cases": [
        {"starts": [0, 40000, 81000, 120000], "anchor": 40000, "target": 81000, "expected_offset": 1, "source_predicted_old_offset": 2},
        {"starts": [0, 40000, 81000, 120000], "anchor": 81000, "target": 40000, "expected_offset": -1},
        {"starts": [1000000, 1040000, 1081000, 1120000], "anchor": 1040000, "target": 1081000, "expected_offset": 1},
        {"starts": [0, 40000, 80000, 120000], "anchor": 40000, "target": 80000, "expected_offset": 1},
        {"starts": [0, 40000, 81000, 120000], "anchor": 41000, "target": 81000, "expected_offset": 0}],
    "note": "Only index offset is specified. Legacy endpoint representatives and serialization are separate accepted contracts; no exact seek/device claim."})

tl = base + "TLMode: Yes\nTLMode Style: TLMode Original\n" + style_header + style("Default") + style("TLMode Original") + events
tl += "Comment: 0,0:00:01.00,0:00:02.00,TLMode Original,,0,0,0,,Gate\n"
tl += "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,\n"
tl += "Comment: 0,0:00:03.00,0:00:04.00,TLMode Original,,0,0,0,,Harbor\n"
tl += "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,港\n"
put("tlmode-pairs.ass", tl)
put("precision.srt", "1\n00:00:01,009 --> 00:00:02,009\nOriginal synthetic precision sample\n")
js("format-boundaries.json", {"input": "precision.srt", "authored_microseconds": [1009000, 2009000],
    "expected_export_microseconds": {"ASS": [1000000, 2000000], "SRT": [1009000, 2009000], "MPL2": [1100000, 2100000], "TMPlayer_start_only": [1000000]},
    "constraints": "Comparison is the accepted nonnegative precision contract, not full converter output. Source authored times stay unchanged; loss review remains required. MicroDVD mapping needs its own explicit rational rate."})

def case(identifier, inputs, steps, comparison, expectations, source):
    return {"id": identifier, "inputs": inputs, "steps": steps, "comparison": comparison,
            "accepted_expectations": expectations, "source_evidence": source,
            "legacy_runtime": {"status": "not-executed", "observations": [], "executable_sha256": None, "environment": None},
            "rewrite_runtime": {"status": "not-implemented", "observations": [], "executable_sha256": None, "environment": None}}

source_root = "https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/"
cases = [
    case("C03-preservation", ["unknown-sections.ass"], ["Load a scratch copy and save unchanged.", "Independently load a scratch copy; change first Line text Gate to Gate edited; save to another path."], "byte-exact unchanged save where contracted; ordered opaque-span byte comparison and semantic changed-Line comparison", ["Unknown/repeated sections, whitespace and opaque payload remain ordered and intact.", "Only the changed Line span may regenerate; otherwise required normalization is reported before writing.", "Opaque font-like bytes are not asserted to decode or render."], [source_root + "SubsLoader.cpp#L93"]),
    case("C02-loss-preview", ["conversion-source.ass"], ["Prepare ASS to SRT conversion for Original role at captured revision/options.", "Observe review, then cancel; prepare again and accept the listed losses.", "Independently change the source revision or options after acceptance and try to publish."], "semantic plan/application state; unchanged source bytes; no exact converted golden yet", ["Loss review precedes output and requires explicit acceptance.", "Cancellation creates no converted result; source remains.", "Changed source/options invalidate approval.", "Underlying cleanup/parser corrections and final output destination are not selected here."], [source_root + "SubsGridBase.cpp#L199"]),
    case("C05-audio-association", ["audio-omitted.kls", "audio-explicit.kls", "PROTOTYPE_A.ass", "PROTOTYPE_B.ass"], ["Bind placeholders to scratch Documents and controlled audio resources; retain template and derived hashes.", "Restore omitted case, then explicit case in independent disposable profiles.", "Repeat with A audio unavailable."], "per-Document association identity/provenance, diagnostics and original hashes", ["Omitted B Audio resolves from B own context or none, never A.", "Explicit B association is B own resource, even when missing.", "Missing A media does not alter B context."], [source_root + "Notebook.cpp#L1366"]),
    case("C01-equality", ["equality.json"], ["Compare each equal pair through the respective runtime public time API."], "exact boolean; old type representability recorded per case", ["Both >= and <= return true for equal values.", "New-range-only inputs are not claimed executable in the legacy type."], [source_root + "SubsTime.cpp#L184"]),
    case("C01-fps-isolation", ["rate-A.sub", "rate-B.sub", "fps-isolation.json"], ["Open two Documents and apply explicit rate operations in descriptor order.", "Inspect raw frames and independent rational values after each operation."], "exact frame integers and rational quantities; presence/unknown provenance", ["B rate never reinterprets A.", "Unknown B retains raw frames and has no guessed rate.", "Explicit rates preserve their rational identity."], [source_root + "SubsTime.cpp"]),
    case("T42-A", ["audio-frame-offset.json"], ["For each descriptor use one explicit timebase to map anchor and target independently; subtract indices."], "exact signed index offset; not device timing", ["Approved VFR example yields 1, reverse yields -1; nonzero origin does not change the difference.", "Containing lookup must not be substituted for the declared at-or-after endpoint lookup."], [source_root + "SubsGrid.cpp", source_root + "Timebase.cpp"]),
    case("supplement-TLMode", ["tlmode-pairs.ass"], ["Observe parser pairing and same-format save on a scratch copy; record source and resulting role states."], "raw preservation plus characterized role presence; no invented legacy pass", ["The accepted model distinguishes absent and empty translation.", "This specimen contains one empty and one nonempty paired translation; exact legacy normalization must be observed.", "Marker collisions, malformed pairs and mixed Actor/form-feed flags require additional cases."], [source_root + "SubsLoader.cpp#L120"]),
    case("supplement-format-precision", ["precision.srt", "format-boundaries.json"], ["Load and prepare the named conversions without editing authored times."], "accepted per-format nonnegative precision values; semantic report", ["Report endpoint quantization; source values stay unchanged.", "This does not specify complete serializer bytes, negative export or converter cleanup."], [source_root + "SubsTime.cpp#L83"])
]
manifest = {"schema": 1, "kind": "specification-fixtures-not-a-test-suite", "license_reference": "../../../../LICENSE (repository GPL version 3 text); no external assets", "prepared_date": "2026-09-27",
            "source_baseline": "20d647c4c769ab7f5d383cf3c1c33f03876a94e9", "accepted_spec_baseline": "44dbf0a3ee9215378bfd39e44eefbcf7c507be46",
            "generation": "python prepare-fixtures.py; deterministic, no network, no application execution",
            "inputs": files, "cases": cases,
            "scope_limit": "Eight specification cases do not exhaust six approved outcomes or platform/corpus parity. No legacy/rewrite observation exists yet."}
(ROOT / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
print(f"Prepared {len(files)} inputs and {len(cases)} unexecuted specification cases.")
