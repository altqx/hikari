// TypeScript types for HikariSub program plugins, generated from api.json by `hikarisub --export-docs`.
// Don't edit this file.

/** JSON, as the Hikari API takes and gives it. */
export type Json = null | boolean | number | string | Json[] | { [key: string]: Json };

/** An error from the Hikari API. */
export interface ApiError {
    code: string;
    message: string;
    data?: Json;
}

/** An open Document, as documents.list gives it. */
export type DocumentId = number;

/** A skill's name, which is its folder's. */
export type SkillName = string;

/** A Line's stable id: it survives edits, moves and undo, unlike its row. */
export type LineId = number;

/** Counts the Document's committed changes; a write names the revision it was prepared against. */
export type Revision = number;

export type LineField = "comment" | "layer" | "start" | "end" | "style" | "actor" | "marginL" | "marginR" | "marginV" | "effect" | "text" | "translation" | "unconfirmed" | "generatedFrom";

/** Times are milliseconds. In translation mode text is the original and translation the translated text. */
export interface Line {
    id: LineId;
    /** 1-based position among the Document's Lines. */
    row: number;
    comment?: boolean;
    layer?: number;
    start?: number;
    end?: number;
    style?: string;
    actor?: string;
    marginL?: number;
    marginR?: number;
    marginV?: number;
    effect?: string;
    text?: string;
    translation?: string;
    unconfirmed?: boolean;
    /** The karaoke Line a templater run generated this one from. */
    generatedFrom?: LineId | null;
}

/** An ASS v4+ Style. Text fields hold the field's ASS text; colours are &HAABBGGRR. */
export interface Style {
    name: string;
    fontname?: string;
    fontsize?: string;
    primaryColour?: string;
    secondaryColour?: string;
    outlineColour?: string;
    backColour?: string;
    bold?: boolean;
    italic?: boolean;
    underline?: boolean;
    strikeOut?: boolean;
    scaleX?: string;
    scaleY?: string;
    spacing?: string;
    angle?: string;
    borderStyle?: 1 | 3 | 4;
    outline?: string;
    shadow?: string;
    alignment?: string;
    marginL?: string;
    marginR?: string;
    marginV?: string;
    encoding?: string;
}

/** Line fields to set; the others keep their values. */
export interface LineFields {
    comment?: boolean;
    layer?: number;
    start?: number;
    end?: number;
    style?: string;
    actor?: string;
    marginL?: number;
    marginR?: number;
    marginV?: number;
    effect?: string;
    text?: string;
    translation?: string;
    unconfirmed?: boolean;
}

/** Style fields to set; name renames it. A new Style starts from Arial 48. */
export interface StyleFields {
    name?: string;
    fontname?: string;
    fontsize?: string;
    primaryColour?: string;
    secondaryColour?: string;
    outlineColour?: string;
    backColour?: string;
    bold?: boolean;
    italic?: boolean;
    underline?: boolean;
    strikeOut?: boolean;
    scaleX?: string;
    scaleY?: string;
    spacing?: string;
    angle?: string;
    borderStyle?: 1 | 3 | 4;
    outline?: string;
    shadow?: string;
    alignment?: string;
    marginL?: string;
    marginR?: string;
    marginV?: string;
    encoding?: string;
}

export type Edit = {
    op: "set_line";
    line: LineId;
    fields: LineFields;
} | {
    op: "insert_line";
    after?: LineId;
    before?: LineId;
    fields?: LineFields;
} | {
    op: "delete_line";
    line: LineId;
} | {
    op: "move_line";
    line: LineId;
    before: LineId | null;
} | {
    op: "set_style";
    name: string;
    fields?: StyleFields;
} | {
    op: "delete_style";
    name: string;
} | {
    op: "set_info";
    key: string;
    value: string | null;
};

export type SettingValue = boolean | number | number | string | string[];

export interface NodePort {
    name: string;
    type: string;
}

export interface GrantedFile {
    /** The handle files.read and files.write take. */
    file: string;
    name: string;
    access: "read" | "write";
}

export interface GrantedFolder {
    /** The handle files.list, files.read and files.write take with a path. */
    folder: string;
    name: string;
    access: "read" | "write";
}

export interface FolderEntry {
    /** Relative to the granted folder, with / between its parts. */
    path: string;
    folder: boolean;
}

/** The Project of the window the user works in; null without one. */
export type Project = null | {
    /** The Project folder's name. */
    name: string;
    /** Whether the folder is in Trusted folders. */
    trusted: boolean;
};

export interface DialogControl {
    name: string;
    /** The control's Lua class, lowercased. */
    kind: "edit" | "textbox" | "intedit" | "floatedit" | "dropdown" | "checkbox" | "color" | "coloralpha" | "alpha";
    /** A checkbox's text. */
    label?: string;
    hint?: string;
    /** A dropdown's choices. */
    items?: string[];
    min?: number;
    max?: number;
    /** A floatedit's step; 0 for none. */
    step?: number;
}

export interface DialogAnswer {
    /** The dialog's key: its script and its controls' kinds and names, as Automation remembers answers by. When given, the key alone decides which dialog the answer fills. */
    key?: string;
    /** The dialog's value-bearing controls, in order. Without a key, the answer fills the dialog of the script being run whose controls have these kinds and names; the other fields don't count. */
    controls?: DialogControl[];
    /** The answered values by control name. */
    values: { [key: string]: string | number | boolean };
    /** The pressed button's label; empty when the dialog was closed. */
    button?: string;
}

export interface Problem {
    /** Given by problems.list; problems.set ignores it. */
    id?: string;
    /** Absent only for a QC note that matches no Line. */
    line?: LineId;
    /** A QC note's time in milliseconds; problems.set ignores it. */
    time?: number;
    /** text when omitted. */
    field?: "text" | "translation";
    /** UTF-16 offset in the field; with to, the squiggle. The whole Line when omitted. */
    from?: number;
    to?: number;
    severity: "error" | "warning" | "info" | "hint";
    message: string;
    /** The check's own name, such as a grammar rule. */
    code?: string;
    /** Given by problems.list. */
    source?: {
        kind: "qc" | "plugin" | "server";
        /** qc, a plugin id, or plugin:<plugin id>/<server id>. */
        id: string;
        /** The plugin's own source name from problems.set, or a QC note's section. */
        set?: string;
        /** What the problems list shows. */
        name: string;
    };
    fixes?: {
        title: string;
        /** Replacements in this Line's field, by UTF-16 offsets in the text the problem was set against. */
        edits: {
            from: number;
            to: number;
            insert: string;
        }[];
    }[];
}

/** A theme role, so the mark follows the theme and high contrast. */
export type MarkColour = "accent" | "warning" | "danger" | "success" | "muted";

/** A started program, such as p1. */
export type ProgramProcess = string;

/** The params of `documents.list`: The open Documents, in tab order, with the protected reference last. */
export type DocumentsListParams = Record<string, never>;

/** The result of `documents.list`. */
export interface DocumentsListResult {
    documents: {
        document?: DocumentId;
        title?: string;
        path?: string | null;
        /** A formats.list id: ass, srt, tmplayer, microdvd, mpl2, text or plugin:<plugin id>/<format id>. */
        format?: string;
        revision?: Revision;
        editingTarget?: boolean;
        /** The protected comparison reference. */
        reference?: boolean;
        dirty?: boolean;
        /** A macro holds it, or it is the protected reference. */
        readOnly?: boolean;
    }[];
}

/** The params of `documents.get`: One Document's summary, Line and Style counts and its pending Line editor draft. */
export interface DocumentsGetParams {
    /** Defaults to the active Document. */
    document?: DocumentId;
}

/** The result of `documents.get`. */
export interface DocumentsGetResult {
    document?: DocumentId;
    title?: string;
    path?: string | null;
    /** A formats.list id: ass, srt, tmplayer, microdvd, mpl2, text or plugin:<plugin id>/<format id>. */
    format?: string;
    revision?: Revision;
    editingTarget?: boolean;
    /** The protected comparison reference. */
    reference?: boolean;
    dirty?: boolean;
    /** A macro holds it, or it is the protected reference. */
    readOnly?: boolean;
    lineCount?: number;
    styleCount?: number;
    /** The Line with uncommitted Line editor changes; a write commits them first. */
    draftLine?: LineId | null;
}

/** The params of `lines.list`: Lines in Document order, a page at a time. */
export interface LinesListParams {
    /** Defaults to the active Document. */
    document?: DocumentId;
    offset?: number;
    /** 1000 when omitted. */
    limit?: number;
    /** Only these fields besides id and row; all of them when omitted. */
    fields?: LineField[];
}

/** The result of `lines.list`. */
export interface LinesListResult {
    revision: Revision;
    total?: number;
    lines: Line[];
}

/** The params of `lines.get`: Lines by id, in the order asked. */
export interface LinesGetParams {
    /** Defaults to the active Document. */
    document?: DocumentId;
    ids: LineId[];
    /** Only these fields besides id and row; all of them when omitted. */
    fields?: LineField[];
}

/** The result of `lines.get`. */
export interface LinesGetResult {
    revision: Revision;
    total?: number;
    lines: Line[];
}

/** The params of `lines.find`: Lines matching every criterion given: text or regex in one field, exact Style, Actor or Effect, Comment, and a time window. */
export interface LinesFindParams {
    /** Defaults to the active Document. */
    document?: DocumentId;
    /** A case-sensitive substring. */
    text?: string;
    /** A regular expression, as Find and replace takes it. */
    regex?: string;
    /** For regex. */
    ignoreCase?: boolean;
    /** Where text and regex look; text when omitted. */
    in?: "text" | "translation" | "actor" | "effect" | "style";
    style?: string;
    actor?: string;
    effect?: string;
    comment?: boolean;
    /** Lines ending after this time, in milliseconds. */
    from?: number;
    /** Lines starting before this time, in milliseconds. */
    to?: number;
    /** 500 when omitted; total still counts every match. */
    limit?: number;
    /** Only these fields besides id and row; all of them when omitted. */
    fields?: LineField[];
}

