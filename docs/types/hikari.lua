---@meta hikari
-- LuaLS annotations for the `hikari` global of HikariSub plugins, generated from api.json by
-- `hikarisub --export-docs`. Put this file in your workspace's library; don't edit it.

---JSON null, as `hikari.null` stands for it.
---@class hikari.Null

---An error from HikariSub: the Hikari API's `{code, message, data}`.
---@class hikari.Error
---@field code string
---@field message string
---@field data? any

---A file handle: the handle, the path asked for, and `"read"` or `"write"`.
---@class hikari.File
---@field file string
---@field name string
---@field access "read"|"write"

---A panel or dialog event.
---@class hikari.UiEvent
---@field kind string
---@field widget? string
---@field value? any
---@field panel? string
---@field dialog? string
---@field button? string
---@field values? table<string, any>
---@field files? hikari.File[]

---Stages the edits of one `hikari.document.edit`.
---@class hikari.Change
local Change = {}
---@param line integer
---@param fields table
function Change:set_line(line, fields) end
---@param fields table
---@param where { after?: integer, before?: integer }
function Change:insert_line(fields, where) end
---@param line integer
function Change:delete_line(line) end
---@param line integer
---@param before integer?
function Change:move_line(line, before) end
---@param name string
---@param fields table
function Change:set_style(name, fields) end
---@param name string
function Change:delete_style(name) end
---@param key string
---@param value string?
function Change:set_info(key, value) end

---A snapshot of one Document, at one revision.
---@class hikari.Snapshot
---@field document integer
---@field revision integer
---@field title string
---@field format string
---@field lines hikari.Line[]
---@field styles hikari.Style[]
---@field info { key: string, value: string }[]

---An open Document, as documents.list gives it.
---@alias hikari.DocumentId integer

---A skill's name, which is its folder's.
---@alias hikari.SkillName string

---A Line's stable id: it survives edits, moves and undo, unlike its row.
---@alias hikari.LineId integer

---Counts the Document's committed changes; a write names the revision it was prepared against.
---@alias hikari.Revision integer

---@alias hikari.LineField "comment"|"layer"|"start"|"end"|"style"|"actor"|"marginL"|"marginR"|"marginV"|"effect"|"text"|"translation"|"unconfirmed"|"generatedFrom"

---Times are milliseconds. In translation mode text is the original and translation the translated text.
---@class hikari.Line
---@field id hikari.LineId
---@field row integer 1-based position among the Document's Lines.
---@field comment? boolean
---@field layer? integer
---@field start? integer
---@field end? integer
---@field style? string
---@field actor? string
---@field marginL? integer
---@field marginR? integer
---@field marginV? integer
---@field effect? string
---@field text? string
---@field translation? string
---@field unconfirmed? boolean
---@field generatedFrom? hikari.LineId|hikari.Null The karaoke Line a templater run generated this one from.

---An ASS v4+ Style. Text fields hold the field's ASS text; colours are &HAABBGGRR.
---@class hikari.Style
---@field name string
---@field fontname? string
---@field fontsize? string
---@field primaryColour? string
---@field secondaryColour? string
---@field outlineColour? string
---@field backColour? string
---@field bold? boolean
---@field italic? boolean
---@field underline? boolean
---@field strikeOut? boolean
---@field scaleX? string
---@field scaleY? string
---@field spacing? string
---@field angle? string
---@field borderStyle? 1|3|4
---@field outline? string
---@field shadow? string
---@field alignment? string
---@field marginL? string
---@field marginR? string
---@field marginV? string
---@field encoding? string

---Line fields to set; the others keep their values.
---@class hikari.LineFields
---@field comment? boolean
---@field layer? integer
---@field start? integer
---@field end? integer
---@field style? string
---@field actor? string
---@field marginL? integer
---@field marginR? integer
---@field marginV? integer
---@field effect? string
---@field text? string
---@field translation? string
---@field unconfirmed? boolean

---Style fields to set; name renames it. A new Style starts from Arial 48.
---@class hikari.StyleFields
---@field name? string
---@field fontname? string
---@field fontsize? string
---@field primaryColour? string
---@field secondaryColour? string
---@field outlineColour? string
---@field backColour? string
---@field bold? boolean
---@field italic? boolean
---@field underline? boolean
---@field strikeOut? boolean
---@field scaleX? string
---@field scaleY? string
---@field spacing? string
---@field angle? string
---@field borderStyle? 1|3|4
---@field outline? string
---@field shadow? string
---@field alignment? string
---@field marginL? string
---@field marginR? string
---@field marginV? string
---@field encoding? string

---@alias hikari.Edit { op: "set_line", line: hikari.LineId, fields: hikari.LineFields }|{ op: "insert_line", after?: hikari.LineId, before?: hikari.LineId, fields?: hikari.LineFields }|{ op: "delete_line", line: hikari.LineId }|{ op: "move_line", line: hikari.LineId, before: hikari.LineId|hikari.Null }|{ op: "set_style", name: string, fields?: hikari.StyleFields }|{ op: "delete_style", name: string }|{ op: "set_info", key: string, value: string|hikari.Null }

---@alias hikari.SettingValue boolean|integer|string|string[]

---@class hikari.NodePort
---@field name string
---@field type string

---@class hikari.GrantedFile
---@field file string The handle files.read and files.write take.
---@field name string
---@field access "read"|"write"

---@class hikari.GrantedFolder
---@field folder string The handle files.list, files.read and files.write take with a path.
---@field name string
---@field access "read"|"write"

---@class hikari.FolderEntry
---@field path string Relative to the granted folder, with / between its parts.
---@field folder boolean

---The Project of the window the user works in; null without one.
---@alias hikari.Project hikari.Null|{ name: string, trusted: boolean }

---@class hikari.DialogControl
---@field name string
---@field kind "edit"|"textbox"|"intedit"|"floatedit"|"dropdown"|"checkbox"|"color"|"coloralpha"|"alpha" The control's Lua class, lowercased.
---@field label? string A checkbox's text.
---@field hint? string
---@field items? string[] A dropdown's choices.
---@field min? number
---@field max? number
---@field step? number A floatedit's step; 0 for none.

---@class hikari.DialogAnswer
---@field key? string The dialog's key: its script and its controls' kinds and names, as Automation remembers answers by. When given, the key alone decides which dialog the answer fills.
---@field controls? hikari.DialogControl[] The dialog's value-bearing controls, in order. Without a key, the answer fills the dialog of the script being run whose controls have these kinds and names; the other fields don't count.
---@field values table<string, string|number|boolean> The answered values by control name.
---@field button? string The pressed button's label; empty when the dialog was closed.

---A theme role, so the mark follows the theme and high contrast.
---@alias hikari.MarkColour "accent"|"warning"|"danger"|"success"|"muted"

---A started program, such as p1.
---@alias hikari.ProgramProcess string

---The params of documents.list.
---@alias hikari.DocumentsListParams table<string, any>

---The result of documents.list.
---@class hikari.DocumentsListResult
---@field documents { document?: hikari.DocumentId, title?: string, path?: string|hikari.Null, format?: "ass"|"srt"|"tmplayer"|"microdvd"|"mpl2"|"text", revision?: hikari.Revision, editingTarget?: boolean, reference?: boolean, dirty?: boolean, readOnly?: boolean }[]

---The params of documents.get.
---@class hikari.DocumentsGetParams
---@field document? hikari.DocumentId Defaults to the active Document.

---The result of documents.get.
---@class hikari.DocumentsGetResult
---@field document? hikari.DocumentId
---@field title? string
---@field path? string|hikari.Null
---@field format? "ass"|"srt"|"tmplayer"|"microdvd"|"mpl2"|"text"
---@field revision? hikari.Revision
---@field editingTarget? boolean
---@field reference? boolean The protected comparison reference.
---@field dirty? boolean
---@field readOnly? boolean A macro holds it, or it is the protected reference.
---@field lineCount? integer
---@field styleCount? integer
---@field draftLine? hikari.LineId|hikari.Null The Line with uncommitted Line editor changes; a write commits them first.

---The params of lines.list.
---@class hikari.LinesListParams
---@field document? hikari.DocumentId Defaults to the active Document.
---@field offset? integer
---@field limit? integer 1000 when omitted.
---@field fields? hikari.LineField[] Only these fields besides id and row; all of them when omitted.

---The result of lines.list.
---@class hikari.LinesListResult
---@field revision hikari.Revision
---@field total? integer
---@field lines hikari.Line[]

