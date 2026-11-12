import type { OperationName } from "./api.gen";
import reference from "./api.json";
import { inline, type JsonSchema } from "./schema";

/** The core tools are always offered; the others come through search_tools. */
export const kGroups = {
    core: "Documents, Lines, Styles, the selection, video and audio facts, granted files and commands.",
    typeset: "Typesetting: video frames as images, ink bounds, colours and keyframes; solvers that write the tags for " +
        "a quad, clip, drawing, shift or Hydra change; a render check before applying.",
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
    /** When the tool takes other parameters than HA1's. */
    params?: (schema: JsonSchema) => JsonSchema;
}

/** video.frame's parameters, less maxPixels, which the image budget sets, and with `full`. */
function frameParams(schema: JsonSchema): JsonSchema {
    const { maxPixels: _, ...properties } = schema.properties as Record<string, JsonSchema>;
    return {
        ...schema,
        properties: {
            ...properties,
            maxEdge: { ...properties.maxEdge, description: "Default 1024, and never more than the model takes." },
            full: {
                type: "boolean",
                description: "The whole frame. Without it or crop, the frame is cropped around the ink of line, or of " +
                    "every Line, with a margin, and whole when nothing is drawn.",
            },
        },
    };
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
        description: "One video frame as a PNG image, with every Line, one Line or none drawn on it, cropped around " +
            "the ink unless full or crop is given. Image point (x, y) is video point " +
            "(crop.x + x * crop.width / width, crop.y + y * crop.height / height).",
        params: frameParams,
    },
    "video.ink_bounds": { group: "typeset" },
    "video.colours": { group: "typeset" },
    "video.keyframes": { group: "typeset" },
    "typeset.perspective": { group: "typeset" },
    "typeset.clip": { group: "typeset" },
    "typeset.drawing": { group: "typeset" },
    "typeset.shift": { group: "typeset" },
    "typeset.hydra": { group: "typeset" },
    "typeset.check": { group: "typeset" },
    "typeset.apply": { group: "typeset" },
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

/**
 * Operations no tool offers as they are: search_commands wraps commands.list, read_skill the skills, and the sidecar
 * knows the version.
 */
const kUnoffered: readonly OperationName[] = ["commands.list", "skills.list", "skills.read", "api.version"];

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

/** HA1's reference, as `api.json` holds it or the app sends it in `initialize`. */
export interface Reference {
    version?: number;
    $defs: Record<string, JsonSchema>;
    operations: Operation[];
}

export const kOwnReference = reference as unknown as Reference;

export function tool(name: string, description: string, inputSchema: JsonSchema, access: "read" | "write",
    group: string): Tool {
    return { name, description, inputSchema, annotations: { readOnlyHint: access === "read" }, _meta: { "hikari/group": group } };
}

function entry(name: string, description: string, inputSchema: JsonSchema, access: "read" | "write", group: Group,
    operation?: OperationName): Entry {
    return { tool: tool(name, description, inputSchema, access, group), group, operation };
}

export interface ExtraGroup {
    group: string;
    description: string;
}

/** The tools one app offers: an operation that app's reference lacks is never offered. */
export class Catalogue {
    readonly entries: readonly Entry[];
    private readonly operations: Map<string, Operation>;
    private readonly toolNames: Map<string, string>;

    constructor(private readonly reference: Reference) {
        this.operations = new Map(reference.operations.map((op) => [op.name, op]));
        this.toolNames = new Map<string, string>([
            ...reference.operations.flatMap((op): [string, string][] => {
                const placement = kPlacements[op.name];
                return placement ? [[op.name, placement.name ?? op.name.replaceAll(".", "_")]] : [];
            }),
            ["commands.list", "search_commands"],
        ]);
        this.entries = this.build();
    }

    /** HA1's text with each operation it names given as the tool that offers it. */
    forModel(text: string): string {
        return text.replace(/\b[a-z]+(?:\.[a-z_]+)+\b/g, (name) => this.toolNames.get(name) ?? name);
    }

    /** HA1's operations that no tool offers and that aren't meant to be left out. */
    unplacedOperations(): string[] {
        return [...this.operations.keys()].filter((name) =>
            !kPlacements[name as OperationName] && !kUnoffered.includes(name as OperationName));
    }

    private schema(params: JsonSchema): JsonSchema {
        const inlined = inline(params, this.reference.$defs, (text) => this.forModel(text));
        return inlined.properties ? inlined : { ...inlined, properties: {} };
    }

    private searchable(): Group[] {
        return (Object.keys(kGroups) as Group[]).filter((group) => group !== "core" &&
            [...this.operations.keys()].some((name) => kPlacements[name as OperationName]?.group === group));
    }

    /** search_tools, listing the catalogue's searchable groups and then `extra`. */
    searchToolsTool(extra: readonly ExtraGroup[] = []): Tool {
        const all = [...this.searchable().map((group) => ({ group, description: kGroups[group] as string })), ...extra];
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

    private searchTools(): Entry {
        return { tool: this.searchToolsTool(), group: "core" };
    }

    private searchCommands(list: Operation): Entry {
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

    private build(): Entry[] {
        const entries = [this.searchTools()];
        const list = this.operations.get("commands.list");
        if (list)
            entries.push(this.searchCommands(list));
        for (const op of this.reference.operations) {
            const placement = kPlacements[op.name];
            if (placement && op.deprecatedIn === undefined)
                entries.push(entry(this.toolNames.get(op.name)!, placement.description ?? this.forModel(op.summary),
                    (placement.params ?? ((schema) => schema))(this.schema(op.params)), op.access, placement.group, op.name));
        }
        return entries;
    }
}

export const kCatalogue = new Catalogue(kOwnReference);