/** The result of `lines.find`. */
export interface LinesFindResult {
    revision: Revision;
    total?: number;
    lines: Line[];
}

/** The params of `styles.list`: The Document's Styles, in order. */
export interface StylesListParams {
    /** Defaults to the active Document. */
    document?: DocumentId;
}

/** The result of `styles.list`. */
export interface StylesListResult {
    revision: Revision;
    styles: Style[];
}

/** The params of `info.list`: The Document's Script Info properties, in order. */
export interface InfoListParams {
    /** Defaults to the active Document. */
    document?: DocumentId;
}

/** The result of `info.list`. */
export interface InfoListResult {
    revision: Revision;
    info: {
        key: string;
        value: string;
    }[];
}

/** The params of `selection.get`: The selected Lines, in Document order, and the active Line. */
export interface SelectionGetParams {
    /** Defaults to the active Document. */
    document?: DocumentId;
}

/** The result of `selection.get`. */
export interface SelectionGetResult {
    document: DocumentId;
    revision: Revision;
    active: LineId | null;
    selected: LineId[];
}

/** The params of `api.version`: The API version: it grows by one with each release that adds to the API. */
export type ApiVersionParams = Record<string, never>;

/** The result of `api.version`. */
export interface ApiVersionResult {
    version: number;
}

/** The params of `commands.list`: The commands of menus, toolbars and shortcuts, with their enabled state and keys. */
export interface CommandsListParams {
    window?: "global" | "subtitles" | "editor" | "video" | "audio";
    /** A case-insensitive part of the command id or its name. */
    query?: string;
}

/** The result of `commands.list`. */
export interface CommandsListResult {
    commands: {
        /** The stable command id, e.g. GLOBAL_SAVE_SUBS. */
        command: string;
        /** The English name the shortcut editor shows. */
        name: string;
        window: "global" | "subtitles" | "editor" | "video" | "audio";
        enabled: boolean;
        disabledReason?: string;
        keys: string[];
        /** Always given: a HikariSub command, an Automation macro (macro:<file>:<ordinal>) or a plugin's (plugin.<id>.<command>). */
        kind?: "hotkey" | "macro" | "plugin";
        /** A macro's script file name. */
        file?: string;
        /** A macro's name. */
        macro?: string;
    }[];
}

/** The params of `panels.list`: The workspace panels and whether each is open, floating or focused. */
export type PanelsListParams = Record<string, never>;

/** The result of `panels.list`. */
export interface PanelsListResult {
    panels: {
        panel: string;
        title: string;
        open: boolean;
        floating: boolean;
        focused: boolean;
    }[];
}

/** The params of `settings.list`: Settings with their type, scope, default and current value, plugins' settings as plugin.<plugin id>.<setting id>. */
export interface SettingsListParams {
    /** Only ids starting with it, e.g. grid. */
    prefix?: string;
}

/** The result of `settings.list`. */
export interface SettingsListResult {
    settings: {
        setting: string;
        type: "boolean" | "integer" | "number" | "string" | "stringList";
        scope: "profile" | "collections" | "workspace";
        default: SettingValue;
        value: SettingValue;
        /** The name the Settings window shows, in the UI language. */
        title?: string;
        /** app, or the id of the plugin whose setting it is. */
        source?: string;
        /** Whether a Project can override it on the Project tab. */
        projectable?: boolean;
    }[];
}

/** The params of `settings.get`: The current values of the settings named. */
export interface SettingsGetParams {
    settings: string[];
}

/** The result of `settings.get`. */
export interface SettingsGetResult {
    values: { [key: string]: SettingValue };
}

/** The params of `commands.run`: Runs an enabled command as its menu item, toolbar button or shortcut would. */
export interface CommandsRunParams {
    /** A command id from commands.list. */
    command: string;
}

/** The result of `commands.run`. */
export interface CommandsRunResult {
    command: string;
    ran: true;
}

/** The params of `panels.open`: Opens a panel where the layout keeps it. */
export interface PanelsOpenParams {
    /** A panel id from panels.list. */
    panel: string;
}

/** The result of `panels.open`. */
export interface PanelsOpenResult {
    panel: string;
    title: string;
    open: boolean;
    floating: boolean;
    focused: boolean;
}

/** The params of `panels.close`: Closes a panel. */
export interface PanelsCloseParams {
    /** A panel id from panels.list. */
    panel: string;
}

/** The result of `panels.close`. */
export interface PanelsCloseResult {
    panel: string;
    title: string;
    open: boolean;
    floating: boolean;
    focused: boolean;
}

/** The params of `panels.focus`: Opens a panel if needed and gives it the keyboard focus. */
export interface PanelsFocusParams {
    /** A panel id from panels.list. */
    panel: string;
}

/** The result of `panels.focus`. */
export interface PanelsFocusResult {
    panel: string;
    title: string;
    open: boolean;
    floating: boolean;
    focused: boolean;
}

/** The params of `settings.open`: Opens the Settings window at a setting, on the Yours or Project tab; it changes no setting. */
export interface SettingsOpenParams {
    /** A setting id from settings.list, such as appearance.theme or plugin.<plugin id>.<setting id>. */
    setting?: string;
    /** The tab; yours when omitted. */
    scope?: "yours" | "project";
}

/** The result of `settings.open`. */
export interface SettingsOpenResult {
    opened: true;
}

/** The params of `settings.set`: Changes a setting; the value must have the setting's type. */
export interface SettingsSetParams {
    setting: string;
    value: SettingValue;
}

/** The result of `settings.set`. */
export interface SettingsSetResult {
    setting: string;
    value: SettingValue;
}

/** The params of `document.apply`: Applies edits prepared against a revision as one undo step; a stale revision changes nothing. */
export interface DocumentApplyParams {
    /** Defaults to the active Document. */
    document?: DocumentId;
    /** The revision the edits were prepared against. */
    revision: Revision;
    /** A few words naming the change, as the user reads it in the review and in History after the caller's name. */
    name: string;
    /** Applied in order; any refusal leaves the Document as it was. */
    edits: Edit[];
}

/** The result of `document.apply`. */
export interface DocumentApplyResult {
    revision: Revision;
    /** New Lines, in edit order. */
    inserted: LineId[];
    /** False when the edits changed nothing: no undo step was added. */
    changed: boolean;
}

/** The params of `selection.set`: Selects Lines and makes one active, as a click in the Grid would; never an undo step. */
export interface SelectionSetParams {
    /** Defaults to the active Document. */
    document?: DocumentId;
    selected: LineId[];
    /** The first selected Line when omitted. */
    active?: LineId | null;
}

/** The result of `selection.set`. */
export interface SelectionSetResult {
    document: DocumentId;
    revision: Revision;
    active: LineId | null;
    selected: LineId[];
}

/** The params of `document.group_begin`: Groups the steps the caller commits on the editing target from now on, by any write, into one undo step named after the caller and `name`. The group ends at document.group_end, when the plugin's command or event handler returns, when the plugin stops, or at the first step someone else makes. Plugins only. */
export interface DocumentGroupBeginParams {
    /** A few words naming the grouped step, as the user reads it in History after the caller's name. */
    name: string;
}

/** The result of `document.group_begin`. */
export interface DocumentGroupBeginResult {
    document: DocumentId;
}

/** The params of `document.group_end`: Ends the caller's group: its steps become one undo step. */
export type DocumentGroupEndParams = Record<string, never>;

/** The result of `document.group_end`. */
export interface DocumentGroupEndResult {
    /** How many steps became one; 0 when the group made none. A group someone else's step ended earlier is no longer open. */
    steps: number;
}

/** The params of `video.info`: Whether a video is open, its size and aspect ratio, the frame shown and the keyframes. */
export type VideoInfoParams = Record<string, never>;

/** The result of `video.info`. */
export interface VideoInfoResult {
    open: boolean;
    width?: number;
    height?: number;
    aspect?: number[];
    frameShown?: number;
    keyframes?: number[];
}

/** The params of `video.frame_at_time`: The video frame shown at a time. */
export interface VideoFrameAtTimeParams {
    /** Milliseconds. */
    time: number;
}

/** The result of `video.frame_at_time`. */
export interface VideoFrameAtTimeResult {
    frame: number;
}

/** The params of `video.time_of_frame`: The time a video frame starts, in milliseconds. */
export interface VideoTimeOfFrameParams {
    frame: number;
}

/** The result of `video.time_of_frame`. */
export interface VideoTimeOfFrameResult {
    time: number;
}

/** The params of `video.frame`: One video frame as BGRA pixels in base64, with every Line, one Line or none drawn on it, optionally cropped, scaled to fit maxEdge and maxPixels. */
export interface VideoFrameParams {
    /** Default: the frame shown, or with line the Line's first frame when it is not on the frame shown. */
    frame?: number;
    /** Render the subtitles on it. Default true. */
    subtitles?: boolean;
    /** line's Document. Defaults to the active Document. */
    document?: DocumentId;
    /** Draw this Line alone. */
    line?: LineId;
    crop?: {
        x: number;
        y: number;
        width: number;
        height: number;
    };
    /** Default 1024. */
    maxEdge?: number;
    /** The most pixels the image may have. No limit when omitted. */
    maxPixels?: number;
}

/** The result of `video.frame`. */
export interface VideoFrameResult {
    frame: number;
    width: number;
    height: number;
    crop: {
        x: number;
        y: number;
        width: number;
        height: number;
    };
    format: "bgra";
    /** width * 4 bytes per row, top row first, base64. */
    data: string;
}

