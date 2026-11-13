import type { Readable, Writable } from "node:stream";

import { ErrorCode, type PlanStatus, type SessionNewParams, type SessionPromptParams, type SessionPromptResult,
    type SessionStopReason, type SessionSteerResult, type SessionUpdate, type ToolCallKind,
    type ToolCallStatus } from "../protocol";
import type { Part } from "../providers/types";
import { Peer, RpcError } from "../rpc";
import { withSkills, type Skills } from "../skills/skills";

export const kAcpServer = "hikari";
export const kAcpProtocolVersion = 1;

export interface AcpProcess {
    readonly stdin: Writable;
    readonly stdout: Readable;
    on(event: "exit", listener: () => void): unknown;
    on(event: "error", listener: (error: Error) => void): unknown;
    kill(): boolean;
}

export interface AcpDeps {
    /** Qt starts the agent, and its guard gives `session/new` Hikari's tools on a channel for this session. */
    spawn: (session: string) => AcpProcess;
    kindOf: (tool: string) => ToolCallKind;
    send: (update: SessionUpdate) => void;
    version: string;
    /** The session's skills, whose catalogue joins the instructions the first prompt opens with. */
    skills?: Skills;
}

interface AgentCapabilities {
    loadSession?: boolean;
    promptCapabilities?: { image?: boolean };
}

type ContentBlock = { type: "text"; text: string } | { type: "image"; mimeType: string; data: string };

interface Turn {
    done: (result: SessionStopReason | Error) => void;
    text: string;
    cancelled: boolean;
}

const kStopReasons: readonly SessionStopReason[] = ["end_turn", "max_tokens", "max_turn_requests", "refusal", "cancelled"];
const kReadKinds = new Set(["read", "search", "fetch", "think"]);
const kEditKinds = new Set(["edit", "delete", "move"]);
const kToolPrefixes = ["mcp__hikari__", "hikari_", "hikari.", "hikari/", "hikari:"];

function isObject(value: unknown): value is Record<string, unknown> {
    return typeof value === "object" && value !== null && !Array.isArray(value);
}

function servedByHikari(meta: unknown): boolean {
    if (!isObject(meta))
        return false;
    return Object.entries(meta).some(([key, value]) =>
        ((key === "server_name" || key === "serverName") && value === kAcpServer) || servedByHikari(value));
}

/** The Hikari tool an agent's tool call runs, if it is one: agents name MCP tools their own way. */
export function hikariTool(call: { title?: unknown; _meta?: unknown; rawInput?: unknown }): string | undefined {
    const title = typeof call.title === "string" ? call.title.trim() : "";
    for (const prefix of kToolPrefixes)
        if (title.startsWith(prefix) && title.length > prefix.length)
            return title.slice(prefix.length).split(/\s/, 1)[0];
    if (servedByHikari(call._meta))
        return title.split(/\s/, 1)[0] || undefined;
    return undefined;
}

function blocks(parts: Part[], images: boolean): ContentBlock[] {
    return parts.flatMap((part): ContentBlock[] => {
        if (part.type === "text")
            return [{ type: "text", text: part.text }];
        if (part.type === "image" && images)
            return [{ type: "image", mimeType: part.mediaType, data: part.base64 }];
        return [];
    });
}

function status(value: unknown): ToolCallStatus | undefined {
    if (value === "completed" || value === "failed")
        return value;
    if (value === "pending" || value === "in_progress")
        return "in_progress";
    return undefined;
}

function contentText(content: unknown): string {
    if (!Array.isArray(content))
        return "";
    return content.flatMap((c) => (isObject(c) && c.type === "content" && isObject(c.content) &&
        c.content.type === "text" && typeof c.content.text === "string" ? [c.content.text] : [])).join("\n");
}

/** A thread on an agent that speaks ACP, with Hikari's tools as its only MCP server. */
export class AcpSession {
    private process?: AcpProcess;
    private peer?: Peer;
    private ready?: Promise<void>;
    private agentSession?: string;
    private capabilities: AgentCapabilities = {};
    private turn?: Turn;
    private introduced = false;
    private readonly calls = new Set<string>();

    constructor(readonly id: string, private readonly params: SessionNewParams, private readonly deps: AcpDeps) {}

    async prompt(params: SessionPromptParams): Promise<SessionPromptResult> {
        if (this.turn)
            throw new RpcError(ErrorCode.busy, "A turn is already running in this session");
        const result = new Promise<SessionStopReason | Error>((done) => (this.turn = { done, text: "", cancelled: false }));
        const turn = this.turn!;
        try {
            await this.start();
            if (turn.cancelled)
                this.finish("cancelled");
            else
                await this.send(turn, params);
        } catch (error) {
            if (this.turn === turn)
                this.finish(error instanceof Error ? error : new Error(String(error)));
        }
        const stop = await result;
        if (stop instanceof Error)
            throw new RpcError(ErrorCode.providerFailed, stop.message);
        return { stopReason: stop };
    }

    private async send(turn: Turn, params: SessionPromptParams): Promise<void> {
        this.deps.send({ sessionUpdate: "history", message: { author: "user", parts: params.prompt } });
        const prompt = blocks(params.prompt, this.capabilities.promptCapabilities?.image === true);
        const system = this.introduced ? undefined
            : this.deps.skills ? await withSkills(this.params.system, this.deps.skills) : this.params.system;
        if (system)
            prompt.unshift({ type: "text", text: system });
        this.introduced = true;
        const answer = await this.peer!.request("session/prompt", { sessionId: this.agentSession, prompt });
        const reason = (answer as { stopReason?: unknown } | null)?.stopReason;
        if (this.turn === turn)
            this.finish(kStopReasons.includes(reason as SessionStopReason) ? reason as SessionStopReason : "end_turn");
    }

