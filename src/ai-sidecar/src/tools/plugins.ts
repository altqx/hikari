import { Method, type PluginToolConfig, type PluginToolsCallParams, type PluginToolsCallResult } from "../protocol";
import type { Tool } from "./catalogue";
import type { JsonSchema } from "./schema";
import type { ToolSource } from "./toolset";

type Request = (method: string, params?: unknown) => Promise<unknown>;

const kGroup = "plugin_tools";

/** The plugins' AI tools, as Qt lists them. Qt checks, gates and runs every call. */
export class PluginTools {
    private tools: Tool[] = [];

    constructor(private readonly request: Request) {}

    set(tools: PluginToolConfig[]): void {
        this.tools = tools.map((t) => ({
            name: t.name,
            description: t.description,
            inputSchema: t.inputSchema as JsonSchema,
            annotations: { readOnlyHint: t.readOnly },
            _meta: { "hikari/group": kGroup },
        }));
    }

    readOnly(name: string): boolean {
        return this.tools.some((t) => t.name === name && t.annotations.readOnlyHint);
    }

    /** Every tool, called for `sessionId`. */
    source(sessionId: string): ToolSource {
        return this.sourceOf(() => this.tools, sessionId);
    }

    /** The read-only tools only, for HikariSub's MCP server, which refuses writes. */
    readOnlySource(): ToolSource {
        return this.sourceOf(() => this.tools.filter((t) => t.annotations.readOnlyHint));
    }

    private sourceOf(tools: () => Tool[], sessionId?: string): ToolSource {
        return {
            group: kGroup,
            description: "Tools from the plugins you installed",
            tools,
            call: async (name, args) => {
                const params: PluginToolsCallParams = { ...(sessionId === undefined ? {} : { sessionId }), tool: name, arguments: args };
                const { result } = await this.request(Method.pluginToolsCall, params) as PluginToolsCallResult;
                return { content: [{ type: "text", text: JSON.stringify(result ?? null) }] };
            },
        };
    }
}

export function checkedPluginTools(params: unknown): PluginToolConfig[] | undefined {
    const tools = (params as { tools?: unknown } | null)?.tools;
    const valid = Array.isArray(tools) && tools.every((t: Partial<PluginToolConfig>) =>
        typeof t === "object" && t !== null && typeof t.name === "string" && /^[A-Za-z0-9_-]{1,64}$/.test(t.name)
        && typeof t.description === "string" && typeof t.inputSchema === "object" && t.inputSchema !== null
        && typeof t.readOnly === "boolean");
    return valid ? tools as PluginToolConfig[] : undefined;
}