/** The params of `video.ink_bounds`: Where one Line's ink, or every Line's, lies on a video frame, in video pixels. */
export interface VideoInkBoundsParams {
    /** Default: the frame shown, or with line the Line's first frame when it is not on the frame shown. */
    frame?: number;
    /** Defaults to the active Document. */
    document?: DocumentId;
    /** This Line alone. Every Line when omitted. */
    line?: LineId;
}

/** The result of `video.ink_bounds`. */
export interface VideoInkBoundsResult {
    frame: number;
    /** Null when nothing is drawn. */
    bounds: {
        x: number;
        y: number;
        width: number;
        height: number;
    } | null;
}

/** The params of `video.colours`: The colours of a video frame at points, as ASS &HBBGGRR&, each the mean of the square radius pixels around it. */
export interface VideoColoursParams {
    /** Default: the frame shown. */
    frame?: number;
    points: {
        x: number;
        y: number;
    }[];
    /** Default 0, the pixel alone. */
    radius?: number;
    /** Sample with the subtitles drawn. Default false. */
    subtitles?: boolean;
}

/** The result of `video.colours`. */
export interface VideoColoursResult {
    frame: number;
    colours: {
        x: number;
        y: number;
        colour: string;
    }[];
}

/** The params of `video.keyframes`: The keyframes from one time to another, each with its start time. */
export interface VideoKeyframesParams {
    /** Milliseconds. Default 0. */
    from?: number;
    /** Milliseconds. Default: the end. */
    to?: number;
}

/** The result of `video.keyframes`. */
export interface VideoKeyframesResult {
    keyframes: {
        frame: number;
        time: number;
    }[];
    /** Every keyframe in the range; the first 500 are listed. */
    total: number;
}

/** The params of `video.frames`: Video frames from one to another, every stride-th, scaled to fit maxEdge, as luma or BGRA in base64. At most 256 frames and 16 MiB of pixels come in one call. */
export interface VideoFramesParams {
    from: number;
    to: number;
    /** Default 1, every frame. */
    stride?: number;
    /** Default 64. */
    maxEdge?: number;
    /** Default luma, one byte per pixel. */
    format?: "luma" | "bgra";
    /** Render the subtitles on them. Default false. */
    subtitles?: boolean;
}

/** The result of `video.frames`. */
export interface VideoFramesResult {
    width: number;
    height: number;
    format: "luma" | "bgra";
    frames: {
        frame: number;
        /** width bytes per row for luma, width * 4 for bgra; top row first; base64. */
        data: string;
    }[];
}

/** The params of `video.set_keyframes`: Replaces the video's keyframes as Open keyframes does. With file, writes them there as an Aegisub keyframes file first and opens it. Not an undo step. */
export interface VideoSetKeyframesParams {
    frames: number[];
    /** A writable file handle, from files.pick or files.open. */
    file?: string;
}

/** The result of `video.set_keyframes`. */
export interface VideoSetKeyframesResult {
    count: number;
    file?: string;
}

/** The params of `audio.info`: Whether audio is open and loaded, its duration and sample rate. */
export type AudioInfoParams = Record<string, never>;

/** The result of `audio.info`. */
export interface AudioInfoResult {
    open: boolean;
    ready?: boolean;
    duration?: number;
    sampleRate?: number;
    channels?: number;
}

/** The params of `audio.peaks`: The lowest and highest 16-bit sample in each of `buckets` equal slices of a time range. */
export interface AudioPeaksParams {
    start: number;
    end: number;
    /** Default 256. */
    buckets?: number;
}

/** The result of `audio.peaks`. */
export interface AudioPeaksResult {
    start: number;
    end: number;
    min: number[];
    max: number[];
}

/** The params of `files.list`: The files and folders the user granted this caller; with folder, also what that folder holds. */
export interface FilesListParams {
    /** A folder handle to list. */
    folder?: string;
    /** A folder inside it; default the folder itself. */
    path?: string;
}

/** The result of `files.list`. */
export interface FilesListResult {
    files: GrantedFile[];
    folders?: GrantedFolder[];
    /** With folder: in name order, at most 4096, without links that lead outside it. */
    entries?: FolderEntry[];
    /** Present when the folder holds more than 4096 entries. */
    truncated?: boolean;
}

/** The params of `files.read`: A granted file's content, as UTF-8 text or base64; at most 16 MiB. */
export interface FilesReadParams {
    /** A handle from files.list. */
    file?: string;
    /** Instead of file, a folder handle from files.list. */
    folder?: string;
    /** With folder, the file's path inside it, with / between its parts. */
    path?: string;
    /** Default utf8. */
    encoding?: "utf8" | "base64";
}

/** The result of `files.read`. */
export interface FilesReadResult {
    file: string;
    size: number;
    encoding: "utf8" | "base64";
    data: string;
}

/** The params of `files.write`: Replaces a file granted for writing with new content; in a writable folder, makes it and its folders. An AI caller's write is staged for the user to review and reaches the disk only when kept. */
export interface FilesWriteParams {
    /** A handle from files.list. */
    file?: string;
    /** Instead of file, a folder handle from files.list. */
    folder?: string;
    /** With folder, the file's path inside it, with / between its parts. */
    path?: string;
    /** Default utf8. */
    encoding?: "utf8" | "base64";
    data: string;
}

/** The result of `files.write`. */
export interface FilesWriteResult {
    file: string;
    size: number;
    /** True: waiting in the review, not on the disk yet. */
    staged?: boolean;
}

/** The params of `skills.list`: The caller's skills: each skill folder's name, whether it is the Project's or the user's, and its files, SKILL.md among them. */
export type SkillsListParams = Record<string, never>;

/** The result of `skills.list`. */
export interface SkillsListResult {
    skills: {
        skill: SkillName;
        origin: "project" | "yours";
        files: string[];
    }[];
}

/** The params of `skills.read`: One file of a skill skills.list gives, as UTF-8 text; at most 256 KiB. */
export interface SkillsReadParams {
    skill: SkillName;
    /** A path skills.list gives, such as SKILL.md or references/tags.md. */
    file: string;
}

/** The result of `skills.read`. */
export interface SkillsReadResult {
    skill: SkillName;
    file: string;
    size: number;
    data: string;
}

/** The params of `automation.macros`: The loaded Automation macros, as the Automation menu lists them. */
export interface AutomationMacrosParams {
    /** Keep macros whose name, script or file name holds this, ignoring case. */
    query?: string;
}

/** The result of `automation.macros`. */
export interface AutomationMacrosResult {
    macros: {
        /** The script's name. */
        script: string;
        /** The script's file name. */
        file: string;
        macro: string;
        /** The macro's place among the script's, from 0. */
        ordinal: number;
        description: string;
        /** The script is loaded and shown in the user's window. */
        enabled: boolean;
        disabledReason?: string;
    }[];
}

/** The params of `automation.run_macro`: Runs a loaded Automation macro on the active tab's selection, as the menu would, as one undo step. Answers fill its dialogs. Plugins only. */
export interface AutomationRunMacroParams {
    /** The script's file name. */
    file: string;
    macro: string;
    /** One per dialog, in order, as macro.ran gives them. */
    answers?: DialogAnswer[];
    /** never (the default) answers a dialog whose key matches without showing it; prefilled shows it filled in. A dialog without a matching answer, and a question, is always shown. */
    show?: "never" | "prefilled";
}

/** The result of `automation.run_macro`. */
export interface AutomationRunMacroResult {
    outcome: "applied" | "refused" | "failed" | "cancelled" | "declined" | "stopped" | "lost";
    message: string;
    log: string;
    /** The answers its dialogs got, as macro.ran gives them; only when the caller sees the Document it ran on. */
    dialogs?: DialogAnswer[];
}

/** The params of `automation.run`: Saves a Lua Automation script to the AI scripts folder and runs one of its macros on the active tab's selection, once the user approves the script. The run is one undo step. */
export interface AutomationRunParams {
    /** The script's file name without .lua; an earlier script of that name is replaced. */
    name: string;
    /** The whole Lua script, registering its macro with aegisub.register_macro. */
    script: string;
    /** The macro to run, by its registered name; default the first. */
    macro?: string;
}

/** The result of `automation.run`. */
export interface AutomationRunResult {
    /** The saved script's file name. */
    file: string;
    outcome: "applied" | "refused" | "failed" | "cancelled" | "declined" | "stopped" | "lost" | "invalid";
    message: string;
    log: string;
}

/** The params of `translation.project`: The Document's Honya project files: PROJECT.md, GLOSSARY.md, CHARACTERS.md and STYLE.md, those that exist. folder is null when the Document has no project folder. */
export interface TranslationProjectParams {
    /** Defaults to the active Document. */
    document?: DocumentId;
}

/** The result of `translation.project`. */
export interface TranslationProjectResult {
    folder: string | null;
    files: {
        name: "PROJECT.md" | "GLOSSARY.md" | "CHARACTERS.md" | "STYLE.md";
        data: string;
    }[];
}

/** The params of `translation.project_write`: Replaces one of the Document's Honya project files, keeping Honya's Markdown and honya:data format. */
export interface TranslationProjectWriteParams {
    /** Defaults to the active Document. */
    document?: DocumentId;
    file: "PROJECT.md" | "GLOSSARY.md" | "CHARACTERS.md" | "STYLE.md";
    data: string;
}

/** The result of `translation.project_write`. */
export interface TranslationProjectWriteResult {
    file: "PROJECT.md" | "GLOSSARY.md" | "CHARACTERS.md" | "STYLE.md";
    size: number;
}

/** The params of `nodes.catalogue`: The node types the node-effect editor offers, with their ports and parameters. */
export interface NodesCatalogueParams {
    category?: string;
}

/** The result of `nodes.catalogue`. */
export interface NodesCatalogueResult {
    nodes: {
        id: string;
        category: string;
        summary: string;
        inputs: NodePort[];
        outputs: NodePort[];
        params: { [key: string]: Json };
        since: number;
    }[];
}