    steer(): Promise<SessionSteerResult> {
        return Promise.resolve({ taken: false });
    }

    takesToolCalls(): boolean {
        return this.turn !== undefined && !this.turn.cancelled;
    }

    cancel(): void {
        if (!this.turn)
            return;
        this.turn.cancelled = true;
        if (this.agentSession)
            this.peer?.notify("session/cancel", { sessionId: this.agentSession });
    }

    close(): void {
        this.cancel();
        this.process?.kill();
        this.ended();
    }

    private start(): Promise<void> {
        return this.ready ??= this.open().catch((error: unknown) => {
            this.process?.kill();
            this.ended();
            throw error;
        });
    }

    private async open(): Promise<void> {
        const process = this.deps.spawn(this.id);
        const peer = new Peer((line) => process.stdin.write(line), (reason) => this.ended(process, new Error(reason)));
        this.process = process;
        this.peer = peer;
        process.stdout.on("data", (chunk: Buffer) => peer.feed(chunk));
        process.on("exit", () => this.ended(process));
        process.on("error", (error) => this.ended(process, error));
        peer.onNotification("session/update", (p) => this.update(p));
        peer.onRequest("session/request_permission", () => ({ outcome: { outcome: "cancelled" } }));
        const initialized = await peer.request("initialize", { protocolVersion: kAcpProtocolVersion,
            clientCapabilities: { fs: { readTextFile: false, writeTextFile: false }, terminal: false },
            clientInfo: { name: "hikarisub", title: "HikariSub", version: this.deps.version } }) as
            { agentCapabilities?: AgentCapabilities } | null;
        this.capabilities = initialized?.agentCapabilities ?? {};
        if (this.agentSession && this.capabilities.loadSession) {
            await peer.request("session/load", { sessionId: this.agentSession, cwd: ".", mcpServers: [] });
            return;
        }
        const opened = await peer.request("session/new", { cwd: ".", mcpServers: [] }) as { sessionId?: unknown } | null;
        if (typeof opened?.sessionId !== "string")
            throw new Error("The agent opened no session");
        this.agentSession = opened.sessionId;
        this.introduced = false;
    }

    private update(params: unknown): void {
        if (!isObject(params) || params.sessionId !== this.agentSession || !isObject(params.update))
            return;
        const u = params.update;
        switch (u.sessionUpdate) {
        case "agent_message_chunk":
            if (isObject(u.content) && u.content.type === "text" && typeof u.content.text === "string" && u.content.text) {
                if (this.turn)
                    this.turn.text += u.content.text;
                this.deps.send({ sessionUpdate: "agent_message_chunk", content: { type: "text", text: u.content.text } });
            }
            return;
        case "tool_call":
            return this.toolCall(u);
        case "tool_call_update":
            return this.toolCallUpdate(u);
        case "plan":
            if (Array.isArray(u.entries))
                this.deps.send({ sessionUpdate: "plan", entries: u.entries.filter(isObject).map((e) => ({
                    content: String(e.content ?? ""), priority: "medium" as const,
                    status: (e.status === "completed" || e.status === "in_progress" ? e.status : "pending") as PlanStatus,
                })) });
            return;
        }
    }

    private toolCall(u: Record<string, unknown>): void {
        if (typeof u.toolCallId !== "string")
            return;
        const tool = hikariTool(u);
        const kind: ToolCallKind = tool ? this.deps.kindOf(tool)
            : kReadKinds.has(String(u.kind)) ? "read" : kEditKinds.has(String(u.kind)) ? "edit" : "other";
        this.calls.add(u.toolCallId);
        this.deps.send({ sessionUpdate: "tool_call", toolCallId: u.toolCallId, title: tool ?? String(u.title ?? ""),
            kind, status: status(u.status) ?? "in_progress", rawInput: u.rawInput ?? {} });
        if (status(u.status) === "completed" || status(u.status) === "failed")
            this.toolCallUpdate(u);
    }

    private toolCallUpdate(u: Record<string, unknown>): void {
        const done = status(u.status);
        if (typeof u.toolCallId !== "string" || (done !== "completed" && done !== "failed") || !this.calls.delete(u.toolCallId))
            return;
        this.deps.send({ sessionUpdate: "tool_call_update", toolCallId: u.toolCallId, status: done,
            content: [{ type: "content", content: { type: "text", text: contentText(u.content) } }] });
    }

    private ended(process?: AcpProcess, error?: Error): void {
        if (process && process !== this.process)
            return;
        this.peer?.close(new RpcError(ErrorCode.providerFailed, error?.message ?? "The agent ended the session"));
        this.process = undefined;
        this.peer = undefined;
        this.ready = undefined;
        if (this.turn)
            this.finish(error ?? new Error("The agent ended the session"));
    }

    private finish(stop: SessionStopReason | Error): void {
        const turn = this.turn;
        this.turn = undefined;
        for (const call of this.calls)
            this.deps.send({ sessionUpdate: "tool_call_update", toolCallId: call, status: "failed", content: [] });
        this.calls.clear();
        if (turn?.text)
            this.deps.send({ sessionUpdate: "history", message: { author: "assistant", parts: [{ type: "text", text: turn.text }] } });
        turn?.done(stop);
    }
}
