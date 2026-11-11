import type { Readable, Writable } from "node:stream";

import { ErrorCode, type SessionNewParams, type SessionPromptParams, type SessionPromptResult,
    type SessionStopReason, type SessionSteerParams, type SessionSteerResult, type SessionUpdate,
    type ToolCallKind } from "../protocol";
import type { Part, Usage } from "../providers/types";
import { RpcError } from "../rpc";

export const kCodexServer = "hikari";

export interface CodexProcess {
    readonly stdin: Writable;
    readonly stdout: Readable;
    on(event: "exit", listener: () => void): unknown;
    on(event: "error", listener: (error: Error) => void): unknown;
    kill(): boolean;
}

export interface CodexDeps {
    /** Qt starts `codex app-server` with Hikari's tools on a channel for this session. */
    spawn: (session: string) => CodexProcess;
    kindOf: (tool: string) => ToolCallKind;
    send: (update: SessionUpdate) => void;
    version: string;
}

interface Breakdown {
    inputTokens: number;
    cachedInputTokens: number;
    cacheWriteInputTokens: number;
    outputTokens: number;
}

type Item = { type: string; id: string } & Record<string, unknown>;

interface Turn {
    id?: string;
    done: (result: SessionStopReason | Error) => void;
    cancelled: boolean;
    streamed: Set<string>;
}

/** Codex's app-server speaks JSON-RPC without the `jsonrpc` member, one message per line. */
class Lines {
    private next = 1;
    private buffered = "";
    private readonly pending = new Map<number, { resolve: (r: unknown) => void; reject: (e: Error) => void }>();
    private readonly notifications = new Map<string, (params: unknown) => void>();
    private readonly requests = new Map<string, (params: unknown) => unknown>();

    constructor(private readonly write: (line: string) => void) {}

    onNotification(method: string, handler: (params: unknown) => void): void {
        this.notifications.set(method, handler);
    }

    onRequest(method: string, handler: (params: unknown) => unknown): void {
        this.requests.set(method, handler);
    }

    request(method: string, params: unknown): Promise<unknown> {
        const id = this.next++;
        return new Promise((resolve, reject) => {
            this.pending.set(id, { resolve, reject });
            this.write(JSON.stringify({ id, method, params }) + "\n");
        });
    }

    notify(method: string): void {
        this.write(JSON.stringify({ method }) + "\n");
    }

    feed(chunk: Buffer): void {
        this.buffered += chunk.toString("utf8");
        for (let newline = this.buffered.indexOf("\n"); newline >= 0; newline = this.buffered.indexOf("\n")) {
            const line = this.buffered.slice(0, newline);
            this.buffered = this.buffered.slice(newline + 1);
            try {
                this.receive(JSON.parse(line) as Record<string, unknown>);
            } catch {
                continue;
            }
        }
    }

    close(error: Error): void {
        for (const entry of this.pending.values())
            entry.reject(error);
        this.pending.clear();
    }

    private receive(message: Record<string, unknown>): void {
        const { id, method } = message;
        if (typeof method !== "string") {
            const entry = typeof id === "number" ? this.pending.get(id) : undefined;
            if (!entry)
                return;
            this.pending.delete(id as number);
            const error = message.error as { message?: string } | undefined;
            return error ? entry.reject(new Error(error.message ?? "Codex refused")) : entry.resolve(message.result);
        }
        if (id === undefined)
            return this.notifications.get(method)?.(message.params);
        const handler = this.requests.get(method);
        const answer = handler
            ? { id, result: handler(message.params) }
            : { id, error: { code: ErrorCode.methodNotFound, message: `HikariSub doesn't answer ${method}` } };
        this.write(JSON.stringify(answer) + "\n");
    }
}

const kZero: Breakdown = { inputTokens: 0, cachedInputTokens: 0, cacheWriteInputTokens: 0, outputTokens: 0 };