/** The params of `nodes.validate`: Checks a node-effect graph; each problem names its node and port or field. */
export interface NodesValidateParams {
    graph: { [key: string]: Json };
}

/** The result of `nodes.validate`. */
export interface NodesValidateResult {
    valid: boolean;
    problems: {
        node?: string;
        port?: string;
        message: string;
    }[];
}

/** The params of `effects.graphs`: The Document's node-effect graphs, one per graph Line. */
export interface EffectsGraphsParams {
    /** Defaults to the active Document. */
    document?: DocumentId;
}

/** The result of `effects.graphs`. */
export interface EffectsGraphsResult {
    document: DocumentId;
    revision: Revision;
    graphs: {
        line: LineId;
        name: string;
        /** The Style whose karaoke Lines it runs on. */
        style: string;
        valid: boolean;
    }[];
}

/** The params of `effects.graph`: One graph Line's graph, with what keeps it from running. */
export interface EffectsGraphParams {
    /** Defaults to the active Document. */
    document?: DocumentId;
    line: LineId;
}

/** The result of `effects.graph`. */
export interface EffectsGraphResult {
    line: LineId;
    name: string;
    style: string;
    revision: Revision;
    /** Null when the Line's text is no graph. */
    graph: { [key: string]: Json } | null;
    valid: boolean;
    problems: {
        node?: string;
        port?: string;
        message: string;
    }[];
}

/** The params of `effects.edit`: Edits a graph in steps (add, connect, disconnect, remove, set) and gives the new graph with its problems; nothing in the Document changes. */
export interface EffectsEditParams {
    /** A graph as effects.graph gives it; {"format": 1, "nodes": [], "wires": []} starts an empty one. */
    graph: { [key: string]: Json };
    /** Applied in order; a refused step changes nothing. */
    steps: {
        op: "add" | "connect" | "disconnect" | "remove" | "set";
        /** add: a node type from nodes.catalogue. */
        type?: string;
        /** add: the new node's id, made from its type when omitted; remove, set: the node. */
        node?: string;
        /** add: the node's fields. */
        fields?: { [key: string]: Json };
        /** connect: an output, "node.port". */
        from?: string;
        /** connect, disconnect: an input, "node.port"; connect replaces a wire already there. */
        to?: string;
        /** set: the field. */
        field?: string;
        /** set: the field's value; null restores its default. */
        value?: Json;
    }[];
}

/** The result of `effects.edit`. */
export interface EffectsEditResult {
    graph: { [key: string]: Json };
    /** The ids of the nodes added, in step order. */
    added: string[];
    valid: boolean;
    problems: {
        node?: string;
        port?: string;
        message: string;
    }[];
}

/** The params of `effects.preview`: Runs a graph on the Document's karaoke Lines and gives the Lines it would write; nothing is applied. Lua nodes don't run. */
export interface EffectsPreviewParams {
    /** Defaults to the active Document. */
    document?: DocumentId;
    /** A graph Line: its graph, name and Style. */
    line?: LineId;
    /** A graph to run in place of the Line's. */
    graph?: { [key: string]: Json };
    /** The Style whose karaoke Lines it runs on; needed without line. */
    style?: string;
    /** The graph's name, which the Lines' Effect "fx <name>" carries. */
    name?: string;
    /** Only these karaoke Lines; all of them when omitted. */
    sources?: LineId[];
    /** How many generated Lines to give; default 20. */
    limit?: number;
}

/** The result of `effects.preview`. */
export interface EffectsPreviewResult {
    /** False when a problem stopped the run; applying it would change nothing. */
    ran: boolean;
    complete: boolean;
    /** How many karaoke Lines ran. */
    sources: number;
    /** How many Lines the run made. */
    generated: number;
    /** The first generated Lines, as ASS Dialogue lines. */
    lines: string[];
    problems: {
        node?: string;
        line?: LineId;
        message: string;
    }[];
}

/** The params of `effects.save`: Writes a graph to the Document as one undo step: into a new graph Line, or in place of a graph Line's. */
export interface EffectsSaveParams {
    /** Defaults to the active Document. */
    document?: DocumentId;
    graph: { [key: string]: Json };
    /** The graph Line to replace; omit it to add a graph. */
    line?: LineId;
    /** A new graph's name. */
    name?: string;
    /** A new graph's Style: whose karaoke Lines it runs on. */
    style?: string;
}

/** The result of `effects.save`. */
export interface EffectsSaveResult {
    line: LineId;
    revision: Revision;
    graph: { [key: string]: Json };
    valid: boolean;
    problems: {
        node?: string;
        port?: string;
        message: string;
    }[];
}

/** The params of `effects.apply`: Runs a graph Line and writes its Lines as one undo step, replacing those it wrote before; the karaoke Lines it ran on become Comments. Lua nodes don't run, so a graph with one applies from the node-effect editor. */
export interface EffectsApplyParams {
    /** Defaults to the active Document. */
    document?: DocumentId;
    /** The graph Line. */
    line: LineId;
    /** Only these karaoke Lines; all of them when omitted. */
    sources?: LineId[];
}

/** The result of `effects.apply`. */
export interface EffectsApplyResult {
    revision: Revision;
    generated: number;
    /** How many karaoke Lines got Lines. */
    sources: number;
    changed: boolean;
}

/** The params of `plugins.list`: The installed plugins with their state, trust tier and commands. */
export type PluginsListParams = Record<string, never>;

/** The result of `plugins.list`. */
export interface PluginsListResult {
    plugins: {
        id: string;
        name: string;
        version: string;
        description: string;
        source: "file" | "marketplace";
        signed: boolean;
        enabled: boolean;
        tier: "restricted" | "unrestricted";
        revoked: boolean;
        /** While the plugin runs. */
        commands: {
            /** plugin.<plugin id>.<command id> */
            command: string;
            title: string;
        }[];
    }[];
}

/** The params of `plugins.manifest`: An installed plugin's hikari-plugin.json. */
export interface PluginsManifestParams {
    plugin: string;
}

/** The result of `plugins.manifest`. */
export interface PluginsManifestResult {
    plugin: string;
    manifest: { [key: string]: Json };
}

/** The params of `plugins.scaffold`: Writes a plugin from the authoring guide's template into the AI plugins folder and packs it; nothing is installed. */
export interface PluginsScaffoldParams {
    /** Reverse-DNS, lowercase: com.example.sign-counter. An earlier scaffold of that id is replaced. */
    plugin: string;
    name: string;
    /** Semantic version; default 0.1.0. */
    version?: string;
    description?: string;
    /** restricted (default) runs in the sandbox without ffi, io, os and debug; unrestricted needs the user's full trust at install. */
    tier?: "restricted" | "unrestricted";
    /** The manifest's permissions: documents.all, network {hosts, reason}, files, vcs.read. */
    permissions?: { [key: string]: Json };
    commands: {
        id: string;
        title: string;
    }[];
    /** main.lua, registering each command with hikari.commands.register; the guide's first plugin when omitted. */
    code?: string;
    /** More files beside main.lua and hikari-plugin.json. */
    files?: {
        path: string;
        text: string;
    }[];
}

/** The result of `plugins.scaffold`. */
export interface PluginsScaffoldResult {
    plugin: string;
    folder: string;
    /** The packed .hikariplugin. */
    package: string;
    files: string[];
    manifest: { [key: string]: Json };
}

/** The params of `plugins.review`: What installing a package would do, as the install question shows it: its trust tier, signing, permissions, commands and files. */
export interface PluginsReviewParams {
    /** A plugin plugins.scaffold made. */
    plugin?: string;
    /** A .hikariplugin the user granted, by its files.list handle. */
    file?: string;
}

/** The result of `plugins.review`. */
export interface PluginsReviewResult {
    plugin: string;
    name: string;
    version: string;
    change: "install" | "update" | "reinstall" | "downgrade";
    installedVersion: string;
    /** unrestricted runs outside the sandbox and needs the user's full trust. */
    tier: "restricted" | "unrestricted";
    /** The signing key changed, or an unsigned package replaces a signed one. */
    needsRetrust: boolean;
    /** The signing key; empty when unsigned. */
    keyId: string;
    pinnedKeyId: string;
    permissions: string[];
    /** The titles of the commands it adds. */
    commands: string[];
    files: {
        path: string;
        text: string;
    }[];
    digest: string;
}

/** The params of `plugins.install`: Installs a scaffolded or granted plugin package and turns it on, once the user approves it in HikariSub; it asks every time. */
export interface PluginsInstallParams {
    /** A plugin plugins.scaffold made. */
    plugin?: string;
    /** A .hikariplugin the user granted, by its files.list handle. */
    file?: string;
}

/** The result of `plugins.install`. */
export interface PluginsInstallResult {
    plugin: string;
    name: string;
    version: string;
    change: "install" | "update" | "reinstall" | "downgrade";
    installedVersion: string;
    /** unrestricted runs outside the sandbox and needs the user's full trust. */
    tier: "restricted" | "unrestricted";
    /** The signing key changed, or an unsigned package replaces a signed one. */
    needsRetrust: boolean;
    /** The signing key; empty when unsigned. */
    keyId: string;
    pinnedKeyId: string;
    permissions: string[];
    /** The titles of the commands it adds. */
    commands: string[];
    files: {
        path: string;
        text: string;
    }[];
    digest: string;
    installed: true;
}

/** The params of `plugins.run`: Runs an installed plugin's command as its menu item would. */
export interface PluginsRunParams {
    plugin: string;
    /** The command's id within the plugin, as in its manifest. */
    command: string;
}

/** The result of `plugins.run`. */
export interface PluginsRunResult {
    ran: true;
}

/** The params of `status_items.set`: Changes one of the caller's status bar items. Plugins only. */
export interface StatusItemsSetParams {
    /** An id from contributes.statusItems. */
    item: string;
    /** Empty: icon only. */
    text?: string;
    /** An icon role or own:<id>; empty: text only. */
    icon?: string;
    tooltip?: string;
    tone?: "normal" | "warning" | "danger";
    visible?: boolean;
}

