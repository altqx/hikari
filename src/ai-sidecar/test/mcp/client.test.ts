import { describe, expect, test } from "bun:test";
import { EventEmitter } from "node:events";
import { PassThrough, Writable } from "node:stream";

import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { ReadBuffer, serializeMessage } from "@modelcontextprotocol/sdk/shared/stdio.js";
import type { Transport } from "@modelcontextprotocol/sdk/shared/transport.js";
import type { JSONRPCMessage } from "@modelcontextprotocol/sdk/types.js";
import { z } from "zod";

import type { RelayedProcess } from "../../src/agents/relay";
import { type Connector, kServerNameShown, McpClients, mcpFetch, namespacedTool, serverGroupDescription } from "../../src/mcp/client";
import type { McpAdmitParams, McpServerConfig, McpServerStatusParams } from "../../src/protocol";
import type { RelayRequest } from "../../src/providers/relay";
import { RpcError } from "../../src/rpc";
import { ToolSet } from "../../src/tools/toolset";
import { answer, resultOf, scriptedTurn, toolNames, toolUse } from "../agent/scripted";
import { Harness as ProviderHarness } from "../providers/harness";

const provider = new ProviderHarness().install();

/** A stdio server as Qt would relay it: the far end of the pipes runs an MCP SDK server in this process. */
class FakeProcess extends EventEmitter {
    readonly stdout = new PassThrough();
    readonly stderr = new PassThrough();
    readonly stdin: Writable;
    killed = false;
    readonly calls: string[] = [];

    constructor(build: (server: McpServer) => void) {
        super();
        const process = this;
        const buffer = new ReadBuffer();
        const transport: Transport = {
            async start() {},
            async send(message: JSONRPCMessage) {
                process.stdout.write(serializeMessage(message));
            },
            async close() {},
        };
        this.stdin = new Writable({
            write(chunk: Buffer, _encoding, done) {
                buffer.append(chunk);
                for (let m = buffer.readMessage(); m; m = buffer.readMessage()) {
                    if ("method" in m && m.method === "tools/call")
                        process.calls.push(String((m.params as { name: string }).name));
                    transport.onmessage?.(m);
                }
                done();
            },
        });
        const server = new McpServer({ name: "fake", version: "1.0.0" });
        build(server);
        void server.connect(transport);
    }

    kill(): boolean {
        this.killed = true;
        this.exit();
        return true;
    }

    exit(): void {
        this.stdout.end();
        this.emit("exit", 0, null);
    }
}

function notes(server: McpServer): void {
    server.registerTool("read_note", { description: "Reads a note", inputSchema: { id: z.string() },
        annotations: { readOnlyHint: true } }, ({ id }) => ({ content: [{ type: "text", text: `note ${id}` }] }));
    server.registerTool("delete.note", { description: "Deletes a note", inputSchema: { id: z.string() },
        annotations: { destructiveHint: true } }, () => ({ content: [{ type: "text", text: "deleted" }] }));
}

interface Harness {
    clients: McpClients;
    spawned: FakeProcess[];
    admits: McpAdmitParams[];
    statuses: McpServerStatusParams[];
    relayed: RelayRequest[];
    refuse: { now: boolean };
}

function harness(build: (server: McpServer) => void = notes): Harness {
    const h: Harness = { spawned: [], admits: [], statuses: [], relayed: [], refuse: { now: false },
        clients: undefined as never };
    const connector: Connector = {
        stdio: () => {
            const process = new FakeProcess(build);
            h.spawned.push(process);
            return process as unknown as RelayedProcess;
        },
        relay: { send: async (request) => {
            h.relayed.push(request);
            return { status: 404, headers: [], body: new Response("gone").body! };
        } },
        admit: async (params) => {
            h.admits.push(params);
            if (h.refuse.now)
                throw new RpcError(-32000, "The user denied notes__delete_note.", { code: "refused" });
            return {};
        },
        status: (params) => h.statuses.push(params),
        info: { name: "hikarisub", version: "test" },
    };
    h.clients = new McpClients(connector);
    return h;
}

const kNotes: McpServerConfig = { id: "notes", name: "Notes", transport: "stdio", revision: 1 };