---The params of lines.get.
---@class hikari.LinesGetParams
---@field document? hikari.DocumentId Defaults to the active Document.
---@field ids hikari.LineId[]
---@field fields? hikari.LineField[] Only these fields besides id and row; all of them when omitted.

---The result of lines.get.
---@class hikari.LinesGetResult
---@field revision hikari.Revision
---@field total? integer
---@field lines hikari.Line[]

---The params of lines.find.
---@class hikari.LinesFindParams
---@field document? hikari.DocumentId Defaults to the active Document.
---@field text? string A case-sensitive substring.
---@field regex? string A regular expression, as Find and replace takes it.
---@field ignoreCase? boolean For regex.
---@field in? "text"|"translation"|"actor"|"effect"|"style" Where text and regex look; text when omitted.
---@field style? string
---@field actor? string
---@field effect? string
---@field comment? boolean
---@field from? integer Lines ending after this time, in milliseconds.
---@field to? integer Lines starting before this time, in milliseconds.
---@field limit? integer 500 when omitted; total still counts every match.
---@field fields? hikari.LineField[] Only these fields besides id and row; all of them when omitted.

---The result of lines.find.
---@class hikari.LinesFindResult
---@field revision hikari.Revision
---@field total? integer
---@field lines hikari.Line[]

---The params of styles.list.
---@class hikari.StylesListParams
---@field document? hikari.DocumentId Defaults to the active Document.

---The result of styles.list.
---@class hikari.StylesListResult
---@field revision hikari.Revision
---@field styles hikari.Style[]

---The params of info.list.
---@class hikari.InfoListParams
---@field document? hikari.DocumentId Defaults to the active Document.

---The result of info.list.
---@class hikari.InfoListResult
---@field revision hikari.Revision
---@field info { key: string, value: string }[]

---The params of selection.get.
---@class hikari.SelectionGetParams
---@field document? hikari.DocumentId Defaults to the active Document.

---The result of selection.get.
---@class hikari.SelectionGetResult
---@field document hikari.DocumentId
---@field revision hikari.Revision
---@field active hikari.LineId|hikari.Null
---@field selected hikari.LineId[]

---The params of api.version.
---@alias hikari.ApiVersionParams table<string, any>

---The result of api.version.
---@class hikari.ApiVersionResult
---@field version integer

---The params of commands.list.
---@class hikari.CommandsListParams
---@field window? "global"|"subtitles"|"editor"|"video"|"audio"
---@field query? string A case-insensitive part of the command id or its name.

---The result of commands.list.
---@class hikari.CommandsListResult
---@field commands { command: string, name: string, window: "global"|"subtitles"|"editor"|"video"|"audio", enabled: boolean, disabledReason?: string, keys: string[], kind?: "hotkey"|"macro"|"plugin", file?: string, macro?: string }[]

---The params of panels.list.
---@alias hikari.PanelsListParams table<string, any>

---The result of panels.list.
---@class hikari.PanelsListResult
---@field panels { panel: string, title: string, open: boolean, floating: boolean, focused: boolean }[]

---The params of settings.list.
---@class hikari.SettingsListParams
---@field prefix? string Only ids starting with it, e.g. grid.

---The result of settings.list.
---@class hikari.SettingsListResult
---@field settings { setting: string, type: "boolean"|"integer"|"string"|"stringList", scope: "profile"|"collections"|"workspace", default: hikari.SettingValue, value: hikari.SettingValue }[]

---The params of settings.get.
---@class hikari.SettingsGetParams
---@field settings string[]

---The result of settings.get.
---@class hikari.SettingsGetResult
---@field values table<string, hikari.SettingValue>

---The params of commands.run.
---@class hikari.CommandsRunParams
---@field command string A command id from commands.list.

---The result of commands.run.
---@class hikari.CommandsRunResult
---@field command string
---@field ran true

---The params of panels.open.
---@class hikari.PanelsOpenParams
---@field panel string A panel id from panels.list.

---The result of panels.open.
---@class hikari.PanelsOpenResult
---@field panel string
---@field title string
---@field open boolean
---@field floating boolean
---@field focused boolean

---The params of panels.close.
---@class hikari.PanelsCloseParams
---@field panel string A panel id from panels.list.

---The result of panels.close.
---@class hikari.PanelsCloseResult
---@field panel string
---@field title string
---@field open boolean
---@field floating boolean
---@field focused boolean

---The params of panels.focus.
---@class hikari.PanelsFocusParams
---@field panel string A panel id from panels.list.

---The result of panels.focus.
---@class hikari.PanelsFocusResult
---@field panel string
---@field title string
---@field open boolean
---@field floating boolean
---@field focused boolean

---The params of settings.set.
---@class hikari.SettingsSetParams
---@field setting string
---@field value hikari.SettingValue

---The result of settings.set.
---@class hikari.SettingsSetResult
---@field setting string
---@field value hikari.SettingValue

---The params of document.apply.
---@class hikari.DocumentApplyParams
---@field document? hikari.DocumentId Defaults to the active Document.
---@field revision hikari.Revision The revision the edits were prepared against.
---@field name string A few words naming the change, as the user reads it in the review and in History after the caller's name.
---@field edits hikari.Edit[] Applied in order; any refusal leaves the Document as it was.

---The result of document.apply.
---@class hikari.DocumentApplyResult
---@field revision hikari.Revision
---@field inserted hikari.LineId[] New Lines, in edit order.
---@field changed boolean False when the edits changed nothing: no undo step was added.

---The params of selection.set.
---@class hikari.SelectionSetParams
---@field document? hikari.DocumentId Defaults to the active Document.
---@field selected hikari.LineId[]
---@field active? hikari.LineId|hikari.Null The first selected Line when omitted.

---The result of selection.set.
---@class hikari.SelectionSetResult
---@field document hikari.DocumentId
---@field revision hikari.Revision
---@field active hikari.LineId|hikari.Null
---@field selected hikari.LineId[]

---The params of document.group_begin.
---@class hikari.DocumentGroupBeginParams
---@field name string A few words naming the grouped step, as the user reads it in History after the caller's name.

---The result of document.group_begin.
---@class hikari.DocumentGroupBeginResult
---@field document hikari.DocumentId

---The params of document.group_end.
---@alias hikari.DocumentGroupEndParams table<string, any>

---The result of document.group_end.
---@class hikari.DocumentGroupEndResult
---@field steps integer How many steps became one; 0 when the group made none. A group someone else's step ended earlier is no longer open.

---The params of video.info.
---@alias hikari.VideoInfoParams table<string, any>

---The result of video.info.
---@class hikari.VideoInfoResult
---@field open boolean
---@field width? integer
---@field height? integer
---@field aspect? integer[]
---@field frameShown? integer
---@field keyframes? integer[]

---The params of video.frame_at_time.
---@class hikari.VideoFrameAtTimeParams
---@field time integer Milliseconds.

---The result of video.frame_at_time.
---@class hikari.VideoFrameAtTimeResult
---@field frame integer

---The params of video.time_of_frame.
---@class hikari.VideoTimeOfFrameParams
---@field frame integer

---The result of video.time_of_frame.
---@class hikari.VideoTimeOfFrameResult
---@field time integer

---The params of video.frame.
---@class hikari.VideoFrameParams
---@field frame? integer Default: the frame shown, or with line the Line's first frame when it is not on the frame shown.
---@field subtitles? boolean Render the subtitles on it. Default true.
---@field document? hikari.DocumentId line's Document. Defaults to the active Document.
---@field line? hikari.LineId Draw this Line alone.
---@field crop? { x: integer, y: integer, width: integer, height: integer }
---@field maxEdge? integer Default 1024.
---@field maxPixels? integer The most pixels the image may have. No limit when omitted.

---The result of video.frame.
---@class hikari.VideoFrameResult
---@field frame integer
---@field width integer
---@field height integer
---@field crop { x: integer, y: integer, width: integer, height: integer }
---@field format "bgra"
---@field data string width * 4 bytes per row, top row first, base64.

---The params of video.ink_bounds.
---@class hikari.VideoInkBoundsParams
---@field frame? integer Default: the frame shown, or with line the Line's first frame when it is not on the frame shown.
---@field document? hikari.DocumentId Defaults to the active Document.
---@field line? hikari.LineId This Line alone. Every Line when omitted.

---The result of video.ink_bounds.
---@class hikari.VideoInkBoundsResult
---@field frame integer
---@field bounds { x: integer, y: integer, width: integer, height: integer }|hikari.Null Null when nothing is drawn.

---The params of video.colours.
---@class hikari.VideoColoursParams
---@field frame? integer Default: the frame shown.
---@field points { x: integer, y: integer }[]
---@field radius? integer Default 0, the pixel alone.
---@field subtitles? boolean Sample with the subtitles drawn. Default false.

