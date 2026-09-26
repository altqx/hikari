# Automation manager, script dialogs and cancellation study

Ticket: [Prototype automation manager, script dialogs and cancellation](https://github.com/altqx/hikari/issues/51).

Throwaway, memory-only HTML. Open [automation-host.html](automation-host.html), or from the repository run:

```powershell
python -m http.server 8765 --bind 127.0.0.1 --directory docs/prototypes
```

Then visit:

- [Manager task window](http://127.0.0.1:8765/automation-host.html?variant=window): a two-column manager dialog; closing it leaves the workspace and independent task strip.
- [Persistent manager tool](http://127.0.0.1:8765/automation-host.html?variant=panel): script discovery, target and macro actions alongside video/audio/editor; the subtitle grid remains full width below.

The bottom bar switches arrangements and updates the URL without resetting state. Left/right keys switch outside editable controls and dialogs. The task-window header also offers the alternate arrangement, because a modal dialog makes the bottom bar inert. Narrow windows stack the manager below the grid.

## What is fixed, and what is being reviewed

Keep the accepted Compact Studio / Classic shell, video left, audio above editor right, full-width grid, shared workspace, optional Home, follow-editing with optional pin, and protected comparison. Separate-process Lua is accepted. Script-created options remain task dialogs in both arrangements.

**Proposed only:** manager placement, one active macro application-wide, captured-target reservation, atomic staged document apply, one sample undo, helper lifetime, explicit restart/reload flow and escalation interaction. C06–C08 compatibility departures remain named decisions; showing them does not approve them. No timeout duration or automatic force-stop deadline is encoded. The sample pauses at an explicit apply boundary solely to expose state; it does not propose a mandatory human confirmation for every actual macro.

## Walkthroughs

The five buttons under “Walkthroughs” reset only sample memory.

1. **Typed options → result:** change prefix, dropdown and checkbox. “Apply” returns a string plus a typed values table; advance work twice, then apply the staged result. Check the captured Line and use Undo. “Preview” returns its string and the synthetic macro stops without changing the document.
2. **Return distinctions:** select the default button schema in the manager and rerun. Default OK is shown as the source-observed empty string; default Cancel and window-close/Escape return false plus values. Under the custom schema, the button labeled Cancel returns the string “Cancel”; the sample script chooses to stop. These are not interchangeable host events.
3. **File choices:** select Sample choices.lua → Choose sample file. Choose one path, a multiple-path array or save destination; Cancel returns Lua nil (displayed as JSON null). No OS file picker or file operation occurs.
4. **Cooperative cancellation:** Cancel latches the run; choose cancellation acknowledgment. The proposed staged edit is discarded and helper remains available. During an options dialog, “Cancel run” resolves its pending request through false/values, then enters the waiting-for-acknowledgment state.
5. **Stuck call / crash:** simulate still waiting, explicitly Force stop, then Restart helper + reload scripts. Helper generation changes; registrations reload; simulated globals reset; the interrupted macro never reruns automatically. A crash has a distinct terminal result. Late-reply simulation rejects a reply to the terminal run.
6. **Target safety:** start a slow marker, switch editing documents or pin the tool elsewhere, and finish. The run retains its captured Document, revision and Line. A protected-reference pin disables Run with a reason. Changing reference visibility does not remove protection.
7. **Discovery:** load the extra synthetic script; inspect the broken registration; simulate editing its source and reload. Reload all resets synthetic script counters. Rerun-last captures the current tool target anew; source editing and shortcut assignment only store sample strings.

The task strip exposes progress, target, generation, outcome and logs. The state disclosure includes document content/revisions, selection, draft, one undo, registrations, simulated globals, request/run IDs, staged edit, cancellation latch, typed replies and audit trail.

## Source and parity checklist

The [accepted process boundary and proposed host contract](https://github.com/altqx/hikari/blob/qt/docs/qt/automation.md) and [pinned Lua API research](https://github.com/altqx/hikari/blob/a99a2470e958437290de73dd2451ee57cc763336/docs/research/lua-automation-host.md) govern the study. The latter links the audited source baseline; this prototype does not execute that host.

| Capability | Sample coverage / limit |
| --- | --- |
| Discovery, registration, validation, active state, load/reload, errors, editing, rerun and shortcuts | Synthetic registrations, a disabled macro and active indicator; no Lua callbacks, external editor or actual key dispatch. Callback order/count and mutable validation stay unresolved. |
| Target and selection | Captured Document/revision/Line, protected-reference refusal and target reservation proposal. The real Lua subtitle sequence includes info/style records, one-based full-object indices and assign-back semantics; this sample Line grid is not that API. |
| Dialog controls | All named classes appear: label, edit, textbox, intedit, floatedit, dropdown, checkbox, color, coloralpha and alpha. Grid spans are sampled; not an arbitrary schema renderer. Values are typed; color normalization/coercions, malformed schemas, focus/IME and accessibility require native fixtures. |
| Dialog returns | Custom label strings, false plus values, default empty string and file-picker nil/path/array remain distinct. Button-ID third argument is not silently repaired. |
| Progress / cancellation / errors | Manual increasing progress and log events; cooperative acknowledgment, stuck call, force stop, script error, crash and explicit restart. No real IPC, deadline, cancellation hook or process termination. |
| Document result | One staged prefix and one undo expose the proposal. No proof of legacy transactions, selection returns or external side-effect rollback. |
| Existing helpers / modules | No capability removal implied. Frames/pixel userdata, timing/VFR, audio selection, project properties, fonts/text extents, GUI cursor/selection, clipboard, localization, paths and all native/FFI/module/DependencyControl behavior remain required but unimplemented here. |
| Existing stubs / quirks | Does not upgrade register_filter or set_undo_point, normalize callback behavior, honor ignored float step or turn alpha into a new numeric control. |

Default button construction/readback is source-observed in [AutomationDialog.cpp](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationDialog.cpp#L475). The [dialog entry point and progress bridge](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationProgress.cpp) require unchanged-script fixtures before native acceptance.

## Human decisions

1. **Manager placement:** task window, persistent tool, or task window plus a small reusable task/history tool?
2. **Cancellation/recovery presentation:** keep explicit Cancel → still waiting → Force stop → Restart, or prefer another presentation? Choose deadlines/automatic escalation separately.
3. **Concurrency and target reservation:** accept one active macro and reserved captured target, or require simultaneous work? Exact transaction/validation/undo changes still need named C06–C08 decisions and native proof.

## Verification and limits

JavaScript syntax was checked with Node. Root browser checks exercised typed options, staged prefix application and undo, explicit force stop, rejection of a terminal run's late reply, restart without rerunning the interrupted macro, protected-target refusal and both arrangements. Both arrangements were captured and inspected at the default 1280-wide viewport; no horizontal document overflow appeared in the tool arrangement. No Lua, native process, IPC, platform file picker, modules, scripts, files or network calls are executed. No timers or performance measurements exist. All media and text are synthetic. Dialog classes are a representative fixed schema; count/strength/style/color fields demonstrate typed return values, while this sample macro only uses prefix/enabled. This is not a full parity implementation or security sandbox. External file/network effects of real Lua/native modules cannot be rolled back by terminating a helper.

The shell textarea follows the accepted Enter-commits-and-advances choice and checks browser composition state; this is not proof of native QML/IME behavior or the pending editable hidden-tag projection. A study guard asks for applying an existing text draft before changing its selected Line or starting a macro on that target; it does not settle the product's command/draft policy. Native automation compatibility and ASS editor feasibility remain separate work.
