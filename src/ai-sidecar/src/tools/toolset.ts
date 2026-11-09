import { ErrorCode } from "../protocol";
import { RpcError } from "../rpc";
import type { CommandsListResult, OperationName, Operations, VideoFrameResult } from "./api.gen";
import { kCatalogue, type Catalogue, type Entry, type Tool } from "./catalogue";
import { kDefaultImageBudget, type ImageBudget } from "./images";
import { pngFromBgra } from "./png";

export type Content = { type: "text"; text: string } | { type: "image"; data: string; mimeType: string };

/** MCP's CallToolResult. */
export interface CallToolResult {
    content: Content[];
    isError?: boolean;
}

/**
 * Tools from outside HA1 that search_tools finds as one more group, such as the web search service. A source with no
 * tools now is left out of search_tools.
 */
export interface ToolSource {
    readonly group: string;
    readonly description: string;
    tools(): Tool[];
    call(name: string, args: Record<string, unknown>): Promise<CallToolResult>;
}

export interface HikariCalls {
    call<O extends OperationName>(operation: O, params?: Operations[O]["params"]): Promise<Operations[O]["result"]>;
}

const kHints: Record<string, string> = {
    invalid_params: "Check the tool's parameters and call it again.",
    not_found: "Look the id up again, with documents_list, lines_find or lines_list.",
    stale_revision: "Read the Lines again and prepare the edits against the current revision, which documents_get gives.",
    busy: "Call it again once that write is done.",
    read_only: "Wait for the macro to finish, or work on another Document.",
    protected: "Work on another Document.",
    unknown_operation: "This HikariSub doesn't offer that tool.",
};

const kDocumentHint = "Work on the active Document, or ask the user to attach this one with @.";

function text(value: unknown): CallToolResult {
    return { content: [{ type: "text", text: JSON.stringify(value) }] };
}

function failure(message: string): CallToolResult {
    return { content: [{ type: "text", text: message }], isError: true };
}

function isObject(value: unknown): value is Record<string, unknown> {
    return typeof value === "object" && value !== null && !Array.isArray(value);
}

/** An error as the model reads it: what happened and what to do next. */
function worded(tool: string, error: unknown, catalogue: Catalogue): string {
    if (!(error instanceof RpcError))
        return `${tool} failed: ${error instanceof Error ? error.message : String(error)}`;
    if (error.code === ErrorCode.requestCancelled)
        return `${tool} was cancelled.`;
    if (error.code !== ErrorCode.hikariApi || !isObject(error.data))
        return `HikariSub couldn't run ${tool}: ${error.message}`;
    const code = String(error.data.code);
    const details = error.data.data;
    const hint = code === "not_granted" && isObject(details) && "document" in details ? kDocumentHint : kHints[code];
    const current = code === "stale_revision" && isObject(details) && typeof details.current === "number"
        ? `It is now at revision ${details.current}.` : undefined;
    return [catalogue.forModel(error.message), current, hint].filter(Boolean).join(" ");
}

function words(query: string): string[] {
    return query.toLowerCase().split(/[^\p{L}\p{N}]+/u).filter(Boolean);
}

/** A word in a tool's name counts for more than any number of words in its description. */
const kNameWeight = 100;

/** How many of `wanted` occur in `haystack`. */
function score(wanted: string[], haystack: string): number {
    const lower = haystack.toLowerCase().replaceAll("_", " ");
    return wanted.filter((word) => lower.includes(word)).length;
}

function frame(result: VideoFrameResult): CallToolResult {
    const png = pngFromBgra(result.width, result.height, Buffer.from(result.data, "base64"));
    const { frame, crop, width, height } = result;
    return {
        content: [
            { type: "text", text: JSON.stringify({ frame, crop, width, height }) },
            { type: "image", data: Buffer.from(png).toString("base64"), mimeType: "image/png" },
        ],
    };
}

type Rect = VideoFrameResult["crop"];

/** `ink` grown on every side by a quarter of its longer side, and by 32 px at least. */
function around(ink: Rect): Rect {
    const margin = Math.max(32, Math.ceil(Math.max(ink.width, ink.height) / 4));
    const x = Math.max(0, ink.x - margin), y = Math.max(0, ink.y - margin);
    return { x, y, width: ink.x + ink.width + margin - x, height: ink.y + ink.height + margin - y };
}

/**
 * Hikari's tools for one conversation: the core tools, plus those search_tools found. Every call goes into HA1
 * through `_hikari/api.call`, which validates it and scopes it; errors come back as text for the model.
 */
export class ToolSet {
    private readonly entries: readonly Entry[];
    private readonly offered: Set<string>;
    private readonly byName: Map<string, Entry>;
    /** The model's: the agent sets it from the provider's wire with kImageBudgets. */
    imageBudget: ImageBudget = kDefaultImageBudget;

    constructor(private readonly api: HikariCalls, private readonly catalogue: Catalogue = kCatalogue,
        private readonly sources: readonly ToolSource[] = []) {
        this.entries = catalogue.entries;
        this.offered = new Set(this.entries.filter((e) => e.group === "core").map((e) => e.tool.name));
        this.byName = new Map(this.entries.map((e) => [e.tool.name, e]));
    }