---The result of video.colours.
---@class hikari.VideoColoursResult
---@field frame integer
---@field colours { x: integer, y: integer, colour: string }[]

---The params of video.keyframes.
---@class hikari.VideoKeyframesParams
---@field from? integer Milliseconds. Default 0.
---@field to? integer Milliseconds. Default: the end.

---The result of video.keyframes.
---@class hikari.VideoKeyframesResult
---@field keyframes { frame: integer, time: integer }[]
---@field total integer Every keyframe in the range; the first 500 are listed.

---The params of video.frames.
---@class hikari.VideoFramesParams
---@field from integer
---@field to integer
---@field stride? integer Default 1, every frame.
---@field maxEdge? integer Default 64.
---@field format? "luma"|"bgra" Default luma, one byte per pixel.
---@field subtitles? boolean Render the subtitles on them. Default false.

---The result of video.frames.
---@class hikari.VideoFramesResult
---@field width integer
---@field height integer
---@field format "luma"|"bgra"
---@field frames { frame: integer, data: string }[]

---The params of video.set_keyframes.
---@class hikari.VideoSetKeyframesParams
---@field frames integer[]
---@field file? string A writable file handle, from files.pick or files.open.

---The result of video.set_keyframes.
---@class hikari.VideoSetKeyframesResult
---@field count integer
---@field file? string

---The params of audio.info.
---@alias hikari.AudioInfoParams table<string, any>

---The result of audio.info.
---@class hikari.AudioInfoResult
---@field open boolean
---@field ready? boolean
---@field duration? integer
---@field sampleRate? integer
---@field channels? integer

---The params of audio.peaks.
---@class hikari.AudioPeaksParams
---@field start integer
---@field end integer
---@field buckets? integer Default 256.

---The result of audio.peaks.
---@class hikari.AudioPeaksResult
---@field start integer
---@field end integer
---@field min integer[]
---@field max integer[]

---The params of files.list.
---@class hikari.FilesListParams
---@field folder? string A folder handle to list.
---@field path? string A folder inside it; default the folder itself.

---The result of files.list.
---@class hikari.FilesListResult
---@field files hikari.GrantedFile[]
---@field folders? hikari.GrantedFolder[]
---@field entries? hikari.FolderEntry[] With folder: in name order, at most 4096, without links that lead outside it.
---@field truncated? boolean Present when the folder holds more than 4096 entries.

---The params of files.read.
---@class hikari.FilesReadParams
---@field file? string A handle from files.list.
---@field folder? string Instead of file, a folder handle from files.list.
---@field path? string With folder, the file's path inside it, with / between its parts.
---@field encoding? "utf8"|"base64" Default utf8.

---The result of files.read.
---@class hikari.FilesReadResult
---@field file string
---@field size integer
---@field encoding "utf8"|"base64"
---@field data string

---The params of files.write.
---@class hikari.FilesWriteParams
---@field file? string A handle from files.list.
---@field folder? string Instead of file, a folder handle from files.list.
---@field path? string With folder, the file's path inside it, with / between its parts.
---@field encoding? "utf8"|"base64" Default utf8.
---@field data string

---The result of files.write.
---@class hikari.FilesWriteResult
---@field file string
---@field size integer
---@field staged? boolean True: waiting in the review, not on the disk yet.

---The params of skills.list.
---@alias hikari.SkillsListParams table<string, any>

---The result of skills.list.
---@class hikari.SkillsListResult
---@field skills { skill: hikari.SkillName, origin: "project"|"yours", files: string[] }[]

---The params of skills.read.
---@class hikari.SkillsReadParams
---@field skill hikari.SkillName
---@field file string A path skills.list gives, such as SKILL.md or references/tags.md.

---The result of skills.read.
---@class hikari.SkillsReadResult
---@field skill hikari.SkillName
---@field file string
---@field size integer
---@field data string

---The params of automation.macros.
---@class hikari.AutomationMacrosParams
---@field query? string Keep macros whose name, script or file name holds this, ignoring case.

---The result of automation.macros.
---@class hikari.AutomationMacrosResult
---@field macros { script: string, file: string, macro: string, ordinal: integer, description: string, enabled: boolean, disabledReason?: string }[]

---The params of automation.run_macro.
---@class hikari.AutomationRunMacroParams
---@field file string The script's file name.
---@field macro string
---@field answers? hikari.DialogAnswer[] One per dialog, in order, as macro.ran gives them.
---@field show? "never"|"prefilled" never (the default) answers a dialog whose key matches without showing it; prefilled shows it filled in. A dialog without a matching answer, and a question, is always shown.

---The result of automation.run_macro.
---@class hikari.AutomationRunMacroResult
---@field outcome "applied"|"refused"|"failed"|"cancelled"|"declined"|"stopped"|"lost"
---@field message string
---@field log string
---@field dialogs? hikari.DialogAnswer[] The answers its dialogs got, as macro.ran gives them; only when the caller sees the Document it ran on.

---The params of automation.run.
---@class hikari.AutomationRunParams
---@field name string The script's file name without .lua; an earlier script of that name is replaced.
---@field script string The whole Lua script, registering its macro with aegisub.register_macro.
---@field macro? string The macro to run, by its registered name; default the first.

---The result of automation.run.
---@class hikari.AutomationRunResult
---@field file string The saved script's file name.
---@field outcome "applied"|"refused"|"failed"|"cancelled"|"declined"|"stopped"|"lost"|"invalid"
---@field message string
---@field log string

---The params of translation.project.
---@class hikari.TranslationProjectParams
---@field document? hikari.DocumentId Defaults to the active Document.

---The result of translation.project.
---@class hikari.TranslationProjectResult
---@field folder string|hikari.Null
---@field files { name: "PROJECT.md"|"GLOSSARY.md"|"CHARACTERS.md"|"STYLE.md", data: string }[]

---The params of translation.project_write.
---@class hikari.TranslationProjectWriteParams
---@field document? hikari.DocumentId Defaults to the active Document.
---@field file "PROJECT.md"|"GLOSSARY.md"|"CHARACTERS.md"|"STYLE.md"
---@field data string

---The result of translation.project_write.
---@class hikari.TranslationProjectWriteResult
---@field file "PROJECT.md"|"GLOSSARY.md"|"CHARACTERS.md"|"STYLE.md"
---@field size integer

---The params of nodes.catalogue.
---@class hikari.NodesCatalogueParams
---@field category? string

---The result of nodes.catalogue.
---@class hikari.NodesCatalogueResult
---@field nodes { id: string, category: string, summary: string, inputs: hikari.NodePort[], outputs: hikari.NodePort[], params: table<string, any>, since: integer }[]

---The params of nodes.validate.
---@class hikari.NodesValidateParams
---@field graph table<string, any>

---The result of nodes.validate.
---@class hikari.NodesValidateResult
---@field valid boolean
---@field problems { node?: string, port?: string, message: string }[]

---The params of effects.graphs.
---@class hikari.EffectsGraphsParams
---@field document? hikari.DocumentId Defaults to the active Document.

---The result of effects.graphs.
---@class hikari.EffectsGraphsResult
---@field document hikari.DocumentId
---@field revision hikari.Revision
---@field graphs { line: hikari.LineId, name: string, style: string, valid: boolean }[]

---The params of effects.graph.
---@class hikari.EffectsGraphParams
---@field document? hikari.DocumentId Defaults to the active Document.
---@field line hikari.LineId

---The result of effects.graph.
---@class hikari.EffectsGraphResult
---@field line hikari.LineId
---@field name string
---@field style string
---@field revision hikari.Revision
---@field graph table<string, any>|hikari.Null Null when the Line's text is no graph.
---@field valid boolean
---@field problems { node?: string, port?: string, message: string }[]

---The params of effects.edit.
---@class hikari.EffectsEditParams
---@field graph table<string, any> A graph as effects.graph gives it; {"format": 1, "nodes": [], "wires": []} starts an empty one.
---@field steps { op: "add"|"connect"|"disconnect"|"remove"|"set", type?: string, node?: string, fields?: table<string, any>, from?: string, to?: string, field?: string, value?: any }[] Applied in order; a refused step changes nothing.

---The result of effects.edit.
---@class hikari.EffectsEditResult
---@field graph table<string, any>
---@field added string[] The ids of the nodes added, in step order.
---@field valid boolean
---@field problems { node?: string, port?: string, message: string }[]