/** The result of `status_items.set`. */
export interface StatusItemsSetResult {
    item: string;
}

/** The params of `formats.list`: The subtitle formats HikariSub can open and export, built-in and from plugins. */
export type FormatsListParams = Record<string, never>;

/** The result of `formats.list`. */
export interface FormatsListResult {
    formats: {
        /** ass, srt, … or plugin:<plugin id>/<format id>. */
        id: string;
        title: string;
        extensions: string[];
        import: boolean;
        export: boolean;
        /** The plugin's id, for a plugin format. */
        plugin?: string;
    }[];
}

/** The params of `typeset.perspective`: The Line's text with the tags that render it on a quad (VT3's perspective solver): \pos, \org, \frx, \fry, \frz, \fax, \fscx and \fscy. */
export interface TypesetPerspectiveParams {
    /** Defaults to the active Document. */
    document?: DocumentId;
    line: LineId;
    /** Script coordinates: top left, top right, bottom right, bottom left. */
    quad: {
        x: number;
        y: number;
    }[];
    /** Where \org goes: the quad's centre (default), the centre without \fax, or where the Line has it. */
    origin?: "center" | "nofax" | "keep";
}

/** The result of `typeset.perspective`. */
export interface TypesetPerspectiveResult {
    line: LineId;
    text: string;
    transform: { [key: string]: Json };
}

/** The params of `typeset.clip`: The Line's text with its first clip replaced by a rectangle or a vector clip. */
export interface TypesetClipParams {
    /** Defaults to the active Document. */
    document?: DocumentId;
    line: LineId;
    /** Script coordinates, rounded to whole units. */
    rect?: {
        x1: number;
        y1: number;
        x2: number;
        y2: number;
    };
    /** ASS drawing commands such as "m 0 0 l 100 0 100 50 0 50"; empty removes the vector clip. */
    vector?: string;
    /** \iclip: draw outside the clip. */
    inverse?: boolean;
}

/** The result of `typeset.clip`. */
export interface TypesetClipResult {
    line: LineId;
    /** The Line's text with the tags written; nothing is applied. */
    text: string;
}

/** The params of `typeset.drawing`: The Line's text with its first drawing replaced by a shape (\p1), placed at a position with an alignment. */
export interface TypesetDrawingParams {
    /** Defaults to the active Document. */
    document?: DocumentId;
    line: LineId;
    /** ASS drawing commands, in script units from the drawing's origin. */
    shape: string;
    position?: {
        x: number;
        y: number;
    };
    /** \an; 7 (top left) with position, else the Line's. */
    alignment?: number;
}

/** The result of `typeset.drawing`. */
export interface TypesetDrawingResult {
    line: LineId;
    /** The Line's text with the tags written; nothing is applied. */
    text: string;
}

/** The params of `typeset.shift`: Moves the Lines' positions, \move points, clips, drawings and \org by an offset, as the Position shifter does. */
export interface TypesetShiftParams {
    /** Defaults to the active Document. */
    document?: DocumentId;
    lines: LineId[];
    /** Script units. */
    dx: number;
    dy: number;
    /** What moves; every kind but drawing when omitted. */
    kinds?: ("position" | "move_start" | "move_end" | "clip" | "drawing" | "origin")[];
}

/** The result of `typeset.shift`. */
export interface TypesetShiftResult {
    /** The Lines the change alters, with their new text; nothing is applied. */
    lines: {
        line: LineId;
        text: string;
    }[];
}

/** The params of `typeset.hydra`: Changes one tag across the Lines as the Hydra strip does: set it, add to it, multiply it by line number, or run a gradient across characters or Lines. */
export interface TypesetHydraParams {
    /** Defaults to the active Document. */
    document?: DocumentId;
    /** Multiply and the line gradients count them in this order. */
    lines: LineId[];
    /** A Hydra tag without the backslash: blur, bord, be, alpha, 1a, 3a, 4a, 1c (or c), 3c, 4c, fad, fax, fay, fs, fsp, shad, xbord, ybord, xshad, yshad, pos or t. */
    tag: string;
    change: "set" | "add" | "multiply" | "gradient";
    /** set: the value; add: what to add; multiply: the start; gradient: the first value. Colours are R, G, B. */
    values: number[];
    /** multiply: added for each Line. */
    perLine?: number[];
    /** multiply: set each Line's value, or add to it. Default set. */
    byLine?: "set" | "add";
    /** gradient: the last value. */
    to?: number[];
    /** gradient: default characters. */
    across?: "characters" | "lines";
    /** gradient: run from the end. */
    decreasing?: boolean;
}

/** The result of `typeset.hydra`. */
export interface TypesetHydraResult {
    /** The Lines the change alters, with their new text; nothing is applied. */
    lines: {
        line: LineId;
        text: string;
    }[];
}

/** The params of `typeset.check`: Renders one Line alone, with a proposed text, on its frame and compares its ink with a target. */
export interface TypesetCheckParams {
    /** Defaults to the active Document. */
    document?: DocumentId;
    line: LineId;
    /** The proposed text; the Line's own when omitted. */
    text?: string;
    /** Where the Line's ink should land, in script coordinates: a quad or a box. */
    target: {
        /** Script coordinates: top left, top right, bottom right, bottom left. */
        quad: {
            x: number;
            y: number;
        }[];
    } | {
        box: {
            x: number;
            y: number;
            width: number;
            height: number;
        };
    };
    /** Default: the frame shown when the Line is on it, else the Line's first. */
    frame?: number;
    /** Video pixels; default 4. */
    tolerance?: number;
}

/** The result of `typeset.check`. */
export interface TypesetCheckResult {
    line: LineId;
    frame: number;
    pass: boolean;
    /** The largest distance between an edge of the ink's bounds and the target's, in video pixels; null when nothing is drawn. */
    error: number | null;
    ink: {
        x: number;
        y: number;
        width: number;
        height: number;
    } | null;
    target: {
        x: number;
        y: number;
        width: number;
        height: number;
    };
}

/** The params of `typeset.apply`: Applies typeset Lines as one undo step after every one passes the render check; when one misses its target nothing is applied. */
export interface TypesetApplyParams {
    /** Defaults to the active Document. */
    document?: DocumentId;
    revision: Revision;
    /** A few words naming the change, such as "Typeset the shop sign". */
    name: string;
    lines: {
        line: LineId;
        text: string;
        /** Where the Line's ink should land, in script coordinates: a quad or a box. */
        target: {
            /** Script coordinates: top left, top right, bottom right, bottom left. */
            quad: {
                x: number;
                y: number;
            }[];
        } | {
            box: {
                x: number;
                y: number;
                width: number;
                height: number;
            };
        };
        frame?: number;
    }[];
    /** Video pixels; default 4. */
    tolerance?: number;
}

/** The result of `typeset.apply`. */
export interface TypesetApplyResult {
    revision: Revision;
    inserted: LineId[];
    changed: boolean;
    checks: {
        line: LineId;
        frame: number;
        pass: boolean;
        /** The largest distance between an edge of the ink's bounds and the target's, in video pixels; null when nothing is drawn. */
        error: number | null;
        ink: {
            x: number;
            y: number;
            width: number;
            height: number;
        } | null;
        target: {
            x: number;
            y: number;
            width: number;
            height: number;
        };
    }[];
}

/** The params of `project.info`: The Project of the window the user works in, or null. */
export type ProjectInfoParams = Record<string, never>;

/** The result of `project.info`. */
export interface ProjectInfoResult {
    project: Project;
}

/** The params of `vcs.refs`: The branches, tags and recent commits of the git repository holding the Document, or the Project's. Plugins whose manifest asks for vcs.read only. */
export interface VcsRefsParams {
    /** Defaults to the active Document; an unsaved one gives the Project's repository. */
    document?: DocumentId;
    /** How many recent commits; 50 when left out. */
    limit?: number;
}

/** The result of `vcs.refs`. */
export interface VcsRefsResult {
    /** The repository's work tree. */
    root: string;
    /** The Document's path in the repository, '/'-separated; null for the Project's. */
    path: string | null;
    /** null before the first commit. */
    head: null | {
        /** The current branch; null when detached. */
        ref: string | null;
        commit: string;
    };
    /** At most 500, newest first. */
    branches: {
        name: string;
        commit: string;
        current: boolean;
    }[];
    /** At most 500, newest first. */
    tags: {
        name: string;
        commit: string;
    }[];
    /** The newest from HEAD. */
    commits: {
        commit: string;
        summary: string;
        author: string;
        /** Seconds since 1970, when it was committed. */
        time: number;
    }[];
}

/** The params of `vcs.show`: The Document's file as a git branch, tag or commit has it. Plugins whose manifest asks for vcs.read only. */
export interface VcsShowParams {
    /** A branch, tag, commit or revision such as HEAD~2. */
    ref: string;
    /** Defaults to the active Document. */
    document?: DocumentId;
}

/** The result of `vcs.show`. */
export interface VcsShowResult {
    ref: string;
    commit: string;
    /** The Document's path in the repository, '/'-separated. */
    path: string;
    /** The file at that commit, as UTF-8. */
    text: string;
}

/** The params of `presence.set_text`: Sets or clears the caller's text for {plugin} in the user's Discord status. Plugins whose manifest asks for presence only. */
export interface PresenceSetTextParams {
    /** Plain text; empty clears it. */
    text: string;
}

/** The result of `presence.set_text`. */
export type PresenceSetTextResult = Record<string, never>;

/** The params of `karaoke.syllables`: A Line's karaoke syllables as the templater reads them: text, tag, start and end in ms. */
export interface KaraokeSyllablesParams {
    /** Defaults to the active Document. */
    document?: DocumentId;
    line: LineId;
}

