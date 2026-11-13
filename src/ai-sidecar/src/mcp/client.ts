import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StreamableHTTPClientTransport } from "@modelcontextprotocol/sdk/client/streamableHttp.js";
import { ReadBuffer, serializeMessage } from "@modelcontextprotocol/sdk/shared/stdio.js";
import type { Transport } from "@modelcontextprotocol/sdk/shared/transport.js";
import { ToolListChangedNotificationSchema, type JSONRPCMessage } from "@modelcontextprotocol/sdk/types.js";

import type { RelayedProcess } from "../agents/relay";
import { Method, type McpAdmitParams, type McpServerConfig, type McpServerStatusParams } from "../protocol";
import type { Relay } from "../providers/relay";
import type { Tool } from "../tools/catalogue";
import type { JsonSchema } from "../tools/schema";
import type { CallToolResult, Content, ToolSource } from "../tools/toolset";

/** Qt's agent name for a user-added stdio server. */
export function mcpAgent(server: string): string {
    return `mcp:${server}`;
}

/** Stands in for the server's URL, which only Qt knows. */
export function mcpPlaceholderUrl(server: string): URL {
    return new URL(`https://${server}.mcp.invalid/`);
}

const kNameLimit = 64;

/** `<server>__<tool>`, in the characters and length every provider takes; a cut name ends in a hash of the whole. */
export function namespacedTool(server: string, tool: string): string {
    const name = `${server}__${tool}`.replace(/[^A-Za-z0-9_-]/g, "_");
    if (name.length <= kNameLimit)
        return name;
    const hash = Bun.hash(`${server}\0${tool}`).toString(36).slice(0, 8);
    return `${name.slice(0, kNameLimit - hash.length - 1)}_${hash}`;
}

/** A Qt-started server's stdio, one JSON-RPC message per line. */
export class RelayedStdio implements Transport {
    onclose?: () => void;
    onerror?: (error: Error) => void;
    onmessage?: (message: JSONRPCMessage) => void;
    private process?: RelayedProcess;
    private readonly buffer = new ReadBuffer();
    private closed = false;

    constructor(private readonly spawn: () => RelayedProcess) {}

    async start(): Promise<void> {
        const process = this.process = this.spawn();
        process.stdout.on("data", (chunk: Buffer) => {
            this.buffer.append(chunk);
            for (;;) {
                let message: JSONRPCMessage | null;
                try {
                    message = this.buffer.readMessage();
                } catch (error) {
                    this.onerror?.(error as Error);
                    continue;
                }
                if (!message)
                    break;
                this.onmessage?.(message);
            }
        });
        process.on("error", (error: Error) => {
            this.onerror?.(error);
            this.end();
        });
        process.on("exit", () => this.end());
    }

    send(message: JSONRPCMessage): Promise<void> {
        const process = this.process;
        if (!process || this.closed)
            return Promise.reject(new Error("The MCP server isn't running"));
        return new Promise((resolve, reject) =>
            process.stdin.write(serializeMessage(message), (error) => (error ? reject(error) : resolve())));
    }

    async close(): Promise<void> {
        this.process?.kill();
        this.end();
    }

    private end(): void {
        if (this.closed)
            return;
        this.closed = true;
        this.onclose?.();
    }
}

/** The MCP SDK's fetch for one HTTP server: through Qt's relay, every status handed back as it came. */
export function mcpFetch(relay: Relay, server: string): typeof fetch {
    const relayed = async (input: string | URL | Request, init?: RequestInit): Promise<Response> => {
        const request = input instanceof Request ? new Request(input, init) : new Request(input.toString(), init);
        const body = request.body ? new Uint8Array(await request.arrayBuffer()) : undefined;
        const response = await relay.send({ target: { kind: "mcp", id: server }, method: request.method,
            url: request.url, headers: [...request.headers], body }, init?.signal ?? request.signal);
        const empty = [101, 103, 202, 204, 205, 304].includes(response.status) || request.method === "HEAD";
        return new Response(empty ? null : response.body, { status: response.status, headers: response.headers });
    };
    return relayed as typeof fetch;
}

type McpContent = { type: string; text?: string; data?: string; mimeType?: string; [key: string]: unknown };

function content(items: McpContent[] | undefined, structured: unknown): Content[] {
    const out = (items ?? []).map((item): Content => {
        if (item.type === "text" && typeof item.text === "string")
            return { type: "text", text: item.text };
        if (item.type === "image" && typeof item.data === "string" && typeof item.mimeType === "string")
            return { type: "image", data: item.data, mimeType: item.mimeType };
        const { data: _data, ...rest } = item;
        return { type: "text", text: JSON.stringify(rest) };
    });
    if (out.length === 0 && structured !== undefined)
        out.push({ type: "text", text: JSON.stringify(structured) });
    return out;
}

export interface Connector {
    stdio(server: string): RelayedProcess;
    relay: Relay;
    /** Qt's answer to `_hikari/mcpServers.admit`; rejects when Qt refuses. */
    admit(params: McpAdmitParams): Promise<unknown>;
    status(params: McpServerStatusParams): void;
    /** The sidecar's own name and version, for `initialize`. */
    info: { name: string; version: string };
}

interface ServerTool {
    tool: Tool;
    original: string;
}

/** One user-added server: connected on first use, and again after it fails or its settings change. */
class Connection {
    private client?: Client;
    private connecting?: Promise<void>;
    private tools: ServerTool[] = [];
    private closed = false;

