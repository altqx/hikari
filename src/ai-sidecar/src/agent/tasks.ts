import { ErrorCode, Method, type TaskState } from "../protocol";
import { RpcError } from "../rpc";
import type { Tool } from "../tools/catalogue";
import type { CallToolResult, ToolSource } from "../tools/toolset";

export type Request = (method: string, params: unknown) => Promise<unknown>;

export const kDefaultWaitMs = 600_000;
export const kMaxWaitMs = 1_800_000;

const kRunningNext = "The result arrives as a message when it ends, so end your turn or carry on; don't poll it.";

const kScope = {
    type: "object",
    description: "The only Lines the sub-agent may change. Running sub-agents' scopes must not overlap.",
    properties: {
        document: { type: "integer", minimum: 1, description: "The Document, as documents_list gives it." },
        lines: {
            type: "array",
            items: { type: "integer", minimum: 1 },
            minItems: 1,
            description: "Line ids; without them the whole Document.",
        },
    },
    required: ["document"],
    additionalProperties: false,
};

const kTarget = {
    description: "Where it runs, as agent_capabilities lists it. Without one, on your own model or agent.",
    oneOf: [
        { type: "object", properties: { agent: { type: "string" } }, required: ["agent"], additionalProperties: false },
        {
            type: "object",
            properties: { providerId: { type: "string" }, model: { type: "string" } },
            required: ["providerId", "model"],
            additionalProperties: false,
        },
    ],
};

// MUST KILL: no agent tool is read-only, or compaction would clear an open task like a read.
function tool(name: string, description: string, properties: Record<string, unknown>, required: string[]): Tool {
    return {
        name,
        description,
        inputSchema: { type: "object", properties, required, additionalProperties: false },
        annotations: { readOnlyHint: false },
        _meta: { "hikari/group": "agent" },
    } as Tool;
}

const kTaskId = { taskId: { type: "string", description: "The taskId agent_delegate gave." } };

export const kTaskTools: Tool[] = [
    tool("agent_capabilities", "Lists where sub-agents can run now, how many run, the limit and your mode.", {}, []),
    tool("agent_delegate",
        "Hands a self-contained task to a sub-agent. It sees only `task`, never this conversation, so say everything "
            + "it needs. It can't delegate, it edits only its scope, and at most 4 run at once. By default it runs in "
            + "the background and its result comes back as a message; with wait, this call returns its result.",
        {
            task: { type: "string", minLength: 1, description: "Everything the sub-agent needs to know." },
            title: { type: "string", maxLength: 80, description: "A few words the user sees on its card." },
            role: { type: "string", maxLength: 80, description: "Such as translator or timer." },
            target: kTarget,
            mode: { enum: ["manual", "auto", "full"], description: "Never broader than yours." },
            wait: { type: "boolean", description: "Wait for its result." },
            timeoutMs: { type: "integer", minimum: 1, maximum: kMaxWaitMs, description: "How long to wait; 10 minutes by default." },
            scope: kScope,
            clientRequestId: { type: "string", description: "The same id gives the same task back when you retry." },
        }, ["task", "scope"]),
    tool("agent_task_status", "A sub-agent's state and result so far.", kTaskId, ["taskId"]),
    tool("agent_task_cancel", "Stops a sub-agent. Its partial result comes back, and its edits stay in the review.", kTaskId,
        ["taskId"]),
];

function text(value: unknown): CallToolResult {
    return { content: [{ type: "text", text: JSON.stringify(value) }] };
}

function failure(message: string): CallToolResult {
    return { content: [{ type: "text", text: message }], isError: true };
}

function isObject(value: unknown): value is Record<string, unknown> {
    return typeof value === "object" && value !== null && !Array.isArray(value);
}

function stated(state: TaskState): CallToolResult {
    return text(state.status === "running" || state.status === "needs_you" ? { ...state, next: kRunningNext } : state);
}

/** The sub-agent tools of one session. Qt decides every request; the sidecar only passes them on. */
export class TaskTools implements ToolSource {
    readonly group = "agent";
    readonly description = "Sub-agents: hand self-contained tasks to other agents and follow them.";
    readonly offered = true;

    constructor(private readonly sessionId: string, private readonly request: Request) {}

    tools(): Tool[] {
        return kTaskTools;
    }

    async call(name: string, args: Record<string, unknown>): Promise<CallToolResult> {
        try {
            return await this.run(name, args);
        } catch (error) {
            if (error instanceof RpcError && error.code === ErrorCode.taskRefused)
                return failure(error.message);
            throw error;
        }
    }

    private async run(name: string, args: Record<string, unknown>): Promise<CallToolResult> {
        const sessionId = this.sessionId;
        switch (name) {
        case "agent_capabilities":
            return text(await this.request(Method.taskCapabilities, { sessionId }));
        case "agent_task_status":
        case "agent_task_cancel": {
            if (typeof args.taskId !== "string")
                return failure(`${name} needs the taskId agent_delegate gave.`);
            const method = name === "agent_task_status" ? Method.taskStatus : Method.taskCancel;
            return stated(await this.request(method, { sessionId, taskId: args.taskId }) as TaskState);
        }
        case "agent_delegate":
            return this.delegate(args);
        }
        return failure(`No tool is named "${name}".`);
    }

    private async delegate(args: Record<string, unknown>): Promise<CallToolResult> {
        const { wait, timeoutMs, ...rest } = args;
        if (typeof rest.task !== "string" || rest.task.length === 0)
            return failure("agent_delegate needs the task.");
        if (!isObject(rest.scope) || typeof rest.scope.document !== "number")
            return failure("agent_delegate needs a scope with a document.");
        if (wait !== undefined && typeof wait !== "boolean")
            return failure("agent_delegate: wait is true or false.");
        if (timeoutMs !== undefined && (typeof timeoutMs !== "number" || !Number.isInteger(timeoutMs)))
            return failure("agent_delegate: timeoutMs is a whole number of milliseconds.");
        const started = await this.request(Method.taskRequest, { ...rest, sessionId: this.sessionId }) as TaskState;
        if (!wait || started.status !== "running" && started.status !== "needs_you")
            return stated(started);
        const ms = Math.min(Math.max(timeoutMs ?? kDefaultWaitMs, 1), kMaxWaitMs);
        return stated(await this.request(Method.taskWait,
            { sessionId: this.sessionId, taskId: started.taskId, timeoutMs: ms }) as TaskState);
    }
}