/** The result of `karaoke.syllables`. */
export interface KaraokeSyllablesResult {
    line: LineId;
    syllables: {
        text: string;
        stripped: string;
        tag: string;
        start: number;
        end: number;
    }[];
}

/** The params of `karaoke.split`: Splits Lines without \k tags into syllables as the audio editor's karaoke mode does, the time shared by character; Lines that already have \k tags keep them. */
export interface KaraokeSplitParams {
    /** Defaults to the active Document. */
    document?: DocumentId;
    lines: LineId[];
    /** Default true: by syllable, with the language rules the user set; false: at spaces. */
    bySyllable?: boolean;
}

/** The result of `karaoke.split`. */
export interface KaraokeSplitResult {
    /** The Lines that change, with their new text; nothing is applied. */
    lines: {
        line: LineId;
        text: string;
    }[];
}

/** The params of `karaoke.time`: Times a Line's existing syllables: each syllable's end in ms, in order; the \k tags are rewritten. */
export interface KaraokeTimeParams {
    /** Defaults to the active Document. */
    document?: DocumentId;
    line: LineId;
    /** One per syllable, ascending, in ms from the video's start. */
    ends: number[];
}

/** The result of `karaoke.time`. */
export interface KaraokeTimeResult {
    line: LineId;
    /** The Line's text with the \k tags written; nothing is applied. */
    text: string;
}

/** The params of `karaoke.onsets`: Where sung notes start in the open audio, in ms, within a range. */
export interface KaraokeOnsetsParams {
    start: number;
    end: number;
}

/** The result of `karaoke.onsets`. */
export interface KaraokeOnsetsResult {
    onsets: number[];
}

/** The params of `karaoke.snap`: Moves each syllable boundary inside the Lines to the nearest vocal onset within the window, keeping every syllable at least 10 ms long. */
export interface KaraokeSnapParams {
    /** Defaults to the active Document. */
    document?: DocumentId;
    lines: LineId[];
    /** Ms either side; default 100. */
    window?: number;
}

/** The result of `karaoke.snap`. */
export interface KaraokeSnapResult {
    /** The Lines that change, with their new text; nothing is applied. */
    lines: {
        line: LineId;
        text: string;
        /** Boundaries moved. */
        moved?: number;
    }[];
}

/** The params of `karaoke.kanji`: Copies a timed romaji Line's syllables onto its kanji Line as the Kanji timer suggests the groups. */
export interface KaraokeKanjiParams {
    /** Defaults to the active Document. */
    document?: DocumentId;
    romaji: LineId;
    kanji: LineId;
    /** Further syllables of a group become # syllables instead of joining the first. */
    multi?: boolean;
}

/** The result of `karaoke.kanji`. */
export interface KaraokeKanjiResult {
    line: LineId;
    text: string;
    /** False when a group came out blank or the characters ran out: check it. */
    clean: boolean;
}

/** The params of `karaoke.furigana`: The readings the dictionary suggests for a Line's kanji runs, in hiragana. */
export interface KaraokeFuriganaParams {
    /** Defaults to the active Document. */
    document?: DocumentId;
    line: LineId;
}

/** The result of `karaoke.furigana`. */
export interface KaraokeFuriganaResult {
    line: LineId;
    runs: {
        base: string;
        reading: string;
    }[];
}

/** The params of `karaoke.check`: The Karaoke check's problems: unclosed tags, totals that differ from the Line, empty syllables and syllables outside the Line. */
export interface KaraokeCheckParams {
    /** Defaults to the active Document. */
    document?: DocumentId;
}

/** The result of `karaoke.check`. */
export interface KaraokeCheckResult {
    problems: {
        line: LineId;
        kind: "unclosed" | "not_for_audio_editor" | "editor_only" | "total_not_duration" | "zero_length" | "outside_line";
        totalMs?: number;
        durationMs?: number;
        /** Which ones, from 1. */
        syllables?: number[];
    }[];
    /** Problems past the listed ones. */
    unlisted: number;
}

/** The params of `karaoke.template_preview`: The Lines the karaoke templater generates from one karaoke Line, without applying them. */
export interface KaraokeTemplatePreviewParams {
    /** Defaults to the active Document. */
    document?: DocumentId;
    line: LineId;
}

/** The result of `karaoke.template_preview`. */
export interface KaraokeTemplatePreviewResult {
    line: LineId;
    lines: {
        layer: number;
        start: number;
        end: number;
        style: string;
        effect: string;
        text: string;
    }[];
}

/** The params of `karaoke.apply_template`: Runs the karaoke templater on the karaoke Lines, or the Lines given, and applies what it generates as one undo step: old generated Lines are replaced. */
export interface KaraokeApplyTemplateParams {
    /** Defaults to the active Document. */
    document?: DocumentId;
    revision: Revision;
    /** Default: every karaoke Line. */
    lines?: LineId[];
    /** Default "Apply karaoke template". */
    name?: string;
}

/** The result of `karaoke.apply_template`. */
export interface KaraokeApplyTemplateResult {
    revision: Revision;
    changed: boolean;
}

/** The params of `karaoke.align`: Runs the user's own WhisperX or Montreal Forced Aligner on the audio under the Lines and answers each word with its start and end in ms; the user approves every run, and nothing is applied. */
export interface KaraokeAlignParams {
    /** Defaults to the active Document. */
    document?: DocumentId;
    lines: LineId[];
    program: "whisperx" | "mfa";
    /** WhisperX: a language code such as ja; default: it detects. */
    language?: string;
    /** MFA: the pronunciation dictionary, such as english_mfa. */
    dictionary?: string;
    /** MFA: the acoustic model, such as english_mfa. */
    acousticModel?: string;
}

/** The result of `karaoke.align`. */
export interface KaraokeAlignResult {
    words: {
        text: string;
        start: number;
        end: number;
    }[];
}

/** The params of `marks.set`: Marks Grid rows with a coloured bar and a tooltip, replacing the caller's marks on those Lines. Marks belong to the plugin, are never saved and go when it stops. Not an undo step. Plugins only. */
export interface MarksSetParams {
    /** Defaults to the active Document. */
    document?: DocumentId;
    marks: {
        line: LineId;
        colour: MarkColour;
        /** What the mark means; shown on hover after the plugin's name and read by screen readers. */
        tooltip: string;
    }[];
}

/** The result of `marks.set`. */
export interface MarksSetResult {
    document: DocumentId;
    marked: number;
}

/** The params of `marks.clear`: Removes the caller's Grid row marks. Plugins only. */
export interface MarksClearParams {
    /** Defaults to the active Document. */
    document?: DocumentId;
    /** These Lines only; every mark of the caller when omitted. */
    lines?: LineId[];
}

/** The result of `marks.clear`. */
export interface MarksClearResult {
    document: DocumentId;
}

/** The params of `marks.list`: The caller's Grid row marks on a Document. Plugins only. */
export interface MarksListParams {
    /** Defaults to the active Document. */
    document?: DocumentId;
}

/** The result of `marks.list`. */
export interface MarksListResult {
    document: DocumentId;
    marks: {
        line: LineId;
        colour: MarkColour;
        /** What the mark means; shown on hover after the plugin's name and read by screen readers. */
        tooltip: string;
    }[];
}

/** The params of `problems.list`: The problems on a Document: the open QC notes and the problems plugins set, in Document order, then by from, those with no Line last. */
export interface ProblemsListParams {
    /** Defaults to the active Document. */
    document?: DocumentId;
    /** These Lines only. */
    lines?: LineId[];
    /** This severity and worse only. */
    severity?: "error" | "warning" | "info" | "hint";
    /** One source id only. */
    source?: string;
}

/** The result of `problems.list`. */
export interface ProblemsListResult {
    document: DocumentId;
    revision: Revision;
    problems: Problem[];
}

/** The params of `problems.set`: Replaces the caller's problems under one set on a Document. Never saved and not an undo step; they go when the plugin stops, the Document closes, or their Line or field changes. Plugins only. */
export interface ProblemsSetParams {
    /** Defaults to the active Document. */
    document?: DocumentId;
    /** The revision the offsets were measured on; stale_revision when it moved. */
    revision: Revision;
    /** The plugin's own name for this group of checks; default when omitted. */
    set?: string;
    problems: Problem[];
}

/** The result of `problems.set`. */
export interface ProblemsSetResult {
    document: DocumentId;
    count: number;
}

/** The params of `problems.clear`: Removes the caller's problems. Plugins only. */
export interface ProblemsClearParams {
    /** Every Document when omitted. */
    document?: DocumentId;
    /** Every set of the caller's when omitted. */
    set?: string;
}

/** The result of `problems.clear`. */
export interface ProblemsClearResult {
    cleared: number;
}

/** The params of `problems.fix`: Applies one of a problem's quick fixes through document.apply, as one undo step named after the problem's source and the fix; stale_revision when its Line changed since the problem was set. */
export interface ProblemsFixParams {
    /** Defaults to the active Document. */
    document?: DocumentId;
    /** An id from problems.list. */
    problem: string;
    /** The fix's index. */
    fix: number;
}

/** The result of `problems.fix`. */
export interface ProblemsFixResult {
    revision: Revision;
    /** New Lines, in edit order. */
    inserted: LineId[];
    /** False when the edits changed nothing: no undo step was added. */
    changed: boolean;
}

/** The params of `programs.run`: Runs one of the plugin's declared programs with its declared arguments, then args, and answers once it ends. Output is capped at 16 MiB per stream. Plugins with the programs permission only. */
export interface ProgramsRunParams {
    program: string;
    /** ${file:N} becomes the path of files[N], which the program may then open. */
    args?: string[];
    /** Read-only unless the handle is writable. */
    files?: string[];
    stdin?: string;
    /** Of stdin, stdout and stderr; utf8 when omitted. */
    encoding?: "utf8" | "base64";
    /** Milliseconds; 60000 when omitted. */
    timeout?: number;
}