    constructor(readonly config: McpServerConfig, private readonly connector: Connector) {}

    get group(): string {
        return `mcp_${this.config.id}`;
    }

    list(): Tool[] {
        return this.tools.map((t) => t.tool);
    }

    ensure(): Promise<void> {
        if (this.client || this.closed)
            return Promise.resolve();
        return this.connecting ??= this.connect().finally(() => (this.connecting = undefined));
    }

    async call(sessionId: string, name: string, args: Record<string, unknown>): Promise<CallToolResult> {
        const found = this.tools.find((t) => t.tool.name === name);
        if (!found || !this.client)
            return { content: [{ type: "text", text: `${name} isn't available: its MCP server isn't connected.` }], isError: true };
        await this.connector.admit({ sessionId, server: this.config.id, tool: found.original, arguments: args });
        const result = await this.client.callTool({ name: found.original, arguments: args });
        return { content: content(result.content as McpContent[] | undefined, result.structuredContent),
            ...(result.isError ? { isError: true } : {}) };
    }

    async close(): Promise<void> {
        this.closed = true;
        const client = this.client;
        this.client = undefined;
        this.tools = [];
        await client?.close().catch(() => {});
    }

    private async connect(): Promise<void> {
        const { id } = this.config;
        this.connector.status({ server: id, state: "connecting" });
        const client = new Client(this.connector.info, { capabilities: {} });
        const transport = this.config.transport === "stdio"
            ? new RelayedStdio(() => this.connector.stdio(id))
            : new StreamableHTTPClientTransport(mcpPlaceholderUrl(id), { fetch: mcpFetch(this.connector.relay, id) });
        client.onclose = () => {
            if (this.client !== client)
                return;
            this.client = undefined;
            this.tools = [];
            if (!this.closed)
                this.connector.status({ server: id, state: "failed", error: "The server stopped" });
        };
        try {
            await client.connect(transport);
            if (this.closed) {
                await client.close();
                return;
            }
            this.client = client;
            this.tools = await this.fetchTools(client);
            client.setNotificationHandler(ToolListChangedNotificationSchema, async () => {
                if (this.client === client)
                    this.tools = await this.fetchTools(client).catch(() => this.tools);
            });
            this.connector.status({ server: id, state: "ready", tools: this.tools.length });
        } catch (error) {
            this.client = undefined;
            await client.close().catch(() => {});
            this.connector.status({ server: id, state: "failed",
                error: error instanceof Error ? error.message : String(error) });
        }
    }

    private async fetchTools(client: Client): Promise<ServerTool[]> {
        const tools: ServerTool[] = [];
        let cursor: string | undefined;
        do {
            const page = await client.listTools(cursor === undefined ? {} : { cursor });
            for (const t of page.tools)
                tools.push({ original: t.name, tool: {
                    name: namespacedTool(this.config.id, t.name),
                    description: [t.title ?? t.annotations?.title, t.description].filter(Boolean).join(": "),
                    inputSchema: t.inputSchema as JsonSchema,
                    annotations: { readOnlyHint: t.annotations?.readOnlyHint === true },
                    _meta: { "hikari/group": this.group },
                } });
            cursor = page.nextCursor;
        } while (cursor);
        return tools;
    }
}

/**
 * The servers the user added in Settings › AI. Qt starts each stdio server and relays every HTTP request; nothing
 * starts until a session asks.
 */
export class McpClients {
    private readonly connections = new Map<string, Connection>();

    constructor(private readonly connector: Connector) {}

    get size(): number {
        return this.connections.size;
    }

    async set(servers: McpServerConfig[]): Promise<void> {
        const wanted = new Map(servers.map((s) => [s.id, s]));
        const stale = [...this.connections.values()].filter((c) => {
            const next = wanted.get(c.config.id);
            return !next || next.revision !== c.config.revision || next.transport !== c.config.transport
                || next.name !== c.config.name;
        });
        for (const connection of stale)
            this.connections.delete(connection.config.id);
        for (const server of servers)
            if (!this.connections.has(server.id))
                this.connections.set(server.id, new Connection(server, this.connector));
        await Promise.all(stale.map((c) => c.close()));
    }

    /** Connects every server not yet connected, and waits for them. */
    ensure(): Promise<void> {
        return Promise.all([...this.connections.values()].map((c) => c.ensure())).then(() => {});
    }

    /** One tool group per server, whose calls Qt admits for `sessionId`. */
    sources(sessionId: string): ToolSource[] {
        return [...this.connections.values()].map((connection): ToolSource => ({
            group: connection.group,
            description: serverGroupDescription(connection.config.name),
            tools: () => connection.list(),
            call: (name, args) => connection.call(sessionId, name, args),
        }));
    }

    closeAll(): Promise<void> {
        const all = [...this.connections.values()];
        this.connections.clear();
        return Promise.all(all.map((c) => c.close())).then(() => {});
    }
}

export const kServerNameShown = 40;

export function serverGroupDescription(name: string): string {
    return `Tools from the MCP server "${[...name].slice(0, kServerNameShown).join("")}"`;
}

export function connector(peer: { request(method: string, params?: unknown): Promise<unknown> }, stdio: Connector["stdio"], relay: Relay,
info: Connector["info"]): Connector {
    return {
        stdio, relay, info,
        admit: (params) => peer.request(Method.mcpAdmit, params),
        status: (params) => void peer.request(Method.mcpServerStatus, params).catch(() => {}),
    };
}