---The params of effects.preview.
---@class hikari.EffectsPreviewParams
---@field document? hikari.DocumentId Defaults to the active Document.
---@field line? hikari.LineId A graph Line: its graph, name and Style.
---@field graph? table<string, any> A graph to run in place of the Line's.
---@field style? string The Style whose karaoke Lines it runs on; needed without line.
---@field name? string The graph's name, which the Lines' Effect "fx <name>" carries.
---@field sources? hikari.LineId[] Only these karaoke Lines; all of them when omitted.
---@field limit? integer How many generated Lines to give; default 20.

---The result of effects.preview.
---@class hikari.EffectsPreviewResult
---@field ran boolean False when a problem stopped the run; applying it would change nothing.
---@field complete boolean
---@field sources integer How many karaoke Lines ran.
---@field generated integer How many Lines the run made.
---@field lines string[] The first generated Lines, as ASS Dialogue lines.
---@field problems { node?: string, line?: hikari.LineId, message: string }[]

---The params of effects.save.
---@class hikari.EffectsSaveParams
---@field document? hikari.DocumentId Defaults to the active Document.
---@field graph table<string, any>
---@field line? hikari.LineId The graph Line to replace; omit it to add a graph.
---@field name? string A new graph's name.
---@field style? string A new graph's Style: whose karaoke Lines it runs on.

---The result of effects.save.
---@class hikari.EffectsSaveResult
---@field line hikari.LineId
---@field revision hikari.Revision
---@field graph table<string, any>
---@field valid boolean
---@field problems { node?: string, port?: string, message: string }[]

---The params of effects.apply.
---@class hikari.EffectsApplyParams
---@field document? hikari.DocumentId Defaults to the active Document.
---@field line hikari.LineId The graph Line.
---@field sources? hikari.LineId[] Only these karaoke Lines; all of them when omitted.

---The result of effects.apply.
---@class hikari.EffectsApplyResult
---@field revision hikari.Revision
---@field generated integer
---@field sources integer How many karaoke Lines got Lines.
---@field changed boolean

---The params of plugins.list.
---@alias hikari.PluginsListParams table<string, any>

---The result of plugins.list.
---@class hikari.PluginsListResult
---@field plugins { id: string, name: string, version: string, description: string, source: "file"|"marketplace", signed: boolean, enabled: boolean, tier: "restricted"|"unrestricted", revoked: boolean, commands: { command: string, title: string }[] }[]

---The params of plugins.manifest.
---@class hikari.PluginsManifestParams
---@field plugin string

---The result of plugins.manifest.
---@class hikari.PluginsManifestResult
---@field plugin string
---@field manifest table<string, any>

---The params of plugins.scaffold.
---@class hikari.PluginsScaffoldParams
---@field plugin string Reverse-DNS, lowercase: com.example.sign-counter. An earlier scaffold of that id is replaced.
---@field name string
---@field version? string Semantic version; default 0.1.0.
---@field description? string
---@field tier? "restricted"|"unrestricted" restricted (default) runs in the sandbox without ffi, io, os and debug; unrestricted needs the user's full trust at install.
---@field permissions? table<string, any> The manifest's permissions: documents.all, network {hosts, reason}, files, vcs.read.
---@field commands { id: string, title: string }[]
---@field code? string main.lua, registering each command with hikari.commands.register; the guide's first plugin when omitted.
---@field files? { path: string, text: string }[] More files beside main.lua and hikari-plugin.json.

---The result of plugins.scaffold.
---@class hikari.PluginsScaffoldResult
---@field plugin string
---@field folder string
---@field package string The packed .hikariplugin.
---@field files string[]
---@field manifest table<string, any>

---The params of plugins.review.
---@class hikari.PluginsReviewParams
---@field plugin? string A plugin plugins.scaffold made.
---@field file? string A .hikariplugin the user granted, by its files.list handle.

---The result of plugins.review.
---@class hikari.PluginsReviewResult
---@field plugin string
---@field name string
---@field version string
---@field change "install"|"update"|"reinstall"|"downgrade"
---@field installedVersion string
---@field tier "restricted"|"unrestricted" unrestricted runs outside the sandbox and needs the user's full trust.
---@field needsRetrust boolean The signing key changed, or an unsigned package replaces a signed one.
---@field keyId string The signing key; empty when unsigned.
---@field pinnedKeyId string
---@field permissions string[]
---@field commands string[] The titles of the commands it adds.
---@field files { path: string, text: string }[]
---@field digest string

---The params of plugins.install.
---@class hikari.PluginsInstallParams
---@field plugin? string A plugin plugins.scaffold made.
---@field file? string A .hikariplugin the user granted, by its files.list handle.

---The result of plugins.install.
---@class hikari.PluginsInstallResult
---@field plugin string
---@field name string
---@field version string
---@field change "install"|"update"|"reinstall"|"downgrade"
---@field installedVersion string
---@field tier "restricted"|"unrestricted" unrestricted runs outside the sandbox and needs the user's full trust.
---@field needsRetrust boolean The signing key changed, or an unsigned package replaces a signed one.
---@field keyId string The signing key; empty when unsigned.
---@field pinnedKeyId string
---@field permissions string[]
---@field commands string[] The titles of the commands it adds.
---@field files { path: string, text: string }[]
---@field digest string
---@field installed true

---The params of plugins.run.
---@class hikari.PluginsRunParams
---@field plugin string
---@field command string The command's id within the plugin, as in its manifest.

---The result of plugins.run.
---@class hikari.PluginsRunResult
---@field ran true

---The params of typeset.perspective.
---@class hikari.TypesetPerspectiveParams
---@field document? hikari.DocumentId Defaults to the active Document.
---@field line hikari.LineId
---@field quad { x: number, y: number }[] Script coordinates: top left, top right, bottom right, bottom left.
---@field origin? "center"|"nofax"|"keep" Where \org goes: the quad's centre (default), the centre without \fax, or where the Line has it.

---The result of typeset.perspective.
---@class hikari.TypesetPerspectiveResult
---@field line hikari.LineId
---@field text string
---@field transform table<string, any>

---The params of typeset.clip.
---@class hikari.TypesetClipParams
---@field document? hikari.DocumentId Defaults to the active Document.
---@field line hikari.LineId
---@field rect? { x1: number, y1: number, x2: number, y2: number } Script coordinates, rounded to whole units.
---@field vector? string ASS drawing commands such as "m 0 0 l 100 0 100 50 0 50"; empty removes the vector clip.
---@field inverse? boolean \iclip: draw outside the clip.

---The result of typeset.clip.
---@class hikari.TypesetClipResult
---@field line hikari.LineId
---@field text string The Line's text with the tags written; nothing is applied.

---The params of typeset.drawing.
---@class hikari.TypesetDrawingParams
---@field document? hikari.DocumentId Defaults to the active Document.
---@field line hikari.LineId
---@field shape string ASS drawing commands, in script units from the drawing's origin.
---@field position? { x: number, y: number }
---@field alignment? integer \an; 7 (top left) with position, else the Line's.

---The result of typeset.drawing.
---@class hikari.TypesetDrawingResult
---@field line hikari.LineId
---@field text string The Line's text with the tags written; nothing is applied.

---The params of typeset.shift.
---@class hikari.TypesetShiftParams
---@field document? hikari.DocumentId Defaults to the active Document.
---@field lines hikari.LineId[]
---@field dx number Script units.
---@field dy number
---@field kinds? ("position"|"move_start"|"move_end"|"clip"|"drawing"|"origin")[] What moves; every kind but drawing when omitted.

---The result of typeset.shift.
---@class hikari.TypesetShiftResult
---@field lines { line: hikari.LineId, text: string }[] The Lines the change alters, with their new text; nothing is applied.

---The params of typeset.hydra.
---@class hikari.TypesetHydraParams
---@field document? hikari.DocumentId Defaults to the active Document.
---@field lines hikari.LineId[] Multiply and the line gradients count them in this order.
---@field tag string A Hydra tag without the backslash: blur, bord, be, alpha, 1a, 3a, 4a, 1c (or c), 3c, 4c, fad, fax, fay, fs, fsp, shad, xbord, ybord, xshad, yshad, pos or t.
---@field change "set"|"add"|"multiply"|"gradient"
---@field values number[] set: the value; add: what to add; multiply: the start; gradient: the first value. Colours are R, G, B.
---@field perLine? number[] multiply: added for each Line.
---@field byLine? "set"|"add" multiply: set each Line's value, or add to it. Default set.
---@field to? number[] gradient: the last value.
---@field across? "characters"|"lines" gradient: default characters.
---@field decreasing? boolean gradient: run from the end.

