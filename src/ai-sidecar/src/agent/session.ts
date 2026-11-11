import {
    ErrorCode,
    Method,
    type PlanStatus,
    type SessionModel,
    type SessionNewParams,
    type SessionPromptParams,
    type SessionPromptResult,
    type SessionStopReason,
    type SessionSteerParams,
    type SessionSteerResult,
    type SessionUpdate,
    type SessionUpdateParams,
    type ToolCallKind,
} from "../protocol";
import { ProviderError } from "../providers/errors";
import type { CompleteParams, Finished, Message, Part, StopReason, ToolCall, ToolDefinition, Update } from "../providers/types";
import { RpcError } from "../rpc";
import { kReadSkillTool, Skills } from "../skills/skills";
import type { Tool } from "../tools/catalogue";
import { type ImageBudget, kDefaultImageBudget } from "../tools/images";
import type { CallToolResult, ToolSet } from "../tools/toolset";

/** One model request with its stream updates; ProviderService.complete in the sidecar. */
export type Complete = (params: CompleteParams, signal: AbortSignal, emit: (stream: number, update: Update) => void)
    => Promise<Finished>;

/** Model requests one turn may make before it stops with max_turn_requests. */
export const kMaxTurnRequests = 64;
/** Text deltas wait this long so a turn sends at most one chunk per frame. */
export const kChunkMs = 16;

export const kPlanTool: Tool = {
    name: "update_plan",
    description: "Shows the user your plan as a checklist. Send every step each time, with at most one in_progress.",
    inputSchema: {
        type: "object",
        properties: {
            steps: {
                type: "array",
                items: {
                    type: "object",
                    properties: {
                        step: { type: "string" },
                        status: { type: "string", enum: ["pending", "in_progress", "completed"] },
                    },
                    required: ["step", "status"],
                    additionalProperties: false,
                },
            },
        },
        required: ["steps"],
        additionalProperties: false,
    },
    annotations: { readOnlyHint: true },
    _meta: { "hikari/group": "core" },
};

const kStopReasons: Record<StopReason, SessionStopReason> = {
    end: "end_turn",
    toolUse: "end_turn",
    maxTokens: "max_tokens",
    refused: "refusal",
};

function definition(tool: Tool): ToolDefinition {
    return { name: tool.name, description: tool.description, parametersJson: JSON.stringify(tool.inputSchema) };
}

function argumentsOf(call: ToolCall): unknown {
    try {
        return JSON.parse(call.argumentsJson || "{}");
    } catch {
        return undefined;
    }
}

function resultText(result: CallToolResult): string {
    return result.content.map((c) => c.type === "text" ? c.text : `[${c.mimeType} image]`).join("\n");
}

function isPlanStatus(value: unknown): value is PlanStatus {
    return value === "pending" || value === "in_progress" || value === "completed";
}

interface Steer {
    parts: Part[];
    done: (taken: boolean) => void;
}

interface Turn {
    controller: AbortController;
    steers: Steer[];
}

/** One thread's conversation in the sidecar. Qt owns the thread and its permissions; this runs its turns. */
export class AgentSession {
    readonly history: Message[];
    private model: SessionModel;
    private turn?: Turn;
    private pendingText = "";
    private flushTimer?: ReturnType<typeof setTimeout>;
    private system?: string;

    constructor(
        readonly id: string,
        private readonly params: SessionNewParams,
        private readonly tools: ToolSet,
        private readonly complete: Complete,
        private readonly send: (update: SessionUpdate) => void,
        private readonly nextStream: () => number,
        private readonly imageBudget: (providerId: string) => ImageBudget,
        private readonly skills: Skills = new Skills(),
    ) {
        this.history = [...(params.history ?? [])];
        this.model = params.model;
    }

    get running(): boolean {
        return this.turn !== undefined;
    }

    async prompt(params: SessionPromptParams): Promise<SessionPromptResult> {
        if (this.turn)
            throw new RpcError(ErrorCode.busy, "A turn is already running in this session");
        if (params.model)
            this.model = params.model;
        this.tools.imageBudget = this.imageBudget(this.model.providerId);
        const turn: Turn = { controller: new AbortController(), steers: [] };
        this.turn = turn;
        try {
            this.system ??= await this.instructions();
            this.keep({ author: "user", parts: params.prompt });
            return { stopReason: await this.run(turn, params.caching ?? this.params.caching) };
        } finally {
            this.flush();
            for (const steer of turn.steers)
                steer.done(false);
            this.turn = undefined;
        }
    }

    cancel(): void {
        this.turn?.controller.abort();
    }

    steer(params: SessionSteerParams): Promise<SessionSteerResult> {
        if (!this.turn)
            return Promise.resolve({ taken: false });
        const turn = this.turn;
        return new Promise((resolve) => turn.steers.push({ parts: params.prompt, done: (taken) => resolve({ taken }) }));
    }

    private async run(turn: Turn, caching: boolean | undefined): Promise<SessionStopReason> {
        const signal = turn.controller.signal;
        for (let request = 0; request < kMaxTurnRequests; ++request) {
            let finished: Finished;
            try {
                finished = await this.complete(this.request(caching), signal, (_stream, update) => this.streamed(update));
            } catch (error) {
                if (signal.aborted && (!(error instanceof ProviderError) || error.kind === "cancelled"))
                    return "cancelled";
                throw error;
            }
            this.flush();
            this.send({ sessionUpdate: "usage", usage: finished.usage });
            const parts: Part[] = finished.text ? [{ type: "text", text: finished.text }] : [];
            parts.push(...finished.toolCalls);
            if (parts.length > 0)
                this.keep({ author: "assistant", parts });
            if (finished.toolCalls.length === 0)
                return kStopReasons[finished.reason];
            const results = await this.callTools(finished.toolCalls, signal);
            const steers = signal.aborted ? [] : turn.steers.splice(0);
            this.keep({ author: "user", parts: [...results, ...steers.flatMap((s) => s.parts)] });
            for (const steer of steers)
                steer.done(true);
            if (signal.aborted)
                return "cancelled";
        }
        return "max_turn_requests";
    }

