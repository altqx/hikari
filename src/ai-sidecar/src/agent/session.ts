import {
    applyCompaction, clearedResults, kClearedTarget, kCompactAt, kDefaultContextTokens, keptRanges, messageChars,
    prefixChars, summaryMessage, summaryRequest, TokenCount, withStubs,
} from "./compaction";
import {
    type CompactedUpdate,
    ErrorCode,
    Method,
    type PlanStatus,
    type SessionCompactParams,
    type SessionCompactResult,
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
import { kReadSkillTool, Skills, withSkills } from "../skills/skills";
import type { Tool } from "../tools/catalogue";
import { type ImageBudget, kDefaultImageBudget } from "../tools/images";
import type { CallToolResult, HikariCalls, ToolSet } from "../tools/toolset";

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

function reasonOf(error: unknown): string {
    if (error instanceof ProviderError)
        return error.detail || error.kind;
    return error instanceof Error ? error.message : String(error);
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
    private webSearch: boolean;
    private readonly tools: ToolSet;
    private turn?: Turn;
    private pendingText = "";
    private flushTimer?: ReturnType<typeof setTimeout>;
    private system?: string;
    private contextTokens: number;
    private autoCompact: boolean;
    private readonly count = new TokenCount();
    private compactedTo?: number;
    private compacting?: AbortController;

    constructor(
        readonly id: string,
        private readonly params: SessionNewParams,
        tools: (webSearch: () => boolean) => ToolSet,
        private readonly complete: Complete,
        private readonly send: (update: SessionUpdate) => void,
        private readonly nextStream: () => number,
        private readonly imageBudget: (providerId: string) => ImageBudget,
        private readonly skills: Skills = new Skills(),
    ) {
        this.history = [...(params.history ?? [])];
        this.model = params.model;
        this.webSearch = params.webSearch === true;
        this.contextTokens = params.contextTokens || kDefaultContextTokens;
        this.autoCompact = params.autoCompact === true;
        this.tools = tools(() => this.webSearch);
    }

    get running(): boolean {
        return this.turn !== undefined || this.compacting !== undefined;
    }

    async prompt(params: SessionPromptParams): Promise<SessionPromptResult> {
        if (this.running)
            throw new RpcError(ErrorCode.busy, "A turn is already running in this session");
        if (params.model)
            this.model = params.model;
        if (params.webSearch !== undefined)
            this.webSearch = params.webSearch;
        if (params.contextTokens)
            this.contextTokens = params.contextTokens;
        if (params.autoCompact !== undefined)
            this.autoCompact = params.autoCompact;
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
        this.compacting?.abort();
    }

    async compact(params: SessionCompactParams): Promise<SessionCompactResult> {
        if (this.running)
            throw new RpcError(ErrorCode.busy, "A turn is running in this session; compact it once the turn ends");
        const controller = new AbortController();
        this.compacting = controller;
        try {
            this.system ??= await this.instructions();
            if (params.apply === false)
                return { compacted: false, summary: await this.summarise(this.history, params.instruction, controller.signal) };
            const update = await this.compaction(controller.signal, params.instruction, true);
            if (!update)
                return { compacted: false };
            this.apply(update);
            return { compacted: true, ...(update.summary === undefined ? {} : { summary: update.summary }) };
        } catch (error) {
            if (controller.signal.aborted)
                throw new RpcError(ErrorCode.requestCancelled, "The user stopped the compaction");
            throw error;
        } finally {
            this.compacting = undefined;
        }
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
            if (this.dueForCompaction()) {
                try {
                    const update = await this.compaction(signal, undefined, false);
                    if (update)
                        this.apply(update);
                    this.compactedTo = this.estimate();
                } catch (error) {
                    if (signal.aborted)
                        return "cancelled";
                    this.send({ sessionUpdate: "compaction_failed", reason: reasonOf(error) });
                    return "compaction_failed";
                }
            }
            let finished: Finished;
            const chars = this.chars();
            try {
                finished = await this.complete(this.request(caching), signal, (_stream, update) => this.streamed(update));
            } catch (error) {
                if (signal.aborted && (!(error instanceof ProviderError) || error.kind === "cancelled"))
                    return "cancelled";
                throw error;
            }
            this.flush();
            this.sources(finished);
            this.send({ sessionUpdate: "usage", usage: finished.usage });
            if (finished.usage.inputTokens > 0)
                this.count.measure(finished.usage.inputTokens, chars);
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
            if (result.searchFailure)
                this.send({ sessionUpdate: "search_failed", ...result.searchFailure });
            const text = resultText(result);
            this.send({ sessionUpdate: "tool_call_update", toolCallId: call.id, status: result.isError ? "failed" : "completed",
                content: [{ type: "content", content: { type: "text", text } }] });
            results.push({ type: "toolResult", callId: call.id, content: text, ...(result.isError ? { isError: true } : {}) });
        }
        return results;
    }

    private instructions(): Promise<string> {
        return withSkills(this.params.system, this.skills);
    }

    private sources(finished: Finished): void {
        const { citations, searchSuggestions, searches } = finished;
        if (!citations?.length && !searchSuggestions && !searches?.length)
            return;
        this.send({ sessionUpdate: "sources", textLength: finished.text.length,
            ...(citations?.length ? { citations } : {}), ...(searchSuggestions ? { searchSuggestions } : {}),
            ...(searches?.length ? { searches } : {}) });
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

    private definitions(): ToolDefinition[] {
        return [...this.tools.list(), kPlanTool, kReadSkillTool].map(definition);
    }

    private chars(messages: readonly Message[] = this.history): number {
        return prefixChars(this.system, this.definitions()) + messageChars(messages);
    }

    private estimate(messages: readonly Message[] = this.history): number {
        return this.count.estimate(this.chars(messages));
    }

    private dueForCompaction(): boolean {
        const estimate = this.estimate();
        if (!this.autoCompact || estimate < kCompactAt * this.contextTokens)
            return false;
        return this.compactedTo === undefined || estimate - this.compactedTo >= (1 - kCompactAt) * this.contextTokens;
    }

    /**
     * Stage 1 clears old read-tool results; stage 2, when that leaves 60% or more or the user asked, summarises what
     * lies between the first user message and the last two. Nothing changes until the update is applied.
     */
    private async compaction(signal: AbortSignal, instruction: string | undefined, manual: boolean)
        : Promise<CompactedUpdate | undefined> {
        const before = this.estimate();
        const { headEnd, tailStart } = keptRanges(this.history);
        const stubs = clearedResults(this.history, tailStart, (tool) => this.kindOf(tool) === "read");
        const cleared = withStubs(this.history, stubs);
        const clearedTokens = this.estimate(cleared);
        const stageOne: CompactedUpdate = { sessionUpdate: "compacted", stage: 1, before, after: clearedTokens, stubs };
        if (tailStart <= headEnd || (!manual && clearedTokens < kClearedTarget * this.contextTokens))
            return stubs.length > 0 ? stageOne : undefined;
        const summary = await this.summarise(cleared.slice(0, tailStart), instruction, signal);
        const update: CompactedUpdate = { ...stageOne, stage: 2, summary, message: summaryMessage(summary), headEnd,
            keptFrom: tailStart };
        update.after = this.estimate(applyCompaction(this.history, update));
        return update;
    }

    /** The thread's own model writes the summary; a failed request is retried once. */
    private async summarise(messages: readonly Message[], instruction: string | undefined, signal: AbortSignal)
        : Promise<string> {
        let failure: unknown;
        for (let attempt = 0; attempt < 2; ++attempt) {
            try {
                const finished = await this.complete(summaryRequest(messages, instruction, this.model, this.nextStream()),
                    signal, () => {});
                this.send({ sessionUpdate: "usage", usage: finished.usage, compaction: true });
                if (finished.text.trim())
                    return finished.text.trim();
                failure = new ProviderError("malformed", "The summary came back empty");
            } catch (error) {
                if (signal.aborted)
                    throw error;
                failure = error;
            }
        }
        throw failure;
    }

    private apply(update: CompactedUpdate): void {
        this.history.splice(0, this.history.length, ...applyCompaction(this.history, update));
        this.send(update);
    }

    private request(caching: boolean | undefined): CompleteParams {
        const tools = this.definitions();
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
                ...(this.webSearch ? { webSearch: true } : {}),
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

export interface Session {
    prompt(params: SessionPromptParams): Promise<SessionPromptResult>;
    steer(params: SessionSteerParams): Promise<SessionSteerResult>;
    cancel(): void;
    close?(): void;
    compact?(params: SessionCompactParams): Promise<SessionCompactResult>;
    /** False once the user stopped the turn, or when none runs: an agent's late calls then change nothing. */
    takesToolCalls?(): boolean;
}

/** An agent's tools channel, which refuses every call while `session` takes none. */
export function whileTurnRuns(calls: HikariCalls, session: () => Pick<Session, "takesToolCalls"> | undefined): HikariCalls {
    return {
        call: (operation, params) => session()?.takesToolCalls?.() === false
            ? Promise.reject(new RpcError(ErrorCode.requestCancelled, "The user stopped this turn"))
            : calls.call(operation, params),
    };
}

export type ExternalAgent = (id: string, params: SessionNewParams, send: (update: SessionUpdate) => void) => Session;

/** The sessions Qt opened, by id. */
export class AgentSessions {
    private readonly sessions = new Map<string, Session>();
    private readonly opened = new Map<string, SessionNewParams>();
    private stream = 1 << 30;

    constructor(
        private readonly tools: (sessionId: string, webSearch: () => boolean, params: SessionNewParams) => ToolSet,
        private readonly complete: Complete,
        private readonly notify: (method: string, params: SessionUpdateParams) => void,
        private readonly imageBudget: (providerId: string) => ImageBudget = () => kDefaultImageBudget,
        private readonly agents: Partial<Record<"claude" | "codex" | "acp", ExternalAgent>> = {},
        private readonly skills: (sessionId: string) => Skills = () => new Skills(),
    ) {}

    open(params: SessionNewParams): { sessionId: string } {
        this.end(params.sessionId);
        const id = params.sessionId;
        this.opened.set(id, params);
        const send = (update: SessionUpdate) => this.notify(Method.sessionUpdate, { sessionId: id, update });
        if (params.agent !== undefined) {
            const agent = params.agent.startsWith("acp:") ? this.agents.acp
                : this.agents[params.agent as "claude" | "codex"];
            if (!agent)
                throw new RpcError(ErrorCode.invalidParams, `This sidecar can't run ${params.agent}`);
            this.sessions.set(id, agent(id, params, send));
            return { sessionId: id };
        }
        this.sessions.set(id, new AgentSession(id, params, (webSearch) => this.tools(id, webSearch, params), this.complete, send,
            () => this.stream++,
            this.imageBudget, this.skills(id)));
        return { sessionId: id };
    }

    prompt(params: SessionPromptParams): Promise<SessionPromptResult> {
        return this.session(params.sessionId).prompt(params);
    }

    steer(params: SessionSteerParams): Promise<SessionSteerResult> {
        return this.session(params.sessionId).steer(params);
    }

    compact(params: SessionCompactParams): Promise<SessionCompactResult> {
        const session = this.session(params.sessionId);
        if (!session.compact)
            throw new RpcError(ErrorCode.invalidParams, "This agent compacts its own conversation");
        return session.compact(params);
    }

    cancel(sessionId: string): void {
        this.sessions.get(sessionId)?.cancel();
    }

    close(sessionId: string): void {
        this.end(sessionId);
        this.sessions.delete(sessionId);
        this.opened.delete(sessionId);
    }

    /** What Qt opened the session with. */
    params(sessionId: string): SessionNewParams | undefined {
        return this.opened.get(sessionId);
    }

    private end(sessionId: string): void {
        const session = this.sessions.get(sessionId);
        session?.cancel();
        session?.close?.();
    }

    has(sessionId: string): boolean {
        return this.sessions.has(sessionId);
    }

    get(sessionId: string): Session | undefined {
        return this.sessions.get(sessionId);
    }

    private session(id: string): Session {
        const session = this.sessions.get(id);
        if (!session)
            throw new RpcError(ErrorCode.invalidParams, `No session is named "${id}"; open it with session/new`);
        return session;
    }
}