---The result of typeset.hydra.
---@class hikari.TypesetHydraResult
---@field lines { line: hikari.LineId, text: string }[] The Lines the change alters, with their new text; nothing is applied.

---The params of typeset.check.
---@class hikari.TypesetCheckParams
---@field document? hikari.DocumentId Defaults to the active Document.
---@field line hikari.LineId
---@field text? string The proposed text; the Line's own when omitted.
---@field target { quad: { x: number, y: number }[] }|{ box: { x: number, y: number, width: number, height: number } } Where the Line's ink should land, in script coordinates: a quad or a box.
---@field frame? integer Default: the frame shown when the Line is on it, else the Line's first.
---@field tolerance? number Video pixels; default 4.

---The result of typeset.check.
---@class hikari.TypesetCheckResult
---@field line hikari.LineId
---@field frame integer
---@field pass boolean
---@field error number|hikari.Null The largest distance between an edge of the ink's bounds and the target's, in video pixels; null when nothing is drawn.
---@field ink { x: number, y: number, width: number, height: number }|hikari.Null
---@field target { x: number, y: number, width: number, height: number }

---The params of typeset.apply.
---@class hikari.TypesetApplyParams
---@field document? hikari.DocumentId Defaults to the active Document.
---@field revision hikari.Revision
---@field name string A few words naming the change, such as "Typeset the shop sign".
---@field lines { line: hikari.LineId, text: string, target: { quad: { x: number, y: number }[] }|{ box: { x: number, y: number, width: number, height: number } }, frame?: integer }[]
---@field tolerance? number Video pixels; default 4.

---The result of typeset.apply.
---@class hikari.TypesetApplyResult
---@field revision hikari.Revision
---@field inserted hikari.LineId[]
---@field changed boolean
---@field checks { line: hikari.LineId, frame: integer, pass: boolean, error: number|hikari.Null, ink: { x: number, y: number, width: number, height: number }|hikari.Null, target: { x: number, y: number, width: number, height: number } }[]

---The params of project.info.
---@alias hikari.ProjectInfoParams table<string, any>

---The result of project.info.
---@class hikari.ProjectInfoResult
---@field project hikari.Project

---The params of vcs.refs.
---@class hikari.VcsRefsParams
---@field document? hikari.DocumentId Defaults to the active Document; an unsaved one gives the Project's repository.
---@field limit? integer How many recent commits; 50 when left out.

---The result of vcs.refs.
---@class hikari.VcsRefsResult
---@field root string The repository's work tree.
---@field path string|hikari.Null The Document's path in the repository, '/'-separated; null for the Project's.
---@field head hikari.Null|{ ref: string|hikari.Null, commit: string } null before the first commit.
---@field branches { name: string, commit: string, current: boolean }[] At most 500, newest first.
---@field tags { name: string, commit: string }[] At most 500, newest first.
---@field commits { commit: string, summary: string, author: string, time: integer }[] The newest from HEAD.

---The params of vcs.show.
---@class hikari.VcsShowParams
---@field ref string A branch, tag, commit or revision such as HEAD~2.
---@field document? hikari.DocumentId Defaults to the active Document.

---The result of vcs.show.
---@class hikari.VcsShowResult
---@field ref string
---@field commit string
---@field path string The Document's path in the repository, '/'-separated.
---@field text string The file at that commit, as UTF-8.

---The params of karaoke.syllables.
---@class hikari.KaraokeSyllablesParams
---@field document? hikari.DocumentId Defaults to the active Document.
---@field line hikari.LineId

---The result of karaoke.syllables.
---@class hikari.KaraokeSyllablesResult
---@field line hikari.LineId
---@field syllables { text: string, stripped: string, tag: string, start: integer, end: integer }[]

---The params of karaoke.split.
---@class hikari.KaraokeSplitParams
---@field document? hikari.DocumentId Defaults to the active Document.
---@field lines hikari.LineId[]
---@field bySyllable? boolean Default true: by syllable, with the language rules the user set; false: at spaces.

---The result of karaoke.split.
---@class hikari.KaraokeSplitResult
---@field lines { line: hikari.LineId, text: string }[] The Lines that change, with their new text; nothing is applied.

---The params of karaoke.time.
---@class hikari.KaraokeTimeParams
---@field document? hikari.DocumentId Defaults to the active Document.
---@field line hikari.LineId
---@field ends integer[] One per syllable, ascending, in ms from the video's start.

---The result of karaoke.time.
---@class hikari.KaraokeTimeResult
---@field line hikari.LineId
---@field text string The Line's text with the \k tags written; nothing is applied.

---The params of karaoke.onsets.
---@class hikari.KaraokeOnsetsParams
---@field start integer
---@field end integer

---The result of karaoke.onsets.
---@class hikari.KaraokeOnsetsResult
---@field onsets integer[]

---The params of karaoke.snap.
---@class hikari.KaraokeSnapParams
---@field document? hikari.DocumentId Defaults to the active Document.
---@field lines hikari.LineId[]
---@field window? integer Ms either side; default 100.

---The result of karaoke.snap.
---@class hikari.KaraokeSnapResult
---@field lines { line: hikari.LineId, text: string, moved?: integer }[] The Lines that change, with their new text; nothing is applied.

---The params of karaoke.kanji.
---@class hikari.KaraokeKanjiParams
---@field document? hikari.DocumentId Defaults to the active Document.
---@field romaji hikari.LineId
---@field kanji hikari.LineId
---@field multi? boolean Further syllables of a group become # syllables instead of joining the first.

---The result of karaoke.kanji.
---@class hikari.KaraokeKanjiResult
---@field line hikari.LineId
---@field text string
---@field clean boolean False when a group came out blank or the characters ran out: check it.

---The params of karaoke.furigana.
---@class hikari.KaraokeFuriganaParams
---@field document? hikari.DocumentId Defaults to the active Document.
---@field line hikari.LineId

---The result of karaoke.furigana.
---@class hikari.KaraokeFuriganaResult
---@field line hikari.LineId
---@field runs { base: string, reading: string }[]

---The params of karaoke.check.
---@class hikari.KaraokeCheckParams
---@field document? hikari.DocumentId Defaults to the active Document.

---The result of karaoke.check.
---@class hikari.KaraokeCheckResult
---@field problems { line: hikari.LineId, kind: "unclosed"|"not_for_audio_editor"|"editor_only"|"total_not_duration"|"zero_length"|"outside_line", totalMs?: integer, durationMs?: integer, syllables?: integer[] }[]
---@field unlisted integer Problems past the listed ones.

---The params of karaoke.template_preview.
---@class hikari.KaraokeTemplatePreviewParams
---@field document? hikari.DocumentId Defaults to the active Document.
---@field line hikari.LineId

---The result of karaoke.template_preview.
---@class hikari.KaraokeTemplatePreviewResult
---@field line hikari.LineId
---@field lines { layer: integer, start: integer, end: integer, style: string, effect: string, text: string }[]

---The params of karaoke.apply_template.
---@class hikari.KaraokeApplyTemplateParams
---@field document? hikari.DocumentId Defaults to the active Document.
---@field revision hikari.Revision
---@field lines? hikari.LineId[] Default: every karaoke Line.
---@field name? string Default "Apply karaoke template".

---The result of karaoke.apply_template.
---@class hikari.KaraokeApplyTemplateResult
---@field revision hikari.Revision
---@field changed boolean

---The params of karaoke.align.
---@class hikari.KaraokeAlignParams
---@field document? hikari.DocumentId Defaults to the active Document.
---@field lines hikari.LineId[]
---@field program "whisperx"|"mfa"
---@field language? string WhisperX: a language code such as ja; default: it detects.
---@field dictionary? string MFA: the pronunciation dictionary, such as english_mfa.
---@field acousticModel? string MFA: the acoustic model, such as english_mfa.

---The result of karaoke.align.
---@class hikari.KaraokeAlignResult
---@field words { text: string, start: integer, end: integer }[]

---The params of marks.set.
---@class hikari.MarksSetParams
---@field document? hikari.DocumentId Defaults to the active Document.
---@field marks { line: hikari.LineId, colour: hikari.MarkColour, tooltip: string }[]

---The result of marks.set.
---@class hikari.MarksSetResult
---@field document hikari.DocumentId
---@field marked integer

---The params of marks.clear.
---@class hikari.MarksClearParams
---@field document? hikari.DocumentId Defaults to the active Document.
---@field lines? hikari.LineId[] These Lines only; every mark of the caller when omitted.

---The result of marks.clear.
---@class hikari.MarksClearResult
---@field document hikari.DocumentId

