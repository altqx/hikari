import type { Readable } from "node:stream";

import type { HookCallbackMatcher, HookInput, Options, Query, SDKMessage, SDKUserMessage, SpawnedProcess,
    SpawnOptions } from "@anthropic-ai/claude-agent-sdk";

import type { McpServer } from "../mcp/server";
import { withSkills, type Skills } from "../skills/skills";
import { ErrorCode, type SessionNewParams, type SessionPromptParams, type SessionPromptResult,
    type SessionStopReason, type SessionSteerResult, type SessionUpdate, type ToolCallKind } from "../protocol";
import type { Part, Usage } from "../providers/types";
import { RpcError } from "../rpc";
import type { ClaudeAgentSdk } from "./vendor";

export const kServer = "hikari";
const kStderrTail = 2048;
const kToolPrefix = `mcp__${kServer}__`;

export interface ClaudeDeps {
    sdk: () => Promise<ClaudeAgentSdk>;
    spawn: (options: SpawnOptions) => SpawnedProcess & { readonly stderr?: Readable };
    /** A server of its own, so Qt sees the session as the caller. */
    mcp: McpServer;
    /** The session's skills, whose catalogue joins the system prompt. */
    skills?: Skills;
    kindOf: (tool: string) => ToolCallKind;
    send: (update: SessionUpdate) => void;
}

/** Hikari's MCP server inside the Agent SDK's query, one message at a time. */
export function inProcessServer(mcp: McpServer, connection: string) {
    return {
        type: "sdk" as const,
        name: kServer,
        instance: {
            async connect(transport: { onmessage?: (m: unknown) => void; onclose?: () => void; send(m: unknown): Promise<void>;
                start(): Promise<void> }) {
                transport.onmessage = (message) => {
                    void mcp.message(connection, JSON.stringify(message)).then((answer) => {
                        if (answer.reply)
                            return transport.send(JSON.parse(answer.reply));
                    });
                };
                transport.onclose = () => mcp.closed(connection);
                await transport.start();
            },
            async close() {
                mcp.closed(connection);
            },
        },
    };
}

class Inbox implements AsyncIterable<SDKUserMessage> {
    private readonly waiting: SDKUserMessage[] = [];
    private wake?: () => void;
    private ended = false;

    push(message: SDKUserMessage): void {
        this.waiting.push(message);
        this.wake?.();
    }

    end(): void {
        this.ended = true;
        this.wake?.();
    }

    async *[Symbol.asyncIterator](): AsyncIterator<SDKUserMessage> {
        for (;;) {
            const next = this.waiting.shift();
            if (next) {
                yield next;
                continue;
            }
            if (this.ended)
                return;
            await new Promise<void>((resolve) => (this.wake = resolve));
            this.wake = undefined;
        }
    }
}

type ContentBlock = { type: "text"; text: string }
    | { type: "image"; source: { type: "base64"; media_type: "image/png"; data: string } };

function content(parts: Part[]): ContentBlock[] {
    return parts.flatMap((part): ContentBlock[] => {
        if (part.type === "text")
            return [{ type: "text" as const, text: part.text }];
        if (part.type === "image")
            return [{ type: "image" as const,
                source: { type: "base64" as const, media_type: part.mediaType as "image/png", data: part.base64 } }];
        return [];
    });
}

function blocksText(response: unknown): string {
    const blocks = Array.isArray(response) ? response : (response as { content?: unknown } | undefined)?.content;
    if (!Array.isArray(blocks))
        return typeof response === "string" ? response : "";
    return blocks.flatMap((b) => (b?.type === "text" && typeof b.text === "string" ? [b.text] : [])).join("\n");
}

function usage(raw: { input_tokens?: number; output_tokens?: number; cache_read_input_tokens?: number;
    cache_creation_input_tokens?: number; cache_creation?: { ephemeral_1h_input_tokens?: number } } | undefined,
    cost: number | undefined): Usage {
    const cached = raw?.cache_read_input_tokens ?? 0;
    const written = raw?.cache_creation_input_tokens ?? 0;
    return {
        inputTokens: (raw?.input_tokens ?? 0) + cached + written,
        outputTokens: raw?.output_tokens ?? 0,
        cachedInputTokens: cached,
        cacheWriteTokens: written,
        cacheWriteHourTokens: raw?.cache_creation?.ephemeral_1h_input_tokens ?? 0,
        ...(cost === undefined ? {} : { reportedCost: cost }),
    };
}

interface Turn {
    done: (result: SessionStopReason | Error) => void;
    cancelled: boolean;
}

/**
 * A thread on the user's own `claude`, through the Claude Agent SDK: one long-running query, Claude's built-in tools
 * off, and only Hikari's tools allowed.
 */
export class ClaudeSession {
    private query?: Query;
    private inbox?: Inbox;
    private turn?: Turn;
    private claudeSession?: string;
    private readonly calls = new Set<string>();
    private stderr = "";
    private system?: string;

    constructor(readonly id: string, private readonly params: SessionNewParams, private readonly deps: ClaudeDeps) {}

    /** What Claude last wrote on stderr, which the sidecar also logs. */
    get stderrTail(): string {
        return this.stderr;
    }

    get running(): boolean {
        return this.turn !== undefined;
    }