/** The result of `programs.run`. */
export interface ProgramsRunResult {
    exitCode: number;
    stdout: string;
    stderr: string;
    encoding: "utf8" | "base64";
}

/** The params of `programs.start`: Starts one of the plugin's declared programs and keeps it running; its output comes as program.output and its end as program.exited. A plugin keeps at most 8 running. Plugins with the programs permission only. */
export interface ProgramsStartParams {
    program: string;
    args?: string[];
    files?: string[];
    /** Of what program.output carries and programs.send takes. */
    encoding?: "utf8" | "base64";
}

/** The result of `programs.start`. */
export interface ProgramsStartResult {
    process: ProgramProcess;
}

/** The params of `programs.send`: Writes to a started program's stdin; close: true closes stdin after data. Plugins with the programs permission only. */
export interface ProgramsSendParams {
    process: ProgramProcess;
    data?: string;
    close?: boolean;
}

/** The result of `programs.send`. */
export interface ProgramsSendResult {
    process: ProgramProcess;
}

/** The params of `programs.stop`: Ends a started program and its children. Plugins with the programs permission only. */
export interface ProgramsStopParams {
    process: ProgramProcess;
}

/** The result of `programs.stop`. */
export interface ProgramsStopResult {
    process: ProgramProcess;
}

/** Every Hikari API operation: a method of its own name, with its params and result. */
export interface Operations {
    "documents.list": { params: DocumentsListParams; result: DocumentsListResult };
    "documents.get": { params: DocumentsGetParams; result: DocumentsGetResult };
    "lines.list": { params: LinesListParams; result: LinesListResult };
    "lines.get": { params: LinesGetParams; result: LinesGetResult };
    "lines.find": { params: LinesFindParams; result: LinesFindResult };
    "styles.list": { params: StylesListParams; result: StylesListResult };
    "info.list": { params: InfoListParams; result: InfoListResult };
    "selection.get": { params: SelectionGetParams; result: SelectionGetResult };
    "api.version": { params: ApiVersionParams; result: ApiVersionResult };
    "commands.list": { params: CommandsListParams; result: CommandsListResult };
    "panels.list": { params: PanelsListParams; result: PanelsListResult };
    "settings.list": { params: SettingsListParams; result: SettingsListResult };
    "settings.get": { params: SettingsGetParams; result: SettingsGetResult };
    "commands.run": { params: CommandsRunParams; result: CommandsRunResult };
    "panels.open": { params: PanelsOpenParams; result: PanelsOpenResult };
    "panels.close": { params: PanelsCloseParams; result: PanelsCloseResult };
    "panels.focus": { params: PanelsFocusParams; result: PanelsFocusResult };
    "settings.open": { params: SettingsOpenParams; result: SettingsOpenResult };
    "settings.set": { params: SettingsSetParams; result: SettingsSetResult };
    "document.apply": { params: DocumentApplyParams; result: DocumentApplyResult };
    "selection.set": { params: SelectionSetParams; result: SelectionSetResult };
    "document.group_begin": { params: DocumentGroupBeginParams; result: DocumentGroupBeginResult };
    "document.group_end": { params: DocumentGroupEndParams; result: DocumentGroupEndResult };
    "video.info": { params: VideoInfoParams; result: VideoInfoResult };
    "video.frame_at_time": { params: VideoFrameAtTimeParams; result: VideoFrameAtTimeResult };
    "video.time_of_frame": { params: VideoTimeOfFrameParams; result: VideoTimeOfFrameResult };
    "video.frame": { params: VideoFrameParams; result: VideoFrameResult };
    "video.ink_bounds": { params: VideoInkBoundsParams; result: VideoInkBoundsResult };
    "video.colours": { params: VideoColoursParams; result: VideoColoursResult };
    "video.keyframes": { params: VideoKeyframesParams; result: VideoKeyframesResult };
    "video.frames": { params: VideoFramesParams; result: VideoFramesResult };
    "video.set_keyframes": { params: VideoSetKeyframesParams; result: VideoSetKeyframesResult };
    "audio.info": { params: AudioInfoParams; result: AudioInfoResult };
    "audio.peaks": { params: AudioPeaksParams; result: AudioPeaksResult };
    "files.list": { params: FilesListParams; result: FilesListResult };
    "files.read": { params: FilesReadParams; result: FilesReadResult };
    "files.write": { params: FilesWriteParams; result: FilesWriteResult };
    "skills.list": { params: SkillsListParams; result: SkillsListResult };
    "skills.read": { params: SkillsReadParams; result: SkillsReadResult };
    "automation.macros": { params: AutomationMacrosParams; result: AutomationMacrosResult };
    "automation.run_macro": { params: AutomationRunMacroParams; result: AutomationRunMacroResult };
    "automation.run": { params: AutomationRunParams; result: AutomationRunResult };
    "translation.project": { params: TranslationProjectParams; result: TranslationProjectResult };
    "translation.project_write": { params: TranslationProjectWriteParams; result: TranslationProjectWriteResult };
    "nodes.catalogue": { params: NodesCatalogueParams; result: NodesCatalogueResult };
    "nodes.validate": { params: NodesValidateParams; result: NodesValidateResult };
    "effects.graphs": { params: EffectsGraphsParams; result: EffectsGraphsResult };
    "effects.graph": { params: EffectsGraphParams; result: EffectsGraphResult };
    "effects.edit": { params: EffectsEditParams; result: EffectsEditResult };
    "effects.preview": { params: EffectsPreviewParams; result: EffectsPreviewResult };
    "effects.save": { params: EffectsSaveParams; result: EffectsSaveResult };
    "effects.apply": { params: EffectsApplyParams; result: EffectsApplyResult };
    "plugins.list": { params: PluginsListParams; result: PluginsListResult };
    "plugins.manifest": { params: PluginsManifestParams; result: PluginsManifestResult };
    "plugins.scaffold": { params: PluginsScaffoldParams; result: PluginsScaffoldResult };
    "plugins.review": { params: PluginsReviewParams; result: PluginsReviewResult };
    "plugins.install": { params: PluginsInstallParams; result: PluginsInstallResult };
    "plugins.run": { params: PluginsRunParams; result: PluginsRunResult };
    "status_items.set": { params: StatusItemsSetParams; result: StatusItemsSetResult };
    "formats.list": { params: FormatsListParams; result: FormatsListResult };
    "typeset.perspective": { params: TypesetPerspectiveParams; result: TypesetPerspectiveResult };
    "typeset.clip": { params: TypesetClipParams; result: TypesetClipResult };
    "typeset.drawing": { params: TypesetDrawingParams; result: TypesetDrawingResult };
    "typeset.shift": { params: TypesetShiftParams; result: TypesetShiftResult };
    "typeset.hydra": { params: TypesetHydraParams; result: TypesetHydraResult };
    "typeset.check": { params: TypesetCheckParams; result: TypesetCheckResult };
    "typeset.apply": { params: TypesetApplyParams; result: TypesetApplyResult };
    "project.info": { params: ProjectInfoParams; result: ProjectInfoResult };
    "vcs.refs": { params: VcsRefsParams; result: VcsRefsResult };
    "vcs.show": { params: VcsShowParams; result: VcsShowResult };
    "presence.set_text": { params: PresenceSetTextParams; result: PresenceSetTextResult };
    "karaoke.syllables": { params: KaraokeSyllablesParams; result: KaraokeSyllablesResult };
    "karaoke.split": { params: KaraokeSplitParams; result: KaraokeSplitResult };
    "karaoke.time": { params: KaraokeTimeParams; result: KaraokeTimeResult };
    "karaoke.onsets": { params: KaraokeOnsetsParams; result: KaraokeOnsetsResult };
    "karaoke.snap": { params: KaraokeSnapParams; result: KaraokeSnapResult };
    "karaoke.kanji": { params: KaraokeKanjiParams; result: KaraokeKanjiResult };
    "karaoke.furigana": { params: KaraokeFuriganaParams; result: KaraokeFuriganaResult };
    "karaoke.check": { params: KaraokeCheckParams; result: KaraokeCheckResult };
    "karaoke.template_preview": { params: KaraokeTemplatePreviewParams; result: KaraokeTemplatePreviewResult };
    "karaoke.apply_template": { params: KaraokeApplyTemplateParams; result: KaraokeApplyTemplateResult };
    "karaoke.align": { params: KaraokeAlignParams; result: KaraokeAlignResult };
    "marks.set": { params: MarksSetParams; result: MarksSetResult };
    "marks.clear": { params: MarksClearParams; result: MarksClearResult };
    "marks.list": { params: MarksListParams; result: MarksListResult };
    "problems.list": { params: ProblemsListParams; result: ProblemsListResult };
    "problems.set": { params: ProblemsSetParams; result: ProblemsSetResult };
    "problems.clear": { params: ProblemsClearParams; result: ProblemsClearResult };
    "problems.fix": { params: ProblemsFixParams; result: ProblemsFixResult };
    "programs.run": { params: ProgramsRunParams; result: ProgramsRunResult };
    "programs.start": { params: ProgramsStartParams; result: ProgramsStartResult };
    "programs.send": { params: ProgramsSendParams; result: ProgramsSendResult };
    "programs.stop": { params: ProgramsStopParams; result: ProgramsStopResult };
}

/** The window the user works in opened or closed a Project, or the user moved to a window with another one. */
export interface ProjectChangedData {
    project: Project;
}

/** A Document's content changed: a commit, Undo or Redo. */
export interface DocumentChangedData {
    document: DocumentId;
    revision: Revision;
    /** The History step now current. */
    step: string;
}

/** The problems on a Document changed: a source set or cleared them, an edit or a deleted Line dropped some, a plugin stopped, or the open QC notes changed. */
export interface ProblemsChangedData {
    document: DocumentId;
    /** The ids of the sources whose problems changed: qc or a plugin id. */
    sources: string[];
}