---The params of marks.list.
---@class hikari.MarksListParams
---@field document? hikari.DocumentId Defaults to the active Document.

---The result of marks.list.
---@class hikari.MarksListResult
---@field document hikari.DocumentId
---@field marks { line: hikari.LineId, colour: hikari.MarkColour, tooltip: string }[]

---The params of programs.run.
---@class hikari.ProgramsRunParams
---@field program string
---@field args? string[] ${file:N} becomes the path of files[N], which the program may then open.
---@field files? string[] Read-only unless the handle is writable.
---@field stdin? string
---@field encoding? "utf8"|"base64" Of stdin, stdout and stderr; utf8 when omitted.
---@field timeout? integer Milliseconds; 60000 when omitted.

---The result of programs.run.
---@class hikari.ProgramsRunResult
---@field exitCode integer
---@field stdout string
---@field stderr string
---@field encoding "utf8"|"base64"

---The params of programs.start.
---@class hikari.ProgramsStartParams
---@field program string
---@field args? string[]
---@field files? string[]
---@field encoding? "utf8"|"base64" Of what program.output carries and programs.send takes.

---The result of programs.start.
---@class hikari.ProgramsStartResult
---@field process hikari.ProgramProcess

---The params of programs.send.
---@class hikari.ProgramsSendParams
---@field process hikari.ProgramProcess
---@field data? string
---@field close? boolean

---The result of programs.send.
---@class hikari.ProgramsSendResult
---@field process hikari.ProgramProcess

---The params of programs.stop.
---@class hikari.ProgramsStopParams
---@field process hikari.ProgramProcess

---The result of programs.stop.
---@class hikari.ProgramsStopResult
---@field process hikari.ProgramProcess

---The window the user works in opened or closed a Project, or the user moved to a window with another one.
---@class hikari.ProjectChangedData
---@field project hikari.Project

---A Document's content changed: a commit, Undo or Redo.
---@class hikari.DocumentChangedData
---@field document hikari.DocumentId
---@field revision hikari.Revision
---@field step string The History step now current.

---A started program wrote to stdout or stderr. Only its plugin hears it, in order and never merged.
---@class hikari.ProgramOutputData
---@field process hikari.ProgramProcess
---@field stream "stdout"|"stderr"
---@field data string
---@field encoding "utf8"|"base64"

---A started program ended. Only its plugin hears it.
---@class hikari.ProgramExitedData
---@field process hikari.ProgramProcess
---@field exitCode integer
---@field stopped boolean true when programs.stop or the plugin's end stopped it.

---A Document was written to a file: Save, Save as, Save all or Save translation. Nothing comes for a save that was cancelled or failed.
---@class hikari.DocumentSavedData
---@field document hikari.DocumentId
---@field path string The file it was written to, as documents.list gives paths.
---@field revision hikari.Revision The revision that was written.

---The selected or the active Line changed.
---@class hikari.SelectionChangedData
---@field document hikari.DocumentId
---@field revision hikari.Revision
---@field active hikari.LineId|hikari.Null
---@field selected hikari.LineId[]

---A Document opened or closed, or another became active.
---@class hikari.DocumentsChangedData
---@field documents hikari.DocumentId[]
---@field editingTarget hikari.DocumentId|hikari.Null

---A panel opened, closed, floated, docked or took the focus.
---@class hikari.PanelChangedData
---@field panel string
---@field title string
---@field open boolean
---@field floating boolean
---@field focused boolean

---A command was triggered, from a menu, a key, the API or anywhere else. It reports the trigger, not the outcome: a Save that opens a dialog or a question comes before anything is written, and even when it is then cancelled; document.saved reports the write.
---@class hikari.CommandRanData
---@field command string
---@field ok boolean False when it was refused or failed.

---An Automation macro run ended, from the menu, a key, Repeat last run or the API, with the answers its dialogs got, in the form automation.run_macro takes.
---@class hikari.MacroRanData
---@field document hikari.DocumentId
---@field file string The script's file name.
---@field macro string
---@field outcome "applied"|"refused"|"failed"|"cancelled"|"declined"|"stopped"|"lost"
---@field ok boolean True when it applied.
---@field dialogs hikari.DialogAnswer[]


hikari = {}

---The Hikari API number.
---@type integer
hikari.api_version = 1

---JSON `null`.
---@type hikari.Null
hikari.null = nil

---Marks a table as a JSON array, so `{}` goes as `[]`.
---@generic T: table
---@param t T
---@return T
function hikari.array(t) end

---A toast and a Log panel entry under the plugin's name.
---@param text string
---@param level? "info"|"warning"|"error"
function hikari.notify(text, level) end

hikari.plugin = {}
---@type string
hikari.plugin.id = ""
---@type string
hikari.plugin.version = ""

hikari.commands = {}
---Gives a command from `contributes.commands` its handler, once.
---@param id string
---@param fn fun(args: any): any
function hikari.commands.register(id, fn) end
---Runs a HikariSub command, as `commands.run` does.
---@param id string
---@return hikari.CommandsRunResult? result
---@return hikari.Error? error
function hikari.commands.run(id) end

hikari.settings = {}
---@param id string
---@return any
function hikari.settings.get(id) end
---Keeps a setting's value; one that doesn't fit the setting raises an error.
---@param id string
---@param value any
function hikari.settings.set(id, value) end

hikari.i18n = {}
---The UI language, such as `"pt_BR"` or `"en"`.
---@type string
hikari.i18n.locale = ""
---The text for `key` from the plugin's `i18n/` files; `%1` to `%9` take the arguments.
---@param key string
---@param ... any
---@return string
function hikari.i18n.t(key, ...) end

hikari.project = {}
---@return hikari.Project? project
---@return hikari.Error? error
function hikari.project.info() end

hikari.document = {}
---@return { document?: hikari.DocumentId, title?: string, path?: string|hikari.Null, format?: "ass"|"srt"|"tmplayer"|"microdvd"|"mpl2"|"text", revision?: hikari.Revision, editingTarget?: boolean, reference?: boolean, dirty?: boolean, readOnly?: boolean }[]? documents
---@return hikari.Error? error
function hikari.document.list() end
---@param document? integer
---@return hikari.Snapshot? snapshot
---@return hikari.Error? error
function hikari.document.read(document) end
---Makes one change, applied as one undo step named "<plugin name>: <name>".
---@param name string
---@param fn fun(doc: hikari.Snapshot, change: hikari.Change)
---@param options? { document?: integer }
---@return { revision: integer, inserted: integer[], changed: boolean }? result
---@return hikari.Error? error
function hikari.document.edit(name, fn, options) end
---Every step `fn` commits becomes one undo step; returns `{steps}`, then what `fn` returned.
---@param name string
---@param fn fun(): ...
---@return { steps: integer }? group
---@return any ...
function hikari.document.group(name, fn) end

hikari.selection = {}
---@param document? integer
---@return hikari.SelectionGetResult? selection
---@return hikari.Error? error
function hikari.selection.get(document) end
---@param selected integer[]
---@param active? integer
---@param document? integer
---@return hikari.SelectionSetResult? result
---@return hikari.Error? error
function hikari.selection.set(selected, active, document) end

hikari.panels = {}
---Draws a panel from `contributes.panels` from a tree of widgets.
---@param panel string
---@param tree table
function hikari.panels.render(panel, tree) end
---@param panel string
function hikari.panels.show(panel) end
---@param panel string
---@param fn fun(event: hikari.UiEvent)
function hikari.panels.on(panel, fn) end
---Sends `data` to a QML panel's `api.received`; false when the panel isn't loaded.
---@param panel string
---@param data any
---@return boolean
function hikari.panels.post(panel, data) end

hikari.dialogs = {}
---@param dialog string
---@param spec { title: string, body: table, buttons: table[], width?: integer, height?: integer }
function hikari.dialogs.show(dialog, spec) end
---Opens a dialog and waits: the button pressed and the values, or nil when it was closed without one.
---@param dialog string
---@param spec { title: string, body: table, buttons: table[], width?: integer, height?: integer }
---@return string? button
---@return table<string, any>? values
function hikari.dialogs.ask(dialog, spec) end
---@param dialog string
function hikari.dialogs.close(dialog) end
---@param dialog string
---@param fn fun(event: hikari.UiEvent)
function hikari.dialogs.on(dialog, fn) end