function input(parts: Part[]) {
    return parts.flatMap((part) => {
        if (part.type === "text")
            return [{ type: "text" as const, text: part.text, text_elements: [] }];
        if (part.type === "image")
            return [{ type: "image" as const, url: `data:${part.mediaType};base64,${part.base64}` }];
        return [];
    });
}

function usage(now: Breakdown, before: Breakdown): Usage {
    return {
        inputTokens: now.inputTokens - before.inputTokens,
        outputTokens: now.outputTokens - before.outputTokens,
        cachedInputTokens: now.cachedInputTokens - before.cachedInputTokens,
        cacheWriteTokens: now.cacheWriteInputTokens - before.cacheWriteInputTokens,
    };
}

function resultText(result: unknown): string {
    const blocks = (result as { content?: unknown } | null)?.content;
    if (!Array.isArray(blocks))
        return "";
    return blocks.flatMap((b) => (b?.type === "text" && typeof b.text === "string" ? [b.text] : [])).join("\n");
}

/** A thread on the user's own Codex, through `codex app-server`, with Hikari's tools as its only MCP server. */
export class CodexSession {
    private process?: CodexProcess;
    private peer?: Lines;
    private ready?: Promise<void>;
    private thread?: string;
    private turn?: Turn;
    private total: Breakdown = kZero;
    private atTurnStart: Breakdown = kZero;
    private readonly calls = new Set<string>();

    constructor(readonly id: string, private readonly params: SessionNewParams, private readonly deps: CodexDeps) {}

    async prompt(params: SessionPromptParams): Promise<SessionPromptResult> {
        if (this.turn)
            throw new RpcError(ErrorCode.busy, "A turn is already running in this session");
        const result = new Promise<SessionStopReason | Error>((done) =>
            (this.turn = { done, cancelled: false, streamed: new Set() }));
        const turn = this.turn!;
        try {
            await this.start();
            this.deps.send({ sessionUpdate: "history", message: { author: "user", parts: params.prompt } });
            this.atTurnStart = this.total;
            const started = await this.peer!.request("turn/start",
                { threadId: this.thread, input: input(params.prompt) }) as { turn: { id: string } };
            turn.id = started.turn.id;
            if (turn.cancelled)
                this.interrupt(turn);
        } catch (error) {
            if (this.turn === turn)
                this.finish(error instanceof Error ? error : new Error(String(error)));
        }
        const stop = await result;
        if (stop instanceof Error)
            throw new RpcError(ErrorCode.providerFailed, stop.message);
        return { stopReason: stop };
    }

    async steer(params: SessionSteerParams): Promise<SessionSteerResult> {
        const turn = this.turn;
        if (!turn?.id || !this.peer)
            return { taken: false };
        try {
            await this.peer.request("turn/steer", { threadId: this.thread, input: input(params.prompt),
                expectedTurnId: turn.id });
            this.deps.send({ sessionUpdate: "history", message: { author: "user", parts: params.prompt } });
            return { taken: true };
        } catch {
            return { taken: false };
        }
    }

    cancel(): void {
        if (!this.turn)
            return;
        this.turn.cancelled = true;
        this.interrupt(this.turn);
    }

    close(): void {
        this.cancel();
        this.process?.kill();
        this.ended();
    }

    private interrupt(turn: Turn): void {
        if (turn.id)
            void this.peer?.request("turn/interrupt", { threadId: this.thread, turnId: turn.id }).catch(() => {});
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
        const peer = new Lines((line) => process.stdin.write(line));
        this.process = process;
        this.peer = peer;
        process.stdout.on("data", (chunk: Buffer) => peer.feed(chunk));
        process.on("exit", () => this.ended(process));
        process.on("error", (error) => this.ended(process, error));
        this.listen(peer);
        await peer.request("initialize", { clientInfo: { name: "hikarisub", title: "HikariSub", version: this.deps.version },
            capabilities: { experimentalApi: false, requestAttestation: false } });
        peer.notify("initialized");
        const model = this.params.model.model || undefined;
        const opened = this.thread
            ? await peer.request("thread/resume", { threadId: this.thread, ...(model ? { model } : {}) })
            : await peer.request("thread/start", { ...(model ? { model } : {}),
                ...(this.params.system ? { developerInstructions: this.params.system } : {}) });
        this.thread = (opened as { thread: { id: string } }).thread.id;
    }

