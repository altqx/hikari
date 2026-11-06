import { ErrorCode } from "../protocol";
import { RpcError } from "../rpc";
import type { CommandsListResult, OperationName, Operations, VideoFrameResult } from "./api.gen";
import { kCatalogue, kGroups, type Catalogue, type Entry, type Group, type Tool } from "./catalogue";
import { pngFromBgra } from "./png";

export type Content = { type: "text"; text: string } | { type: "image"; data: string; mimeType: string };

/** MCP's CallToolResult. */
export interface CallToolResult {
    content: Content[];
    isError?: boolean;
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

/** How many of `wanted` occur in `haystack`. */
function score(wanted: string[], haystack: string): number {
    const lower = haystack.toLowerCase().replaceAll("_", " ");
    return wanted.filter((word) => lower.includes(word)).length;
}

function frame(result: VideoFrameResult): CallToolResult {
    const png = pngFromBgra(result.width, result.height, Buffer.from(result.data, "base64"));
    return {
        content: [
            { type: "text", text: JSON.stringify({ frame: result.frame, width: result.width, height: result.height }) },
            { type: "image", data: Buffer.from(png).toString("base64"), mimeType: "image/png" },
        ],
    };
}

/**
 * Hikari's tools for one conversation: the core tools, plus those search_tools found. Every call goes into HA1
 * through `_hikari/api.call`, which validates it and scopes it; errors come back as text for the model.
 */
export class ToolSet {
    private readonly entries: readonly Entry[];
    private readonly offered: Set<string>;
    private readonly byName: Map<string, Entry>;

    constructor(private readonly api: HikariCalls, private readonly catalogue: Catalogue = kCatalogue) {
        this.entries = catalogue.entries;
        this.offered = new Set(this.entries.filter((e) => e.group === "core").map((e) => e.tool.name));
        this.byName = new Map(this.entries.map((e) => [e.tool.name, e]));
    }

    /** The tools to offer now, or `all` of them. */
    list(all = false): Tool[] {
        return this.entries.filter((e) => all || this.offered.has(e.tool.name)).map((e) => e.tool);
    }

    has(name: string): boolean {
        return this.byName.has(name);
    }

    async call(name: string, args: unknown): Promise<CallToolResult> {
        const entry = this.byName.get(name);
        if (!entry)
            return failure(`No tool is named "${name}". search_tools finds more tools.`);
        const params = args ?? {};
        if (!isObject(params))
            return failure(`${name} takes a JSON object of arguments.`);
        try {
            if (name === "search_tools")
                return this.searchTools(params);
            if (name === "search_commands")
                return await this.searchCommands(params);
            const result = await this.api.call(entry.operation!, params as never);
            return entry.operation === "video.frame" ? frame(result as VideoFrameResult) : text(result);
        } catch (error) {
            return failure(worded(name, error, this.catalogue));
        }
    }

    private searchTools(params: Record<string, unknown>): CallToolResult {
        const { query, group } = params;
        if (query !== undefined && (typeof query !== "string" || query.length === 0))
            return failure("search_tools: query must be words to look for.");
        if (group !== undefined && (typeof group !== "string" || group === "core" || !(group in kGroups)))
            return failure(`search_tools: group must be one of ${this.groups().join(", ")}.`);
        if (query === undefined && group === undefined)
            return failure("search_tools needs a query, a group or both.");
        const wanted = words((query as string | undefined) ?? "");
        const found = this.entries
            .filter((e) => e.group !== "core" && (group === undefined || e.group === group))
            .map((e): [Entry, number] => [e, score(wanted, `${e.tool.name} ${e.tool.description} ${e.group}`)])
            .filter(([, s]) => wanted.length === 0 || s > 0)
            .sort(([a, sa], [b, sb]) => sb - sa || a.tool.name.localeCompare(b.tool.name))
            .slice(0, 10)
            .map(([e]) => e.tool);
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

    private groups(): Group[] {
        return [...new Set(this.entries.map((e) => e.group))].filter((g) => g !== "core");
    }
}