hikari.files = {}
---@param place string `"data"`, `"cache"`, `"project"` or a folder id
---@param path string
---@return hikari.File? file
---@return hikari.Error? error
function hikari.files.open(place, path) end
---@param place string
---@param path string
---@return { name: string, folder: boolean }[]? entries
---@return hikari.Error? error
function hikari.files.entries(place, path) end
---@param options? { title?: string, save?: boolean, name?: string, filters?: { name: string, patterns: string[] }[] }
---@return hikari.File? file
---@return hikari.Error? error
function hikari.files.pick(options) end
---Asks the user for the folder the first time; whether the plugin has it.
---@param id string
---@return boolean? granted
---@return hikari.Error? error
function hikari.files.folder(id) end
---@return hikari.GrantedFile[]? files
---@return hikari.Error? error
function hikari.files.list() end
---@param file string
---@param encoding? "text"|"base64"
---@return string? data
---@return hikari.Error? error
function hikari.files.read(file, encoding) end
---@param file string
---@param data string
---@param encoding? "text"|"base64"
---@return true? written
---@return hikari.Error? error
function hikari.files.write(file, data, encoding) end

hikari.net = {}
---An HTTPS request HikariSub makes to a host from the manifest.
---@param request string|{ url: string, method?: "GET"|"HEAD"|"POST"|"PUT"|"PATCH"|"DELETE", headers?: table<string, string>, body?: string, encoding?: "text"|"base64" }
---@return { status: integer, headers: table<string, string>, body: string }? response
---@return hikari.Error? error
function hikari.net.fetch(request) end

hikari.job = {}
---@param options { title: string }
---@return true? begun
---@return hikari.Error? error
function hikari.job.begin(options) end
---@param fraction number? nil for a bar without a fraction
---@param text? string
---@return true? shown
---@return hikari.Error? error
function hikari.job.progress(fraction, text) end
---@return boolean
function hikari.job.cancelled() end

hikari.timer = {}
---@param ms integer
---@param fn fun()
---@return integer? id
---@return hikari.Error? error
function hikari.timer.after(ms, fn) end
---@param ms integer
---@param fn fun()
---@return integer? id
---@return hikari.Error? error
function hikari.timer.every(ms, fn) end
---@param id integer
---@return true? cancelled
---@return hikari.Error? error
function hikari.timer.cancel(id) end

hikari.marks = {}
---@param marks { line: integer, colour: "accent"|"warning"|"danger"|"success"|"muted", tooltip?: string }[]
---@param document? integer
---@return hikari.MarksSetResult? result
---@return hikari.Error? error
function hikari.marks.set(marks, document) end
---@param lines? integer[]
---@param document? integer
---@return hikari.MarksClearResult? result
---@return hikari.Error? error
function hikari.marks.clear(lines, document) end
---@param document? integer
---@return { line: hikari.LineId, colour: hikari.MarkColour, tooltip: string }[]? marks
---@return hikari.Error? error
function hikari.marks.list(document) end

hikari.vcs = {}
---@param options? hikari.VcsRefsParams
---@return hikari.VcsRefsResult? refs
---@return hikari.Error? error
function hikari.vcs.refs(options) end
---@param ref string
---@param document? integer
---@return string? text
---@return hikari.VcsShowResult|hikari.Error shown
function hikari.vcs.show(ref, document) end

hikari.programs = {}
---Runs a program the manifest declares and waits for it to end.
---@param program string
---@param options? { args?: string[], files?: (hikari.File|string)[], stdin?: string, encoding?: "text"|"base64", timeout?: integer }
---@return hikari.ProgramsRunResult? result
---@return hikari.Error? error
function hikari.programs.run(program, options) end
---Starts a program the manifest declares: its process.
---@param program string
---@param options? { args?: string[], files?: (hikari.File|string)[], encoding?: "text"|"base64" }
---@return string? process
---@return hikari.Error? error
function hikari.programs.start(program, options) end
---@param process string
---@param data? string
---@param close? boolean
---@return true? sent
---@return hikari.Error? error
function hikari.programs.send(process, data, close) end
---@param process string
---@return true? stopped
---@return hikari.Error? error
function hikari.programs.stop(process) end

