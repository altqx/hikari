import type { OperationName } from "./api.gen";
import reference from "./api.json";
import { inline, type JsonSchema } from "./schema";

/** The core tools are always offered; the others come through search_tools. */
export const kGroups = {
    core: "Documents, Lines, Styles, the selection, video and audio facts, granted files and commands.",
    typeset: "Typesetting: video frames as images, signs, positions and clips.",
    translate: "Translation passes, the glossary, characters and tag rules.",
    karaoke: "Karaoke timing: audio peaks, syllables and onsets.",
    automation: "Automation macros and AI-written Lua.",
    effects: "The node-effect editor's node types and graphs.",
    plugins: "Installed plugins and what they offer.",
    app: "Workspace panels and settings.",
} as const;
export type Group = keyof typeof kGroups;

interface Placement {
    group: Group;
    /** The tool's name when it isn't the operation's with "." as "_". */
    name?: string;
    /** When the tool's answer isn't HA1's result as it is. */
    description?: string;
}

/** Where each HA1 operation is offered. An operation added to HA1 is offered once it is placed here. */
const kPlacements: Partial<Record<OperationName, Placement>> = {
    "documents.list": { group: "core" },
    "documents.get": { group: "core" },
    "lines.list": { group: "core" },
    "lines.get": { group: "core" },
    "lines.find": { group: "core" },
    "styles.list": { group: "core" },
    "info.list": { group: "core" },
    "selection.get": { group: "core" },
    "selection.set": { group: "core" },
    "document.apply": { group: "core" },
    "video.info": { group: "core" },
    "video.frame_at_time": { group: "core" },
    "video.time_of_frame": { group: "core" },
    "audio.info": { group: "core" },
    "files.list": { group: "core" },
    "files.read": { group: "core" },
    "files.write": { group: "core" },
    "commands.run": { group: "core", name: "run_command" },
    "video.frame": {
        group: "typeset",
        description: "One video frame as a PNG image, optionally cropped, scaled to fit maxEdge.",
    },
    "audio.peaks": { group: "karaoke" },
    "nodes.catalogue": { group: "effects" },
    "nodes.validate": { group: "effects" },
    "panels.list": { group: "app" },
    "panels.open": { group: "app" },
    "panels.close": { group: "app" },
    "panels.focus": { group: "app" },
    "settings.list": { group: "app" },
    "settings.get": { group: "app" },
    "settings.set": { group: "app" },
};

/** Operations no tool offers as they are: search_commands wraps commands.list, and the sidecar knows the version. */
const kUnoffered: readonly OperationName[] = ["commands.list", "api.version"];

/** MCP's Tool, as `tools/list` gives it. */
export interface Tool {
    name: string;
    description: string;
    inputSchema: JsonSchema;
    annotations: { readOnlyHint: boolean };
    /** A catalogue group, or a ToolSource's. */
    _meta: { "hikari/group": string };
}

export interface Entry {
    tool: Tool;
    group: Group;
    /** Absent for the tools the sidecar answers itself. */
    operation?: OperationName;
}

interface Operation {
    name: OperationName;
    access: "read" | "write";
    summary: string;
    deprecatedIn?: number;
    params: JsonSchema;
}

const kOperations = reference.operations as unknown as Operation[];
const kDefs = reference.$defs as Record<string, JsonSchema>;

const kToolNames = new Map<string, string>([
    ...kOperations.flatMap((op): [string, string][] => {
        const placement = kPlacements[op.name];
        return placement ? [[op.name, placement.name ?? op.name.replaceAll(".", "_")]] : [];
    }),
    ["commands.list", "search_commands"],
]);

/** HA1's text with each operation it names given as the tool that offers it. */
export function forModel(text: string): string {
    return text.replace(/\b[a-z]+(?:\.[a-z_]+)+\b/g, (name) => kToolNames.get(name) ?? name);
}

function schema(params: JsonSchema): JsonSchema {
    const inlined = inline(params, kDefs, forModel);
    return inlined.properties ? inlined : { ...inlined, properties: {} };
}

export function tool(name: string, description: string, inputSchema: JsonSchema, access: "read" | "write",
    group: string): Tool {
    return { name, description, inputSchema, annotations: { readOnlyHint: access === "read" }, _meta: { "hikari/group": group } };
}

function entry(name: string, description: string, inputSchema: JsonSchema, access: "read" | "write", group: Group,
    operation?: OperationName): Entry {
    return { tool: tool(name, description, inputSchema, access, group), group, operation };
}

function operation(name: OperationName): Operation {
    const op = kOperations.find((o) => o.name === name);
    if (!op)
        throw new Error(`HA1 has no ${name}`);
    return op;
}

const kSearchable = (Object.keys(kGroups) as Group[]).filter((group) =>
    group !== "core" && Object.values(kPlacements).some((p) => p.group === group));

export interface ExtraGroup {
    group: string;
    description: string;
}

/** search_tools, listing the catalogue's searchable groups and then `extra`. */
export function searchToolsTool(extra: readonly ExtraGroup[] = []): Tool {
    const all = [...kSearchable.map((group) => ({ group, description: kGroups[group] as string })), ...extra];
    const groups = all.map(({ group, description }) => `${group} (${description})`).join("; ");
    return tool("search_tools",
        `Finds more tools by words or by group and offers them from then on. Groups: ${groups}.`,
        {
            type: "object",
            properties: {
                query: { type: "string", minLength: 1, description: "Words to look for, such as \"video frame\"." },
                group: { enum: all.map((g) => g.group), description: "Every tool of one group." },
            },
            additionalProperties: false,
        }, "read", "core");
}

function searchTools(): Entry {
    return { tool: searchToolsTool(), group: "core" };
}

function searchCommands(): Entry {
    const list = operation("commands.list");
    const window = (list.params.properties as Record<string, JsonSchema>).window!;
    return entry("search_commands",
        "Finds HikariSub's commands, the actions of menus, toolbars and shortcuts, by words in their id or name, with " +
        "whether each is enabled now and its keys. run_command runs one.",
        {
            type: "object",
            properties: {
                query: { type: "string", minLength: 1, description: "Words to look for, such as \"save subtitles\"." },
                window,
                limit: { type: "integer", minimum: 1, maximum: 100, description: "20 when omitted." },
            },
            required: ["query"],
            additionalProperties: false,
        }, list.access, "core", "commands.list");
}

function catalogue(): Entry[] {
    const entries = [searchTools(), searchCommands()];
    for (const op of kOperations) {
        const placement = kPlacements[op.name];
        if (placement && op.deprecatedIn === undefined)
            entries.push(entry(kToolNames.get(op.name)!, placement.description ?? forModel(op.summary), schema(op.params),
                op.access, placement.group, op.name));
    }
    return entries;
}

export const kCatalogue: readonly Entry[] = catalogue();

/** HA1's operations that no tool offers and that aren't meant to be left out. */
export function unplacedOperations(): string[] {
    return kOperations.map((op) => op.name).filter((name) => !kPlacements[name] && !kUnoffered.includes(name));
}
