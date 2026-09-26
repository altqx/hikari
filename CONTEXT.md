# HikariSub

HikariSub edits subtitle documents and coordinates their text, timing, styles and media context. This glossary records the shared language of the rewrite.

## Language

### Documents and lines

**Line**:
An entry in the subtitle list, including speech, a sign, drawing or comment. A visual text wrap is not another subtitle line.
_Avoid_: Dialogue or event as a universal name for a line.

**ASS event**:
A Dialogue or Comment record in an ASS document.
_Avoid_: Event as the generic name for lines in every subtitle format.

**Document**:
The subtitle content being edited, including lines, styles and script properties, distinct from its saved file and linked media.
_Avoid_: Project, tab or Workspace as synonyms for the content.

**Document tab**:
The navigation entry for an open Document.
_Avoid_: Tab as a synonym for the Document's content or Workspace.

**Session**:
A restorable set of open Documents and associated media and view positions.
_Avoid_: Project file, Workspace layout or complete undo archive.

**Script properties**:
Document-level subtitle information, including title, credits, resolutions and ASS options. Script Info is the ASS section name.
_Avoid_: Script configuration when referring to these properties rather than automation.

**Line group**:
A description followed by contiguous member Lines, with expand/collapse behavior.
_Avoid_: Tree when naming this feature; arbitrary nested group as an implied capability.

**Grid filter**:
Criteria controlling which Lines are shown or exposed to a scoped operation. Hidden Lines still belong to the Document.
_Avoid_: Filter without qualification where automation or presentation is also discussed.

### Workspace and targeting

**Workspace**:
The application's shared arrangement of subtitle-editing panels and tools, independent of which document is the editing target.
_Avoid_: Document layout when referring to this shared arrangement.

**Editing target**:
The subtitle document to which ordinary editing commands apply. A tool with an explicit pinned target may name a different unprotected document. It is distinct from the control or panel that currently has keyboard focus.
_Avoid_: Focused panel as a synonym for the document receiving edits.

**Protected reference**:
A document shown for optional comparison whose content cannot be changed by editing-target commands.
_Avoid_: Second editing target.

**Tool target**:
The document explicitly named by a tool for inspection or an operation. It follows the editing target by default and can be pinned independently; a protected reference permits inspection but no content changes.
_Avoid_: Keyboard focus as a synonym for the tool target.

### Appearance and timing

**Style**:
A named set of subtitle appearance defaults used by Lines, distinct from inline overrides and the application's theme.
_Avoid_: Theme as a synonym for subtitle Style.

**Style catalog**:
A reusable collection of Styles outside the current Document.
_Avoid_: Catalog without qualification; Document styles as a synonym.

**Font catalog**:
A named collection of font-family names.
_Avoid_: Style catalog, embedded fonts or bundled font files.

**ASS override tag**:
An instruction within subtitle text that affects presentation or timing.
_Avoid_: Filter as a synonym.

**Visual tool**:
A tool for manipulating subtitle placement, motion, scale, rotation, clipping or drawings in the video view.
_Avoid_: Video effect.

**Keyframe**:
A video frame identified by media indexing or an imported keyframe list, usable for navigation or snapping.
_Avoid_: Scene cut or ASS animation point as an equivalent term.

**Time shift**:
An offset or alignment of chosen Line start or end times.
_Avoid_: Timing postprocessor as a synonym.

**Timing postprocessor**:
Timing adjustment using lead-in/out, continuity and keyframe tolerances.
_Avoid_: Postprocessor without qualification.

### Translation and automation

**Translation mode**:
Editing original and translated text together within each Line.
_Avoid_: Document comparison as a synonym.

**Original text / Translated text**:
The two text roles in Translation mode, within the same Line and Document.
_Avoid_: Second Document or Protected reference as names for the Original text field.

**Unconfirmed**:
The existing flag marking a Line for translation review. It is independent of saved state, spelling and general approval.
_Avoid_: Doubtful, unsaved or uncommitted as the user-facing label for this flag.

**Automation macro**:
A script-provided editing command, distinct from the script that registers it.
_Avoid_: Script as an interchangeable name for one registered command.

**Automation export filter**:
A compatibility-API term for a script-defined export-processing operation.
_Avoid_: Grid filter as a synonym.