    private listen(peer: Lines): void {
        peer.onNotification("item/agentMessage/delta", (p) => {
            const { itemId, delta } = p as { itemId: string; delta: string };
            this.turn?.streamed.add(itemId);
            this.deps.send({ sessionUpdate: "agent_message_chunk", content: { type: "text", text: delta } });
        });
        peer.onNotification("item/started", (p) => this.itemStarted((p as { item: Item }).item));
        peer.onNotification("item/completed", (p) => this.itemCompleted((p as { item: Item }).item));
        peer.onNotification("thread/tokenUsage/updated", (p) => {
            this.total = (p as { tokenUsage: { total: Breakdown } }).tokenUsage.total;
        });
        peer.onNotification("turn/completed", (p) => {
            const { turn } = p as { turn: { id: string; status: string; error: { message: string } | null } };
            if (!this.turn || (this.turn.id && this.turn.id !== turn.id))
                return;
            this.deps.send({ sessionUpdate: "usage", usage: usage(this.total, this.atTurnStart) });
            if (turn.status === "interrupted" || this.turn.cancelled)
                return this.finish("cancelled");
            if (turn.status === "completed")
                return this.finish("end_turn");
            this.finish(new Error(turn.error?.message ?? "Codex's turn failed"));
        });
        peer.onRequest("mcpServer/elicitation/request", () => ({ action: "decline", content: null }));
    }

    private itemStarted(item: Item): void {
        if (item.type !== "mcpToolCall" && item.type !== "commandExecution" && item.type !== "fileChange")
            return;
        const ours = item.type === "mcpToolCall" && item.server === kCodexServer;
        const title = ours ? String(item.tool) : item.type === "commandExecution" ? String(item.command) : "fileChange";
        this.calls.add(item.id);
        this.deps.send({ sessionUpdate: "tool_call", toolCallId: item.id, title,
            kind: ours ? this.deps.kindOf(title) : "other", status: "in_progress",
            rawInput: item.type === "mcpToolCall" ? item.arguments : {} });
    }

    private itemCompleted(item: Item): void {
        if (item.type === "agentMessage" && typeof item.text === "string" && item.text) {
            if (!this.turn?.streamed.has(item.id))
                this.deps.send({ sessionUpdate: "agent_message_chunk", content: { type: "text", text: item.text } });
            this.deps.send({ sessionUpdate: "history", message: { author: "assistant", parts: [{ type: "text", text: item.text }] } });
            return;
        }
        if (!this.calls.delete(item.id))
            return;
        const failed = item.status !== "completed";
        const text = item.type === "mcpToolCall"
            ? (item.error as { message?: string } | null)?.message ?? resultText(item.result)
            : "";
        this.deps.send({ sessionUpdate: "tool_call_update", toolCallId: item.id, status: failed ? "failed" : "completed",
            content: [{ type: "content", content: { type: "text", text } }] });
    }

    private ended(process?: CodexProcess, error?: Error): void {
        if (process && process !== this.process)
            return;
        this.peer?.close(error ?? new Error("Codex ended the session"));
        this.process = undefined;
        this.peer = undefined;
        this.ready = undefined;
        if (this.turn)
            this.finish(error ?? new Error("Codex ended the session"));
    }

    private finish(stop: SessionStopReason | Error): void {
        const turn = this.turn;
        this.turn = undefined;
        for (const call of this.calls)
            this.deps.send({ sessionUpdate: "tool_call_update", toolCallId: call, status: "failed", content: [] });
        this.calls.clear();
        turn?.done(stop);
    }
}