    options(): Options {
        const model = this.params.model.model;
        return {
            pathToClaudeCodeExecutable: "claude",
            spawnClaudeCodeProcess: (options) => this.spawn(options),
            settingSources: [],
            tools: [],
            strictMcpConfig: true,
            permissionMode: "dontAsk",
            allowedTools: [`mcp__${kServer}`],
            mcpServers: { [kServer]: inProcessServer(this.deps.mcp, `claude:${this.id}`) as never },
            hooks: { PreToolUse: [this.hook((input) => this.before(input))],
                PostToolUse: [this.hook((input) => this.after(input))],
                PostToolUseFailure: [this.hook((input) => this.after(input))] },
            ...(model ? { model } : {}),
            ...(this.system ? { systemPrompt: this.system } : {}),
            ...(this.claudeSession ? { resume: this.claudeSession } : {}),
        };
    }

    async prompt(params: SessionPromptParams): Promise<SessionPromptResult> {
        if (this.turn)
            throw new RpcError(ErrorCode.busy, "A turn is already running in this session");
        const result = new Promise<SessionStopReason | Error>((done) => (this.turn = { done, cancelled: false }));
        try {
            await this.start();
            this.deps.send({ sessionUpdate: "history", message: { author: "user", parts: params.prompt } });
            this.inbox!.push({ type: "user", message: { role: "user", content: content(params.prompt) },
                parent_tool_use_id: null } as SDKUserMessage);
        } catch (error) {
            this.finish(error instanceof Error ? error : new Error(String(error)));
        }
        const stop = await result;
        if (stop instanceof Error)
            throw new RpcError(ErrorCode.providerFailed, stop.message);
        return { stopReason: stop };
    }

    steer(): Promise<SessionSteerResult> {
        return Promise.resolve({ taken: false });
    }

    cancel(): void {
        if (!this.turn)
            return;
        this.turn.cancelled = true;
        void this.query?.interrupt().catch(() => {});
    }

    close(): void {
        this.cancel();
        this.inbox?.end();
        this.query?.close();
        this.query = undefined;
    }

    private async start(): Promise<void> {
        if (this.query)
            return;
        const sdk = await this.deps.sdk();
        this.system ??= this.deps.skills ? await withSkills(this.params.system, this.deps.skills) : this.params.system;
        this.inbox = new Inbox();
        this.query = sdk.query({ prompt: this.inbox, options: this.options() });
        void this.read(this.query);
    }

    private async read(query: Query): Promise<void> {
        try {
            for await (const message of query)
                this.receive(message);
            this.finish(new Error("Claude ended the session"));
        } catch (error) {
            this.finish(error instanceof Error ? error : new Error(String(error)));
        } finally {
            if (this.query === query) {
                this.query = undefined;
                this.inbox = undefined;
            }
        }
    }

    private receive(message: SDKMessage): void {
        if (message.type === "system" && message.subtype === "init") {
            this.claudeSession = message.session_id;
            return;
        }
        if (message.type === "assistant" && message.parent_tool_use_id === null) {
            const text = message.message.content.flatMap((block) => (block.type === "text" ? [block.text] : [])).join("");
            if (text) {
                this.deps.send({ sessionUpdate: "agent_message_chunk", content: { type: "text", text } });
                this.deps.send({ sessionUpdate: "history", message: { author: "assistant", parts: [{ type: "text", text }] } });
            }
            return;
        }
        if (message.type !== "result")
            return;
        this.deps.send({ sessionUpdate: "usage", usage: usage(message.usage, message.total_cost_usd) });
        if (this.turn?.cancelled)
            return this.finish("cancelled");
        if (message.subtype === "success")
            return this.finish("end_turn");
        if (message.subtype === "error_max_turns")
            return this.finish("max_turn_requests");
        this.finish(new Error(("errors" in message && message.errors.join("; ")) || message.subtype));
    }

    private spawn(options: SpawnOptions): SpawnedProcess {
        const process = this.deps.spawn(options);
        process.stderr?.on("data", (chunk: Buffer) => {
            const text = chunk.toString("utf8");
            console.error(`claude ${this.id}: ${text.trimEnd()}`);
            this.stderr = (this.stderr + text).slice(-kStderrTail);
        });
        return process;
    }

    private finish(stop: SessionStopReason | Error): void {
        if (stop instanceof Error && this.stderr.trim())
            stop = new Error(`${stop.message}\nClaude said: ${this.stderr.trim()}`);
        const turn = this.turn;
        this.turn = undefined;
        for (const call of this.calls)
            this.deps.send({ sessionUpdate: "tool_call_update", toolCallId: call, status: "failed", content: [] });
        this.calls.clear();
        turn?.done(stop);
    }

    private hook(run: (input: HookInput) => object): HookCallbackMatcher {
        return { hooks: [async (input) => run(input) as never] };
    }

    private before(input: HookInput): object {
        if (input.hook_event_name !== "PreToolUse")
            return {};
        if (!input.tool_name.startsWith(kToolPrefix))
            return { hookSpecificOutput: { hookEventName: "PreToolUse", permissionDecision: "deny",
                permissionDecisionReason: "Only HikariSub's tools are available." } };
        const name = input.tool_name.slice(kToolPrefix.length);
        this.calls.add(input.tool_use_id);
        this.deps.send({ sessionUpdate: "tool_call", toolCallId: input.tool_use_id, title: name,
            kind: this.deps.kindOf(name), status: "in_progress", rawInput: input.tool_input });
        return {};
    }

    private after(input: HookInput): object {
        if (input.hook_event_name === "PostToolUse")
            this.done(input.tool_use_id, "completed", blocksText(input.tool_response));
        else if (input.hook_event_name === "PostToolUseFailure")
            this.done(input.tool_use_id, "failed", input.error);
        return {};
    }

    private done(call: string, status: "completed" | "failed", text: string): void {
        if (!this.calls.delete(call))
            return;
        this.deps.send({ sessionUpdate: "tool_call_update", toolCallId: call, status,
            content: [{ type: "content", content: { type: "text", text } }] });
    }
}