---Calls a Hikari API operation as the plugin: the result, or nil and the error.
---@overload fun(operation: "documents.list", params?: hikari.DocumentsListParams): hikari.DocumentsListResult?, hikari.Error?
---@overload fun(operation: "documents.get", params?: hikari.DocumentsGetParams): hikari.DocumentsGetResult?, hikari.Error?
---@overload fun(operation: "lines.list", params?: hikari.LinesListParams): hikari.LinesListResult?, hikari.Error?
---@overload fun(operation: "lines.get", params: hikari.LinesGetParams): hikari.LinesGetResult?, hikari.Error?
---@overload fun(operation: "lines.find", params?: hikari.LinesFindParams): hikari.LinesFindResult?, hikari.Error?
---@overload fun(operation: "styles.list", params?: hikari.StylesListParams): hikari.StylesListResult?, hikari.Error?
---@overload fun(operation: "info.list", params?: hikari.InfoListParams): hikari.InfoListResult?, hikari.Error?
---@overload fun(operation: "selection.get", params?: hikari.SelectionGetParams): hikari.SelectionGetResult?, hikari.Error?
---@overload fun(operation: "api.version", params?: hikari.ApiVersionParams): hikari.ApiVersionResult?, hikari.Error?
---@overload fun(operation: "commands.list", params?: hikari.CommandsListParams): hikari.CommandsListResult?, hikari.Error?
---@overload fun(operation: "panels.list", params?: hikari.PanelsListParams): hikari.PanelsListResult?, hikari.Error?
---@overload fun(operation: "settings.list", params?: hikari.SettingsListParams): hikari.SettingsListResult?, hikari.Error?
---@overload fun(operation: "settings.get", params: hikari.SettingsGetParams): hikari.SettingsGetResult?, hikari.Error?
---@overload fun(operation: "commands.run", params: hikari.CommandsRunParams): hikari.CommandsRunResult?, hikari.Error?
---@overload fun(operation: "panels.open", params: hikari.PanelsOpenParams): hikari.PanelsOpenResult?, hikari.Error?
---@overload fun(operation: "panels.close", params: hikari.PanelsCloseParams): hikari.PanelsCloseResult?, hikari.Error?
---@overload fun(operation: "panels.focus", params: hikari.PanelsFocusParams): hikari.PanelsFocusResult?, hikari.Error?
---@overload fun(operation: "settings.set", params: hikari.SettingsSetParams): hikari.SettingsSetResult?, hikari.Error?
---@overload fun(operation: "document.apply", params: hikari.DocumentApplyParams): hikari.DocumentApplyResult?, hikari.Error?
---@overload fun(operation: "selection.set", params: hikari.SelectionSetParams): hikari.SelectionSetResult?, hikari.Error?
---@overload fun(operation: "document.group_begin", params: hikari.DocumentGroupBeginParams): hikari.DocumentGroupBeginResult?, hikari.Error?
---@overload fun(operation: "document.group_end", params?: hikari.DocumentGroupEndParams): hikari.DocumentGroupEndResult?, hikari.Error?
---@overload fun(operation: "video.info", params?: hikari.VideoInfoParams): hikari.VideoInfoResult?, hikari.Error?
---@overload fun(operation: "video.frame_at_time", params: hikari.VideoFrameAtTimeParams): hikari.VideoFrameAtTimeResult?, hikari.Error?
---@overload fun(operation: "video.time_of_frame", params: hikari.VideoTimeOfFrameParams): hikari.VideoTimeOfFrameResult?, hikari.Error?
---@overload fun(operation: "video.frame", params?: hikari.VideoFrameParams): hikari.VideoFrameResult?, hikari.Error?
---@overload fun(operation: "video.ink_bounds", params?: hikari.VideoInkBoundsParams): hikari.VideoInkBoundsResult?, hikari.Error?
---@overload fun(operation: "video.colours", params: hikari.VideoColoursParams): hikari.VideoColoursResult?, hikari.Error?
---@overload fun(operation: "video.keyframes", params?: hikari.VideoKeyframesParams): hikari.VideoKeyframesResult?, hikari.Error?
---@overload fun(operation: "video.frames", params: hikari.VideoFramesParams): hikari.VideoFramesResult?, hikari.Error?
---@overload fun(operation: "video.set_keyframes", params: hikari.VideoSetKeyframesParams): hikari.VideoSetKeyframesResult?, hikari.Error?
---@overload fun(operation: "audio.info", params?: hikari.AudioInfoParams): hikari.AudioInfoResult?, hikari.Error?
---@overload fun(operation: "audio.peaks", params: hikari.AudioPeaksParams): hikari.AudioPeaksResult?, hikari.Error?
---@overload fun(operation: "files.list", params?: hikari.FilesListParams): hikari.FilesListResult?, hikari.Error?
---@overload fun(operation: "files.read", params?: hikari.FilesReadParams): hikari.FilesReadResult?, hikari.Error?
---@overload fun(operation: "files.write", params: hikari.FilesWriteParams): hikari.FilesWriteResult?, hikari.Error?
---@overload fun(operation: "skills.list", params?: hikari.SkillsListParams): hikari.SkillsListResult?, hikari.Error?
---@overload fun(operation: "skills.read", params: hikari.SkillsReadParams): hikari.SkillsReadResult?, hikari.Error?
---@overload fun(operation: "automation.macros", params?: hikari.AutomationMacrosParams): hikari.AutomationMacrosResult?, hikari.Error?
---@overload fun(operation: "automation.run_macro", params: hikari.AutomationRunMacroParams): hikari.AutomationRunMacroResult?, hikari.Error?
---@overload fun(operation: "automation.run", params: hikari.AutomationRunParams): hikari.AutomationRunResult?, hikari.Error?
---@overload fun(operation: "translation.project", params?: hikari.TranslationProjectParams): hikari.TranslationProjectResult?, hikari.Error?
---@overload fun(operation: "translation.project_write", params: hikari.TranslationProjectWriteParams): hikari.TranslationProjectWriteResult?, hikari.Error?
---@overload fun(operation: "nodes.catalogue", params?: hikari.NodesCatalogueParams): hikari.NodesCatalogueResult?, hikari.Error?
---@overload fun(operation: "nodes.validate", params: hikari.NodesValidateParams): hikari.NodesValidateResult?, hikari.Error?
---@overload fun(operation: "effects.graphs", params?: hikari.EffectsGraphsParams): hikari.EffectsGraphsResult?, hikari.Error?
---@overload fun(operation: "effects.graph", params: hikari.EffectsGraphParams): hikari.EffectsGraphResult?, hikari.Error?
---@overload fun(operation: "effects.edit", params: hikari.EffectsEditParams): hikari.EffectsEditResult?, hikari.Error?
---@overload fun(operation: "effects.preview", params?: hikari.EffectsPreviewParams): hikari.EffectsPreviewResult?, hikari.Error?
---@overload fun(operation: "effects.save", params: hikari.EffectsSaveParams): hikari.EffectsSaveResult?, hikari.Error?
---@overload fun(operation: "effects.apply", params: hikari.EffectsApplyParams): hikari.EffectsApplyResult?, hikari.Error?
---@overload fun(operation: "plugins.list", params?: hikari.PluginsListParams): hikari.PluginsListResult?, hikari.Error?
---@overload fun(operation: "plugins.manifest", params: hikari.PluginsManifestParams): hikari.PluginsManifestResult?, hikari.Error?
---@overload fun(operation: "plugins.scaffold", params: hikari.PluginsScaffoldParams): hikari.PluginsScaffoldResult?, hikari.Error?
---@overload fun(operation: "plugins.review", params?: hikari.PluginsReviewParams): hikari.PluginsReviewResult?, hikari.Error?
---@overload fun(operation: "plugins.install", params?: hikari.PluginsInstallParams): hikari.PluginsInstallResult?, hikari.Error?
---@overload fun(operation: "plugins.run", params: hikari.PluginsRunParams): hikari.PluginsRunResult?, hikari.Error?
---@overload fun(operation: "typeset.perspective", params: hikari.TypesetPerspectiveParams): hikari.TypesetPerspectiveResult?, hikari.Error?
---@overload fun(operation: "typeset.clip", params: hikari.TypesetClipParams): hikari.TypesetClipResult?, hikari.Error?
---@overload fun(operation: "typeset.drawing", params: hikari.TypesetDrawingParams): hikari.TypesetDrawingResult?, hikari.Error?
---@overload fun(operation: "typeset.shift", params: hikari.TypesetShiftParams): hikari.TypesetShiftResult?, hikari.Error?
---@overload fun(operation: "typeset.hydra", params: hikari.TypesetHydraParams): hikari.TypesetHydraResult?, hikari.Error?
---@overload fun(operation: "typeset.check", params: hikari.TypesetCheckParams): hikari.TypesetCheckResult?, hikari.Error?
---@overload fun(operation: "typeset.apply", params: hikari.TypesetApplyParams): hikari.TypesetApplyResult?, hikari.Error?
---@overload fun(operation: "project.info", params?: hikari.ProjectInfoParams): hikari.ProjectInfoResult?, hikari.Error?
---@overload fun(operation: "vcs.refs", params?: hikari.VcsRefsParams): hikari.VcsRefsResult?, hikari.Error?
---@overload fun(operation: "vcs.show", params: hikari.VcsShowParams): hikari.VcsShowResult?, hikari.Error?
---@overload fun(operation: "karaoke.syllables", params: hikari.KaraokeSyllablesParams): hikari.KaraokeSyllablesResult?, hikari.Error?
---@overload fun(operation: "karaoke.split", params: hikari.KaraokeSplitParams): hikari.KaraokeSplitResult?, hikari.Error?
---@overload fun(operation: "karaoke.time", params: hikari.KaraokeTimeParams): hikari.KaraokeTimeResult?, hikari.Error?
---@overload fun(operation: "karaoke.onsets", params: hikari.KaraokeOnsetsParams): hikari.KaraokeOnsetsResult?, hikari.Error?
---@overload fun(operation: "karaoke.snap", params: hikari.KaraokeSnapParams): hikari.KaraokeSnapResult?, hikari.Error?
---@overload fun(operation: "karaoke.kanji", params: hikari.KaraokeKanjiParams): hikari.KaraokeKanjiResult?, hikari.Error?
---@overload fun(operation: "karaoke.furigana", params: hikari.KaraokeFuriganaParams): hikari.KaraokeFuriganaResult?, hikari.Error?
---@overload fun(operation: "karaoke.check", params?: hikari.KaraokeCheckParams): hikari.KaraokeCheckResult?, hikari.Error?
---@overload fun(operation: "karaoke.template_preview", params: hikari.KaraokeTemplatePreviewParams): hikari.KaraokeTemplatePreviewResult?, hikari.Error?
---@overload fun(operation: "karaoke.apply_template", params: hikari.KaraokeApplyTemplateParams): hikari.KaraokeApplyTemplateResult?, hikari.Error?
---@overload fun(operation: "karaoke.align", params: hikari.KaraokeAlignParams): hikari.KaraokeAlignResult?, hikari.Error?
---@overload fun(operation: "marks.set", params: hikari.MarksSetParams): hikari.MarksSetResult?, hikari.Error?
---@overload fun(operation: "marks.clear", params?: hikari.MarksClearParams): hikari.MarksClearResult?, hikari.Error?
---@overload fun(operation: "marks.list", params?: hikari.MarksListParams): hikari.MarksListResult?, hikari.Error?
---@overload fun(operation: "programs.run", params: hikari.ProgramsRunParams): hikari.ProgramsRunResult?, hikari.Error?
---@overload fun(operation: "programs.start", params: hikari.ProgramsStartParams): hikari.ProgramsStartResult?, hikari.Error?
---@overload fun(operation: "programs.send", params: hikari.ProgramsSendParams): hikari.ProgramsSendResult?, hikari.Error?
---@overload fun(operation: "programs.stop", params: hikari.ProgramsStopParams): hikari.ProgramsStopResult?, hikari.Error?
---@param operation string
---@param params? table
---@return any result
---@return hikari.Error? error
function hikari.call(operation, params) end

hikari.events = {}
---Calls `fn(data)` for each Hikari API event of that name.
---@overload fun(name: "project.changed", fn: fun(data: hikari.ProjectChangedData))
---@overload fun(name: "document.changed", fn: fun(data: hikari.DocumentChangedData))
---@overload fun(name: "program.output", fn: fun(data: hikari.ProgramOutputData))
---@overload fun(name: "program.exited", fn: fun(data: hikari.ProgramExitedData))
---@overload fun(name: "document.saved", fn: fun(data: hikari.DocumentSavedData))
---@overload fun(name: "selection.changed", fn: fun(data: hikari.SelectionChangedData))
---@overload fun(name: "documents.changed", fn: fun(data: hikari.DocumentsChangedData))
---@overload fun(name: "panel.changed", fn: fun(data: hikari.PanelChangedData))
---@overload fun(name: "command.ran", fn: fun(data: hikari.CommandRanData))
---@overload fun(name: "macro.ran", fn: fun(data: hikari.MacroRanData))
---@param name string
---@param fn fun(data: any)
function hikari.events.on(name, fn) end
