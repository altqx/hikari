# Hosting Aegisub-compatible Lua automation in a QML app

Research for [#20](https://github.com/altqx/hikari/issues/20), completed 2026-09-27. Inspected HikariSub `20d647c4c769ab7f5d383cf3c1c33f03876a94e9`, arch1t3cht/Aegisub `feature` at `9bfd5008d9fc30bd86b3634416781d2c41ce8f89`, and LuaJIT v2.1 at `c6ffc141a8762b41703f9287d63d93622a13dd8f`. Static research only: bundled scripts, QML dialogs, native modules and arm64 builds were not executed.

## Answer

Keep the Lua-facing synchronous API and place a typed request/reply boundary between a worker-owned Lua state and the GUI thread. Parse dialog descriptions into plain data on the worker, instantiate a fixed set of QML Controls on the GUI thread, await the result on the worker, and commit subtitle edits transactionally. The Qt UI must remain responsive while the script waits. This is a recommended host model, not the architecture decision.

“Unchanged Aegisub compatibility” needs a precise contract: the existing HikariSub host already differs from Aegisub in export filters, undo points, validation mutability and dialog button handling. Preserve working scripts, characterize these differences, and explicitly decide which defects to correct. Do not silently promise exact upstream semantics from the current code.

## Current source inventory

Primary implementation references at the inspected HikariSub baseline:

- [Automation.cpp](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Automation.cpp): registration, script/module loading, execution, frame/video/path/GUI helpers.
- [AutomationToFile.cpp](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationToFile.cpp) and [header](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationToFile.h): subtitle userdata, fields/mutation, karaoke/spectrum and undo stub.
- [AutomationDialog.cpp](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationDialog.cpp): description decoding, grid sizing and value readback.
- [AutomationProgress.cpp](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationProgress.cpp): progress/log/dialog registration, queued GUI requests and semaphore waits.
- [AutomationUtils.cpp](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationUtils.cpp), [AutomationScriptReader.cpp](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationScriptReader.cpp): Unicode-compatible loaders, native module registration and MoonScript compilation.

The tables below describe the observed public surface; the linked implementation remains authoritative for coercion/error edge cases. A source inventory is not a passing compatibility suite.

## Script lifecycle and registration

Scripts execute top-level code when loaded; metadata globals are `script_name`, `script_description`, `script_author`, `script_version`. HikariSub exposes `aegisub.lua_automation_version = 4`, removes ordinary `dofile/loadfile` globals and installs its include/module-loading machinery. Its include paths start with the script directory and bundled `Automation/automation/Include`. Aegisub documents Lua 5.1 Automation 4 semantics and limits which helpers are valid during loading. [Aegisub overview](https://aegisub.org/docs/latest/automation/lua/), local Automation.cpp/ScriptReader above.

`register_macro(name, description, run, validate?, is_active?)` retains callable functions; duplicate names within a script are rejected. Run and validation receive `(subtitles, selected_lines, active_line)`; indices refer to the complete subtitle object, including info/styles before dialogue. Run may return selection and active line; validation returns enablement and optional dynamic help, and active callback returns toggle state. Current validation executes directly and passes `can_modify=true`, while upstream validation uses read-only userdata. Upstream registration contract and HikariSub behavior must not be conflated. `register_filter` exists locally but is a no-op `DummyFilter`, not a working export pipeline. [Registration](https://aegisub.org/docs/latest/automation/lua/registration/), local Automation.cpp, [arch1t3cht host](https://github.com/arch1t3cht/Aegisub/blob/9bfd5008d9fc30bd86b3634416781d2c41ce8f89/src/auto4_lua.cpp).

## Subtitle object and records

The userdata has `__index`, `__newindex`, `__len`, `__ipairs` and cleanup hooks. Reading `subs[i]` creates a Lua record: changing that table does not modify the document until assigned back. Public indices start at one. `#subs` and `subs.n` include info, styles and dialogues. Local methods are `delete(...indices)` (also accepts one table), `deleterange(first,last)`, `insert(index,...records)`, `append(...records)`, `lengths()` (three section counts) and `script_resolution()` (width,height). Assigning `nil` to a positive index deletes; assigning a record replaces; index zero appends; a negative index inserts at its absolute position. Local append groups records by class instead of preserving arbitrary interleaving. Out-of-range writes and incompatible replacement classes raise errors. [Local userdata implementation](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationToFile.cpp), [upstream subtitle contract](https://aegisub.org/docs/latest/automation/lua/subtitle_file_interface/).

| Record | Exposed fields in local LineToLua |
|---|---|
| Common | `class`, `section`, `raw` |
| info | `key`, `value` |
| style | `name,fontname,fontsize,color1,color2,color3,color4,bold,italic,underline,strikeout,scale_x,scale_y,spacing,angle,borderstyle,outline,shadow,align,margin_l,margin_r,margin_t,margin_b,encoding,relative_to` |
| dialogue | `comment,layer,start_time,end_time,style,actor,margin_l,margin_r,margin_t,margin_b,effect,text,extra`; conditional HikariSub `text_translation` |

Times are milliseconds; style colors are ASS strings; dialogue comments are represented by a boolean, not a separate record class. Local `margin_t/margin_b` both derive from the ASS vertical margin, `relative_to` is set to 2, and `raw` is generated on read. The local object exposes three structured sections rather than every possible physical line class in upstream's generic file model. Read/write normalization and unknown/extra-field preservation therefore need golden fixtures.

`aegisub.parse_karaoke_data(dialogue)` returns syllable records with `duration,start_time,end_time,tag,text,text_stripped`; `get_frequency_peaks(start,end,freqStart,freqEnd,peak)` is a local extension requiring an audio provider, returns positions and (when peak is zero) intensities. `set_undo_point` is registered but its local inline implementation returns immediately. A macro run is marked as one automation edit by the surrounding host; upstream supports explicit undo behavior. Preserve the intended transaction contract, not a misleading claim that the local no-op is equivalent.

## Global helpers and extensions

All names below are registered in Automation.cpp or AutomationToFile.cpp, except the transient progress/dialog table covered next.

| API | Observed semantics / preservation concern |
|---|---|
| `frame_from_ms(ms)`, `ms_from_frame(frame)` | Delegate to Timebase frame/start-time conversion; nil without loaded video. Preserve VFR boundaries, not an FPS multiplication approximation. |
| `video_size()` | Width,height,aspect ratio,mode; nil without video. Current aspect-mode conditional includes an impossible 2.35–2.36 range test: characterize/correct explicitly. |
| `keyframes()` | Lua array of frame indices from Timebase; nil without video. Array positions are one-based; frame numbers are the provider's frame indices. |
| `get_frame(frame,with_subtitles=false)` | Frame userdata or nil; methods `width,height,getPixel,getPixelFormatted`, GC cleanup. Pixel returns RGB integer or ASS-formatted color, nil outside bounds; runtime buffer is BGRA. |
| `text_extents(style,text)` | Width,height,descent,external leading; requires a style record. Preserve font measurement and units with fixtures. |
| `file_name()` | Current subtitle name or nil when unsaved. |
| `decode_path(path)` | Expands `?audio,?data,?dictionary,?local,?script,?temp,?user,?video`; local data/local/user point into the application's Automation area, while media/script paths use current files. Preserve portability and define user-writable roots during migration. |
| `gettext(text)` | Current wx translation lookup; keep name/UTF-8/fallback behavior through the localisation bridge. |
| `project_properties()` | Table with automation/export/style settings, zoom/aspect/scroll/active/video position and audio/video/timecode/keyframe paths, or nil. Local macro ignores its declared alternate field-name argument, and some values are constants/empty; do not claim perfect upstream parity. |
| `get_audio_selection()` | Current dialogue's start/end when audio box exists; nil otherwise. |
| `set_status_text(text)` | Updates status field; must become a GUI request in a worker host. |
| `gui.get_cursor/set_cursor/get_selection/set_selection/is_modified` | One-based wrapper around editor selection positions plus modified flag. Unicode index units require characterization before mapping to QString/UTF-16. |
| `__init_clipboard` | FFI-backed clipboard get/set used by Lua wrapper; preserve Unicode and GUI-thread dispatch. |
| `cancel()` | Aborts via nil/error-tag path; cancellation should not be logged as ordinary script failure. |

[Upstream miscellaneous API](https://aegisub.org/docs/latest/automation/lua/miscellaneous_apis/) is the comparison contract, not proof that every local extension or defect is upstream behavior.

## Dialogs, progress and cancellation

`aegisub.dialog.display(config, buttons?, button_ids?)` is synchronous to Lua and returns a button result plus a name-keyed value table. The description is a table of controls, with common `class,name,hint,x,y,width,height`; local defaults are x/y=0 and width/height=1. Coordinates are grid cells and spans, not pixels. Classes are case-normalized.

| Class | Data and readback |
|---|---|
| label | `label`; no output value |
| edit / textbox | `text` (local undocumented `value` fallback); string; multiline for textbox |
| intedit | `value,min,max`; integer |
| floatedit | `value,min,max,step`; number (local code reads step but does not pass it to NumCtrl) |
| dropdown | `items,value`; selected string, not index |
| checkbox | `label,value`; boolean |
| color / coloralpha | `value`; hex color string from AssColor with/without alpha |
| alpha | Current implementation is an Edit marked FIXME, not a dedicated alpha control |

Unknown classes and malformed control records error. Local default buttons are OK/Cancel, custom buttons use their supplied labels; cancellation/window close returns false and readback still returns a value table. There is a local discrepancy: LuaDialog supports a third button-ID map but LuaDisplayDialog truncates the arguments to two, so those IDs never reach it through that entry point. This must be a compatibility fixture, not silently copied into a new API. [Dialog code](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationDialog.cpp), [entry point](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationProgress.cpp), [upstream dialogs](https://aegisub.org/docs/latest/automation/lua/dialogs/).

`dialog.open(message,dir,file,wildcard,multiple,must_exist)` returns a path, an array for multiple selection, or nil on cancel; must-exist defaults true. `dialog.save(message,dir,file,wildcard,dont_prompt_overwrite)` returns path/nil; the fifth argument is inverted into the overwrite-prompt flag. Keep distinction between dialog false and file-picker nil.

`progress.set(percent)`, `task(text)`, `title(text)`, `is_cancelled()`, `debug.out([level,]format,...)` and `log` are registered by the progress sink. Local percent updates are queued only when increasing; logging uses formatting and trace-level filtering. `is_cancelled` reports cancellation; the script must respond or the host must reach another checked boundary. `cancel()` explicitly aborts. Do not promise an arbitrary tight loop or native FFI call can always be stopped safely. [Progress source](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationProgress.cpp), [upstream progress](https://aegisub.org/docs/latest/automation/lua/progress_reporting/).

## Threading and QML host proposal

Current HikariSub runs macro processing with a joinable wxThread, queues progress and dialog events to the UI, and waits on semaphores for dialog results. Failure/cancellation invokes undo cleanup. Several helpers still read GUI objects directly and static globals `AutoToFile::laf` / `LuaProgressSink::ps` imply single active-run assumptions. This is a migration hazard, not a thread-safety endorsement.

arch1t3cht's feature branch uses `BackgroundScriptRunner` and a progress sink; dialogs and clipboard operations are dispatched synchronously to the main thread. Its subtitle wrapper distinguishes read-only validation, mutable macro execution, processing completion and cancel paths. This provides a relevant precedent for preserving synchronous Lua semantics across a background executor. [host](https://github.com/arch1t3cht/Aegisub/blob/9bfd5008d9fc30bd86b3634416781d2c41ce8f89/src/auto4_lua.cpp), [progress sink](https://github.com/arch1t3cht/Aegisub/blob/9bfd5008d9fc30bd86b3634416781d2c41ce8f89/src/auto4_lua_progresssink.cpp), [GUI dispatch](https://github.com/arch1t3cht/Aegisub/blob/9bfd5008d9fc30bd86b3634416781d2c41ce8f89/src/auto4_base.cpp), [transaction wrapper](https://github.com/arch1t3cht/Aegisub/blob/9bfd5008d9fc30bd86b3634416781d2c41ce8f89/src/auto4_lua_assfile.cpp).

Proposed implementation:

1. One Lua state has one executor owner; serialize run/validation/load access. Snapshot document/media context so “current tab” cannot change under a run.
2. Execute against a transaction/snapshot and apply one validated result on the GUI thread. Define explicit undo points and cancellation rollback; prevent conflicting document edits while a modal run owns its transaction.
3. Convert config to an immutable DialogRequest with typed controls/button roles; send it by queued signal. A GUI controller owns modal lifetime and focus. QML GridLayout supports row/column/spans; use fixed component factories/Loaders, not script-generated QML code. [GridLayout](https://doc.qt.io/qt-6/qml-qtquick-layouts-gridlayout.html).
4. Worker waits on a request result; GUI never blocks waiting on that same worker. On accept/reject/close/app shutdown, resolve exactly once. UI returns plain typed values; only the worker touches its Lua stack.
5. Throttle progress/log events, propagate cancellation, and service all UI-affine helpers through the dispatcher. Instruction hooks may help pure-Lua cancellation but native calls need cooperative boundaries; process isolation is a separate option with substantial FFI/media/serialization cost.
6. Validation callbacks should remain quick/read-only or execute asynchronously with cached enablement. A synchronous GUI validation can freeze menus; concurrent access to the same Lua state is not a solution.

Use a QML prototype ticket with all control types, sparse grids/spans, long text, keyboard/reader labels, Enter/Esc, sequential dialogs, cancel during work, shutdown while waiting and Unicode paths. HTML can collect appearance feedback only. Figma Starter stays occasional handoff.

## DependencyControl, bundled scripts and arm64

Bundled autoload scripts include Aegisub-Motion, BezierToText, gradient-factory, karaoke templater/leadin, DependencyControl Toolbox, select-overlaps, strip-tags and legacy macros. Modules use `ffi`, regex, Unicode, filesystem, LPeg, luabins and MoonScript, so replacing LuaJIT with plain Lua is not transparent. The local loader preloads `aegisub.__re_impl,__unicode_impl,__lfs_impl,lpeg,luabins`; clipboard uses FFI. [Bundled scripts](https://github.com/altqx/hikari/tree/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/Automation), [module registration](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationUtils.cpp).

Bundled DependencyControl derives a platform key from `ffi.os-ffi.arch`, uses decoded user paths/configuration and references DM.DownloadManager and BM.BadMutex. Current upstream DependencyControl instead documents self-contained pure-FFI replacements and different prerequisites. Do not equate an upstream README with the older bundled revision: test the shipped bundle and make updating it a deliberate change. Native modules need matching ABI/architecture, Unicode paths and writable per-user update roots. No script auto-updates/downloads were executed. [Bundled implementation](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/Automation/automation/Include/l0/DependencyControl.moon), [upstream](https://github.com/TypesettingTools/DependencyControl).

LuaJIT now explicitly lists Windows ARM64 and its v2.1 MSVC batch file has ARM64 target/host-tool logic. It is incorrect to repeat older blanket claims of no Windows arm64 support. LuaJIT recommends the maintained v2.1 production branch and uses rolling releases with timestamp-based version numbers, not an official recent tarball release. Pin an exact commit and test native modules/FFI/cancellation on arm64; platform support does not prove the entire automation bundle works there. [Status](https://luajit.org/status.html), [installation](https://luajit.org/install.html), [pinned MSVC build](https://github.com/LuaJIT/LuaJIT/blob/c6ffc141a8762b41703f9287d63d93622a13dd8f/src/msvcbuild.bat).

## Follow-on decisions and acceptance

Choose a QML dialog renderer and worker/request/transaction design after the prototype. Build a compatibility corpus from bundled scripts and current host behavior: exact records/indexing, mutations/selection/undo, VFR conversion, frame colors, multilingual paths, dialog return types, cancellation/errors and DependencyControl module loading. Distinguish intended upstream fixes from preserved HikariSub extensions. Research establishes the surface and migration risks; full unchanged-script compatibility remains a tested implementation deliverable.