    /** The tools to offer now, or `all` of them. */
    list(all = false): Tool[] {
        const search = this.catalogue.searchToolsTool(
            this.liveSources().map(({ group, description }) => ({ group, description })));
        return [
            ...this.entries.filter((e) => all || this.offered.has(e.tool.name))
                .map((e) => (e.tool.name === "search_tools" ? search : e.tool)),
            ...this.sourceTools().filter(([tool]) => all || this.offered.has(tool.name)).map(([tool]) => tool),
        ];
    }

    has(name: string): boolean {
        return this.byName.has(name) || this.sourceOf(name) !== undefined;
    }

    async call(name: string, args: unknown): Promise<CallToolResult> {
        const entry = this.byName.get(name);
        const source = entry ? undefined : this.sourceOf(name);
        if (!entry && !source)
            return failure(`No tool is named "${name}". search_tools finds more tools.`);
        const params = args ?? {};
        if (!isObject(params))
            return failure(`${name} takes a JSON object of arguments.`);
        if (source)
            return source.call(name, params).catch((error: unknown) => failure(worded(name, error, this.catalogue)));
        try {
            if (name === "search_tools")
                return this.searchTools(params);
            if (name === "search_commands")
                return await this.searchCommands(params);
            if (entry!.operation === "video.frame")
                return await this.videoFrame(name, params);
            return text(await this.api.call(entry!.operation!, params as never));
        } catch (error) {
            return failure(worded(name, error, this.catalogue));
        }
    }

    private async videoFrame(name: string, params: Record<string, unknown>): Promise<CallToolResult> {
        const { full, ...asked } = params;
        if (full !== undefined && typeof full !== "boolean")
            return failure(`${name}: full must be true or false.`);
        const call: Record<string, unknown> = { ...asked, maxPixels: this.imageBudget.maxPixels };
        if (asked.maxEdge === undefined || typeof asked.maxEdge === "number")
            call.maxEdge = Math.min((asked.maxEdge as number | undefined) ?? 1024, this.imageBudget.maxEdge);
        if (!full && asked.crop === undefined) {
            const where = Object.fromEntries(["frame", "document", "line"].filter((key) => key in asked).map((key) => [key, asked[key]]));
            const ink = await this.api.call("video.ink_bounds", where as never).catch((error: unknown) => {
                if (error instanceof RpcError && isObject(error.data) && error.data.code === "unavailable")
                    return undefined;
                throw error;
            });
            if (ink?.bounds) {
                call.frame = ink.frame;
                call.crop = around(ink.bounds);
            }
        }
        return frame(await this.api.call("video.frame", call as never));
    }

    private searchTools(params: Record<string, unknown>): CallToolResult {
        const { query, group } = params;
        if (query !== undefined && (typeof query !== "string" || query.length === 0))
            return failure("search_tools: query must be words to look for.");
        if (group !== undefined && (typeof group !== "string" || !this.groups().includes(group)))
            return failure(`search_tools: group must be one of ${this.groups().join(", ")}.`);
        if (query === undefined && group === undefined)
            return failure("search_tools needs a query, a group or both.");
        const wanted = words((query as string | undefined) ?? "");
        const candidates: [Tool, string][] = [
            ...this.entries.filter((e) => e.group !== "core").map((e): [Tool, string] => [e.tool, e.group]),
            ...this.sourceTools().map(([tool, source]): [Tool, string] => [tool, source.group]),
        ];
        const found = candidates
            .filter(([, g]) => group === undefined || g === group)
            .map(([tool, g]): [Tool, number] => [tool, score(wanted, tool.name) * kNameWeight +
                score(wanted, `${tool.name} ${tool.description} ${g}`)])
            .filter(([, s]) => wanted.length === 0 || s > 0)
            .sort(([a, sa], [b, sb]) => sb - sa || a.name.localeCompare(b.name))
            .slice(0, 10)
            .map(([tool]) => tool);
        if (found.length === 0)
            return failure(`No tool matches. The groups are ${this.groups().join(", ")}.`);
        for (const tool of found)
            this.offered.add(tool.name);
        return text({ tools: found.map(({ name, description, inputSchema }) => ({ name, description, inputSchema })) });
    }

    private async searchCommands(params: Record<string, unknown>): Promise<CallToolResult> {
        const { query, window, limit = 20 } = params;
        if (typeof query !== "string" || words(query).length === 0)
            return failure("search_commands: query must be words to look for, such as \"save\".");
        if (typeof limit !== "number" || !Number.isInteger(limit) || limit < 1 || limit > 100)
            return failure("search_commands: limit must be an integer from 1 to 100.");
        const listed: CommandsListResult = await this.api.call("commands.list",
            window === undefined ? {} : { window } as Operations["commands.list"]["params"]);
        const wanted = words(query);
        const matches = listed.commands.filter((c) => score(wanted, `${c.command} ${c.name}`) === wanted.length);
        return text({ commands: matches.slice(0, limit), total: matches.length });
    }

    private groups(): string[] {
        return [...[...new Set(this.entries.map((e) => e.group as string))].filter((g) => g !== "core"),
            ...this.liveSources().map((s) => s.group)];
    }

    private liveSources(): ToolSource[] {
        return this.sources.filter((s) => s.tools().length > 0);
    }

    private sourceTools(): [Tool, ToolSource][] {
        return this.sources.flatMap((source) => source.tools().map((tool): [Tool, ToolSource] => [tool, source]));
    }

    private sourceOf(name: string): ToolSource | undefined {
        return this.sourceTools().find(([tool]) => tool.name === name)?.[1];
    }
}