describe("MCP client", () => {
    test("names tools per server in every provider's characters, and cuts long ones apart", () => {
        expect(namespacedTool("notes", "delete.note")).toBe("notes__delete_note");
        const a = namespacedTool("server", "x".repeat(80) + "a");
        const b = namespacedTool("server", "x".repeat(80) + "b");
        expect(a.length).toBeLessThanOrEqual(64);
        expect(a).toMatch(/^[A-Za-z0-9_-]+$/);
        expect(a).not.toBe(b);
    });

    test("starts no server until a session asks for its tools", async () => {
        const h = harness();
        await h.clients.set([kNotes]);
        expect(h.spawned).toHaveLength(0);
        expect(h.clients.sources("s1")[0]!.tools()).toEqual([]);
        await h.clients.ensure();
        expect(h.spawned).toHaveLength(1);
        expect(h.statuses.at(-1)).toEqual({ server: "notes", state: "ready", tools: 2 });
    });

    test("offers each server's tools as its own group with the server's hints", async () => {
        const h = harness();
        await h.clients.set([kNotes, { ...kNotes, id: "work", name: "Work" }]);
        await h.clients.ensure();
        const sources = h.clients.sources("s1");
        expect(sources.map((s) => s.group)).toEqual(["mcp_notes", "mcp_work"]);
        const tools = sources[0]!.tools();
        expect(tools.map((t) => t.name)).toEqual(["notes__read_note", "notes__delete_note"]);
        expect(tools[0]!.annotations.readOnlyHint).toBe(true);
        expect(tools[1]!.annotations.readOnlyHint).toBe(false);
        expect(tools[1]!._meta["hikari/group"]).toBe("mcp_notes");
    });

    test("asks Qt before every call, and sends nothing to the server when Qt refuses", async () => {
        const h = harness();
        await h.clients.set([kNotes]);
        await h.clients.ensure();
        const tools = new ToolSet({ call: async () => { throw new Error("no HA1 here"); } }, undefined,
            () => h.clients.sources("s1"));
        expect(await tools.call("notes__read_note", { id: "7" })).toEqual({ content: [{ type: "text", text: "note 7" }] });
        expect(h.admits).toEqual([{ sessionId: "s1", server: "notes", tool: "read_note", arguments: { id: "7" } }]);

        h.refuse.now = true;
        const refused = await tools.call("notes__delete_note", { id: "7" });
        expect(refused.isError).toBe(true);
        expect(h.spawned[0]!.calls).toEqual(["read_note"]);
    });

    test("search_tools finds a server's tools by its group", async () => {
        const h = harness();
        await h.clients.set([kNotes]);
        await h.clients.ensure();
        const tools = new ToolSet({ call: async () => ({}) as never }, undefined, () => h.clients.sources("s1"));
        const found = await tools.call("search_tools", { group: "mcp_notes" });
        expect(found.content[0]!.type === "text" && found.content[0]!.text).toContain("notes__delete_note");
        expect(tools.list().map((t) => t.name)).toContain("notes__delete_note");
    });

    test("a thread finds a server's tool with search_tools after the cached prefix, as a tool_reference on Anthropic", async () => {
        const h = harness();
        await h.clients.set([kNotes]);
        await h.clients.ensure();
        const bodies = await scriptedTurn(provider,
            (id) => new ToolSet({ call: async () => ({}) as never }, undefined, () => h.clients.sources(id)), [
                toolUse("s1", "search_tools", { group: "mcp_notes" }),
                toolUse("c1", "notes__read_note", { id: "7" }),
                answer("Note 7 says hi."),
            ]);
        const [first, found] = bodies;
        expect(toolNames(first!)).not.toContain("notes__read_note");
        expect(JSON.stringify(first!.tools)).toContain("mcp_notes (Tools from the MCP server \\\"Notes\\\")");
        expect(toolNames(found!).slice(0, toolNames(first!).length)).toEqual(toolNames(first!));
        expect(found!.tools.slice(toolNames(first!).length).map((t: { name: string; defer_loading?: boolean }) =>
            [t.name, t.defer_loading])).toEqual([["notes__delete_note", true], ["notes__read_note", true]]);
        expect(resultOf(found!, "s1")!.content).toContainEqual({ type: "tool_reference", tool_name: "notes__read_note" });
        expect(h.spawned[0]!.calls).toEqual(["read_note"]);
        expect(JSON.stringify(resultOf(bodies[2]!, "c1")!.content)).toContain("note 7");
    });

    test("search_tools shows the first 40 characters of a server's name", () => {
        const name = "é".repeat(kServerNameShown) + "cut";
        expect(serverGroupDescription(name)).toBe(`Tools from the MCP server "${"é".repeat(kServerNameShown)}"`);
        expect(serverGroupDescription("Notes")).toBe("Tools from the MCP server \"Notes\"");
    });

    test("a changed server stops and connects again; a removed one stops", async () => {
        const h = harness();
        await h.clients.set([kNotes]);
        await h.clients.ensure();
        await h.clients.set([{ ...kNotes, revision: 2 }]);
        expect(h.spawned[0]!.killed).toBe(true);
        await h.clients.ensure();
        expect(h.spawned).toHaveLength(2);
        await h.clients.set([]);
        expect(h.spawned[1]!.killed).toBe(true);
        expect(h.clients.sources("s1")).toEqual([]);
    });

    test("a server that exits loses its tools and is started again on the next session", async () => {
        const h = harness();
        await h.clients.set([kNotes]);
        await h.clients.ensure();
        h.spawned[0]!.exit();
        await Bun.sleep(0);
        expect(h.clients.sources("s1")[0]!.tools()).toEqual([]);
        expect(h.statuses.at(-1)?.state).toBe("failed");
        await h.clients.ensure();
        expect(h.spawned).toHaveLength(2);
        expect(h.clients.sources("s1")[0]!.tools()).toHaveLength(2);
    });

    test("an HTTP server goes through the relay under its own target, and hands back every status", async () => {
        const h = harness();
        const response = await mcpFetch({ send: async (request) => {
            h.relayed.push(request);
            return { status: 404, headers: [["content-type", "text/plain"]], body: new Response("gone").body! };
        } }, "remote")("https://remote.mcp.invalid/", { method: "POST", body: "{}" });
        expect(response.status).toBe(404);
        expect(await response.text()).toBe("gone");
        expect(h.relayed[0]!.target).toEqual({ kind: "mcp", id: "remote" });
    });

    test("a failed HTTP connection is reported, not thrown", async () => {
        const h = harness();
        await h.clients.set([{ id: "remote", name: "Remote", transport: "http", revision: 1 }]);
        await h.clients.ensure();
        expect(h.relayed[0]!.target).toEqual({ kind: "mcp", id: "remote" });
        expect(h.statuses.at(-1)?.state).toBe("failed");
    });
});