/** A started program wrote to stdout or stderr. Only its plugin hears it, in order and never merged. */
export interface ProgramOutputData {
    process: ProgramProcess;
    stream: "stdout" | "stderr";
    data: string;
    encoding: "utf8" | "base64";
}

/** A started program ended. Only its plugin hears it. */
export interface ProgramExitedData {
    process: ProgramProcess;
    exitCode: number;
    /** true when programs.stop or the plugin's end stopped it. */
    stopped: boolean;
}

/** A Document was written to a file: Save, Save as, Save all or Save translation. Nothing comes for a save that was cancelled or failed. */
export interface DocumentSavedData {
    document: DocumentId;
    /** The file it was written to, as documents.list gives paths. */
    path: string;
    /** The revision that was written. */
    revision: Revision;
}

/** The selected or the active Line changed. */
export interface SelectionChangedData {
    document: DocumentId;
    revision: Revision;
    active: LineId | null;
    selected: LineId[];
}

/** A Document opened or closed, or another became active. */
export interface DocumentsChangedData {
    documents: DocumentId[];
    editingTarget: DocumentId | null;
}

/** A panel opened, closed, floated, docked or took the focus. */
export interface PanelChangedData {
    panel: string;
    title: string;
    open: boolean;
    floating: boolean;
    focused: boolean;
}

/** A command was triggered, from a menu, a key, the API or anywhere else. It reports the trigger, not the outcome: a Save that opens a dialog or a question comes before anything is written, and even when it is then cancelled; document.saved reports the write. */
export interface CommandRanData {
    command: string;
    /** False when it was refused or failed. */
    ok: boolean;
}

/** An Automation macro run ended, from the menu, a key, Repeat last run or the API, with the answers its dialogs got, in the form automation.run_macro takes. */
export interface MacroRanData {
    document: DocumentId;
    /** The script's file name. */
    file: string;
    macro: string;
    outcome: "applied" | "refused" | "failed" | "cancelled" | "declined" | "stopped" | "lost";
    /** True when it applied. */
    ok: boolean;
    dialogs: DialogAnswer[];
}

/** Every Hikari API event, with its data. */
export interface Events {
    "project.changed": ProjectChangedData;
    "document.changed": DocumentChangedData;
    "problems.changed": ProblemsChangedData;
    "program.output": ProgramOutputData;
    "program.exited": ProgramExitedData;
    "document.saved": DocumentSavedData;
    "selection.changed": SelectionChangedData;
    "documents.changed": DocumentsChangedData;
    "panel.changed": PanelChangedData;
    "command.ran": CommandRanData;
    "macro.ran": MacroRanData;
}

/** The program-plugin protocol's version. */
export type ProtocolVersion = 1;

/** A file handle: the handle, the path asked for, and its access. */
export interface FileHandle {
    file: string;
    name: string;
    access: "read" | "write";
}

/** A panel or dialog event. */
export interface UiEvent {
    kind: string;
    widget?: string;
    value?: Json;
    panel?: string;
    dialog?: string;
    button?: string;
    values?: { [id: string]: Json };
    files?: FileHandle[];
}

export interface InitializeParams {
    protocol: ProtocolVersion;
    api: number;
    plugin: { id: string; version: string };
    locale: string;
    settings: { [id: string]: Json };
}

export interface CommandParams {
    command: string;
    /** `{}` from a menu, key or status item. */
    args: Json;
    context: { via: "menu" | "key" | "palette" | "status" | "api" | "ai" };
}

export type EventParams = { [N in keyof Events]: { name: N; data: Events[N] } }[keyof Events];

export interface PanelParams {
    panel: string;
    event: UiEvent;
}

export interface DialogParams {
    dialog: string;
    event: UiEvent;
}

/** An icon button in a quick pick's title row or on its active item. */
export interface QuickPickButton {
    id: string;
    icon: string;
    tooltip: string;
}

export interface QuickPickItem {
    kind?: "item" | "separator";
    /** Needed by every item but a separator. */
    id?: string;
    label: string;
    description?: string;
    detail?: string;
    icon?: string;
    picked?: boolean;
    always_show?: boolean;
    /** Why the item can't be picked. */
    disabled?: string;
    buttons?: QuickPickButton[];
}

/** The params of `quick_pick.ask`, `quick_pick.open` and, with `pick`, `quick_pick.update`. */
export interface QuickPickSpec {
    title?: string;
    placeholder?: string;
    value?: string;
    items?: QuickPickItem[];
    many?: boolean;
    match_on?: ("description" | "detail")[];
    keep_order?: boolean;
    step?: { current: number; total: number };
    back?: boolean;
    buttons?: QuickPickButton[];
    filter?: "fuzzy" | "none";
    busy?: boolean;
    active?: string;
}

/** The params of `input_box.ask`, `input_box.open` and, with `box`, `input_box.update`. */
export interface InputBoxSpec {
    title?: string;
    prompt?: string;
    placeholder?: string;
    value?: string;
    selection?: { from: number; to: number };
    password?: boolean;
    validation?: { pattern?: string; message: string; min_length?: number; max_length?: number };
    step?: { current: number; total: number };
    back?: boolean;
    message?: { severity: "error" | "warning" | "info"; text: string } | null;
    busy?: boolean;
}

export interface QuickPickEvent {
    kind: "value" | "active" | "accept" | "button" | "back" | "hide";
    pick: string;
    value?: string;
    item?: string;
    items?: string[];
    button?: string;
}

export interface InputBoxEvent {
    kind: "value" | "accept" | "back" | "hide";
    box: string;
    value?: string;
}

export interface QuickPickParams {
    pick: string;
    event: QuickPickEvent;
}

export interface InputBoxParams {
    box: string;
    event: InputBoxEvent;
}

/** A snapshot of one Document, as an exporter gets it. */
export interface Snapshot {
    document: number;
    revision: number;
    title: string;
    format: string;
    lines: Line[];
    styles: Style[];
    info: { key: string; value: string }[];
}

export interface ExportParams {
    format: string;
    document: Snapshot;
    options: { frameRate?: number; video?: string; path?: string };
}

/** The file as text, or as base64 with `encoding: "base64"`. */
export interface ExportResult {
    data: string;
    encoding?: "utf8" | "base64";
}

export interface ImportParams {
    format: string;
    /** The file's bytes, in base64. */
    data: string;
    name: string;
}

/** Lines as `insert_line` fields, Styles as `set_style` fields with their `name`, and Script Info by key. */
export interface ImportResult {
    lines: { [field: string]: Json }[];
    styles?: { [field: string]: Json }[];
    info?: { [key: string]: string };
}

/** What HikariSub asks the program, and what it answers. */
export interface HostRequests {
    initialize: { params: InitializeParams; result: Record<string, never> };
    command: { params: CommandParams; result: Json };
    event: { params: EventParams; result: null };
    panel: { params: PanelParams; result: null };
    dialog: { params: DialogParams; result: null };
    quick_pick: { params: QuickPickParams; result: null };
    input_box: { params: InputBoxParams; result: null };
    export: { params: ExportParams; result: ExportResult };
    import: { params: ImportParams; result: ImportResult };
    shutdown: { params: Record<string, never>; result: null };
}

/** What HikariSub tells the program without asking: after `shutdown`, `exit`. */
export interface HostNotifications {
    exit: undefined;
}

/** The members a Lua plugin's `hikari` global has, as methods. */
export interface PluginMethods {
    "hikari/notify": { params: { text: string; level?: "info" | "warning" | "error" }; result: null };
    "hikari/settings.get": { params: { id: string }; result: Json };
    "hikari/settings.set": { params: { id: string; value: Json }; result: null };
    "hikari/subscribe": { params: { events: (keyof Events)[] }; result: null };
    "hikari/unsubscribe": { params: { events: (keyof Events)[] }; result: null };
    /** Takes the app-wide edit turn; every step until `hikari/edit.end` is one undo step. */
    "hikari/edit.begin": { params: { name: string }; result: null };
    "hikari/edit.end": { params: Record<string, never>; result: { steps: number } };
}

/** The plugin services, beyond the Hikari API operations. */
export type ServiceMethod =
    | `panels.${string}`
    | `dialogs.${string}`
    | `quick_pick.${string}`
    | `input_box.${string}`
    | `files.${string}`
    | "net.fetch"
    | `job.${string}`;

/** Every method the program may call, with its params and result. */
export type ProgramCalls = { [M in keyof Operations]: Operations[M] } & PluginMethods;

export type RequestId = number | string;

export interface Request<M extends string = string, P = Json> {
    jsonrpc: "2.0";
    id: RequestId;
    method: M;
    params?: P;
}

export interface Notification<M extends string = string, P = Json> {
    jsonrpc: "2.0";
    method: M;
    params?: P;
}

export interface Success<R = Json> {
    jsonrpc: "2.0";
    id: RequestId;
    result: R;
}

/** -32000 carries a Hikari API error as `{code, data}`; -32601 is an unknown method; -32602 breaks the schema, with `data.pointer`. */
export type ErrorCode = -32700 | -32600 | -32601 | -32602 | -32603 | -32000;

export interface RpcError {
    code: ErrorCode;
    message: string;
    data?: Json;
}

export interface Failure {
    jsonrpc: "2.0";
    id: RequestId | null;
    error: RpcError;
}

export type Response<R = Json> = Success<R> | Failure;

/** A request HikariSub sends. */
export type HostRequest = { [M in keyof HostRequests]: Request<M, HostRequests[M]["params"]> }[keyof HostRequests];

/** A request the program sends. */
export type ProgramRequest =
    | { [M in keyof ProgramCalls]: Request<M, ProgramCalls[M]["params"]> }[keyof ProgramCalls]
    | Request<ServiceMethod, { [key: string]: Json }>;