    private async callTools(calls: ToolCall[], signal: AbortSignal): Promise<Part[]> {
        const results: Part[] = [];
        for (const call of calls) {
            if (signal.aborted) {
                results.push({ type: "toolResult", callId: call.id, content: "The user stopped the turn.", isError: true });
                continue;
            }
            const args = argumentsOf(call);
            this.send({ sessionUpdate: "tool_call", toolCallId: call.id, title: call.name, kind: this.kindOf(call.name),
                status: "in_progress", rawInput: args ?? call.argumentsJson });
            const result = args === undefined
                ? { content: [{ type: "text" as const, text: `${call.name}: the arguments aren't JSON.` }], isError: true }
                : call.name === kPlanTool.name ? this.plan(args)
                : call.name === kReadSkillTool.name ? await this.skills.read(args as Record<string, unknown>)
                : await this.tools.call(call.name, args);
            const text = resultText(result);
            this.send({ sessionUpdate: "tool_call_update", toolCallId: call.id, status: result.isError ? "failed" : "completed",
                content: [{ type: "content", content: { type: "text", text } }] });
            results.push({ type: "toolResult", callId: call.id, content: text, ...(result.isError ? { isError: true } : {}) });
        }
        return results;
    }

    private async instructions(): Promise<string> {
        const skills = await this.skills.instructions();
        return [this.params.system, skills].filter(Boolean).join("\n\n");
    }

    private plan(args: unknown): CallToolResult {
        const steps = (args as { steps?: unknown }).steps;
        if (!Array.isArray(steps) || !steps.every((s) => typeof s?.step === "string" && isPlanStatus(s?.status)))
            return { content: [{ type: "text", text: "update_plan takes steps, each with a step and a status." }], isError: true };
        this.send({ sessionUpdate: "plan",
            entries: steps.map((s) => ({ content: s.step as string, priority: "medium" as const, status: s.status as PlanStatus })) });
        return { content: [{ type: "text", text: "The plan is shown." }] };
    }

    private kindOf(name: string): ToolCallKind {
        if (name === "document_apply")
            return "edit";
        if (name === kPlanTool.name || name === kReadSkillTool.name)
            return "read";
        return this.tools.list(true).find((t) => t.name === name)?.annotations.readOnlyHint ? "read" : "other";
    }

    private request(caching: boolean | undefined): CompleteParams {
        const tools = [...this.tools.list(), kPlanTool, kReadSkillTool].map(definition);
        return {
            stream: this.nextStream(),
            providerId: this.model.providerId,
            request: {
                model: this.model.model,
                ...(this.system ? { system: this.system } : {}),
                messages: this.history,
                tools,
                ...(this.params.cacheKey ? { cacheKey: this.params.cacheKey } : {}),
                ...(caching === undefined ? {} : { caching }),
                ...(this.params.maxOutputTokens ? { maxOutputTokens: this.params.maxOutputTokens } : {}),
            },
        };
    }

    private keep(message: Message): void {
        this.flush();
        this.history.push(message);
        this.send({ sessionUpdate: "history", message });
    }

    private streamed(update: Update): void {
        if (update.kind !== "text")
            return;
        this.pendingText += update.text;
        this.flushTimer ??= setTimeout(() => this.flush(), kChunkMs);
    }

    private flush(): void {
        clearTimeout(this.flushTimer);
        this.flushTimer = undefined;
        if (!this.pendingText)
            return;
        const text = this.pendingText;
        this.pendingText = "";
        this.send({ sessionUpdate: "agent_message_chunk", content: { type: "text", text } });
    }
}

/** The sessions Qt opened, by id. */
export class AgentSessions {
    private readonly sessions = new Map<string, AgentSession>();
    private stream = 1 << 30;

    constructor(
        private readonly tools: (sessionId: string) => ToolSet,
        private readonly complete: Complete,
        private readonly notify: (method: string, params: SessionUpdateParams) => void,
        private readonly imageBudget: (providerId: string) => ImageBudget = () => kDefaultImageBudget,
        private readonly skills: (sessionId: string) => Skills = () => new Skills(),
    ) {}

    open(params: SessionNewParams): { sessionId: string } {
        this.sessions.get(params.sessionId)?.cancel();
        const id = params.sessionId;
        this.sessions.set(id, new AgentSession(id, params, this.tools(id), this.complete,
            (update) => this.notify(Method.sessionUpdate, { sessionId: id, update }), () => this.stream++,
            this.imageBudget, this.skills(id)));
        return { sessionId: id };
    }

    prompt(params: SessionPromptParams): Promise<SessionPromptResult> {
        return this.session(params.sessionId).prompt(params);
    }

    steer(params: SessionSteerParams): Promise<SessionSteerResult> {
        return this.session(params.sessionId).steer(params);
    }

    cancel(sessionId: string): void {
        this.sessions.get(sessionId)?.cancel();
    }

    close(sessionId: string): void {
        this.sessions.get(sessionId)?.cancel();
        this.sessions.delete(sessionId);
    }

    has(sessionId: string): boolean {
        return this.sessions.has(sessionId);
    }

    private session(id: string): AgentSession {
        const session = this.sessions.get(id);
        if (!session)
            throw new RpcError(ErrorCode.invalidParams, `No session is named "${id}"; open it with session/new`);
        return session;
    }
}
