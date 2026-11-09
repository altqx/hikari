# HikariSub

HikariSub edits subtitle documents and coordinates their text, timing, styles and media context. This glossary records the shared language of the app.

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
A restorable set of open Documents and associated media, view positions and each tab's Workspace. Every tab belongs to a shell window; the Session does not yet record which ([MW4 #628](https://github.com/altqx/hikari/issues/628)).
_Avoid_: Project (the folder a window opened), Workspace layout or complete undo archive.

**Script properties**:
Document-level subtitle information, including title, credits, resolutions and ASS options. Script Info is the ASS section name.
_Avoid_: Script configuration when referring to these properties rather than automation.

**Line group**:
A description followed by contiguous member Lines, with expand/collapse behavior. Groups nest: a member can be the description of a group inside it ([#696](https://github.com/altqx/hikari/issues/696)).
_Avoid_: Tree when naming this feature.

**Grid filter**:
Criteria controlling which Lines are shown or exposed to a scoped operation. Hidden Lines still belong to the Document.
_Avoid_: Filter without qualification where automation or presentation is also discussed.

### Workspace and targeting

**Workspace**:
The arrangement of subtitle-editing panels and tools in a shell window and its floating panels. Each Document tab has its own, in its own window, and switching tabs shows that tab's ([ADR 0020](docs/adr/0020-per-document-tab-layouts.md)); it places panels and never changes which Document is the editing target.
_Avoid_: Document layout (the arrangement belongs to the tab, not to the Document's content); Workspace as a name for the set of open Documents or for an opened folder (a Project).

**Editing target**:
The subtitle document to which ordinary editing commands apply: the current tab of the shell window that was active last ([ADR 0025](docs/adr/0025-multiple-windows.md)). A tool with an explicit pinned target may name a different unprotected document. Within a window it is distinct from the control or panel that currently has keyboard focus; focus moving to another window makes that window's current tab the editing target.
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

**Style catalogue**:
A reusable collection of Styles outside the current Document.
_Avoid_: Catalogue without qualification; Document styles as a synonym.

**Font catalogue**:
A named collection of font-family names.
_Avoid_: Style catalogue, embedded fonts or bundled font files.

**ASS override tag**:
An instruction within subtitle text that affects presentation or timing.
_Avoid_: Filter as a synonym.

**Visual tool**:
A tool for manipulating subtitle placement, motion, scale, rotation, clipping or drawings in the video view.
_Avoid_: Video effect.

**Keyframe**:
A video frame identified by media indexing or an imported keyframe list, usable for navigation or snapping.
_Avoid_: Scene cut or ASS animation point as an equivalent term.

**Ink bounds**:
The smallest rectangle, in video pixels, that holds every pixel the subtitle renderer draws for one Line, or for every Line, on a frame.
_Avoid_: A Line's box, which is where its layout places it whether or not anything is drawn.

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

**Hikari API**:
The one typed, versioned set of operations over the running application that plugins, AI tools, the MCP server and node-editor nodes call.
_Avoid_: Automation API or Lua API, which name Aegisub automation's interface.

**Node effect**:
A graph of typed nodes, stored in its Document as a `graph <name>` Comment Line, that turns karaoke Lines into generated Lines marked `fx <name>`.
_Avoid_: Template or effector, which name the stock karaoke templater's Lines and Kara Effector.

**Plugin**:
A `.hikariplugin` package that extends HikariSub through the Hikari API, installed per user and run in its own helper.
_Avoid_: Automation script or macro, which name Aegisub automation; extension or add-on.

**Pinned key**:
The minisign public key HikariSub recorded at a plugin's first signed install; updates must be signed by it unless the user re-trusts another.
_Avoid_: Trusted key as a synonym; a pinned key says who signed before, not that the plugin is verified.

**Plugin marketplace**:
HikariSub's own signed index of plugins, run only by us, and the one source of listed plugins and their automatic updates ([ADR 0022](docs/adr/0022-hikarisub-plugins.md)).
_Avoid_: Store, feed or curated index; there are no third-party feeds.

**Unverified**:
A plugin sideloaded from a `.hikariplugin` file rather than installed from the Plugin marketplace, signed or not. It never updates by itself.

**Operation**:
One named, read- or write-marked entry of the Hikari API, with schemas for its parameters and result.
_Avoid_: Command, which names a menu, toolbar or shortcut action.

**AI tool**:
A function a model can call: an Operation offered under a model-safe name, or a search the AI sidecar answers itself. The core tools are always offered; the others are in groups that `search_tools` finds.
_Avoid_: Operation as a synonym; tool target, which names a tool's Document.

**Attached Document**:
A Document the user named with `@` in an AI thread. The AI's tools see it as well as the editing target.

**MCP server**:
HikariSub serving its AI tools and the open Documents over MCP to agents outside it, such as Claude Code. It is off until the user turns it on, and never on with AI off.
_Avoid_: MCP client, which is the sidecar reaching other servers.

### Files

**User folder**:
The one folder holding what a user may edit or keep: settings, automation scripts and modules, dictionaries, catalogues, presets, autosaves, recovery and logs. Automation's `?user` is this folder. Bundled scripts, modules and dictionaries are copied into it; caches live elsewhere.
_Avoid_: Settings folder, data folder or config folder as names for it; program folder for the bundled files' source.

**Project**:
A folder opened in a shell window as its context: the Explorer shows it, the AI may read inside it, and its Project tier applies to the window. A window has one Project or none, and a subtitle file opened by itself has none ([ADR 0030](docs/adr/0030-open-a-folder-as-a-project.md)). The Project is the folder; the Session is what is open in which window; a Workspace is how one tab's panels are arranged.
_Avoid_: Workspace or Session as names for the folder; Project as a name for the Document.

**Project file**:
The optional `.hikari/project.toml` in a Project. A Project exists without one; HikariSub writes it when a Project-level setting first changes.
_Avoid_: Session file.

**Project tier**:
Where a shared thing (a Style catalogue, a layout, dictionary words, a preset or a setting) is kept in the Project's `.hikari/` folder rather than the User folder. The UI marks the two tiers **Project** and **Mine**.
_Avoid_: Workspace settings.
