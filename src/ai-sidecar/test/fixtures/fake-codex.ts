// Stands in for `codex app-server`: each turn's text is a script of Hikari tool calls, which it makes over the MCP
// channel the `-c mcp_servers.hikari.*` arguments name, as Codex 0.167.0 does.
import { appendFileSync } from "node:fs";

interface Script {
    reply?: string;
    calls?: { tool: string; arguments: unknown }[];
    /** A built-in file change, which Codex asks the client to approve first. */
    fileChange?: string;
}

const log = (line: string) => process.env.FAKE_CODEX_LOG && appendFileSync(process.env.FAKE_CODEX_LOG, line + "\n");
log(`args ${JSON.stringify(process.argv.slice(2))}`);

function setting(name: string): string | undefined {
    const args = process.argv.slice(2);
    for (let i = 0; i + 1 < args.length; ++i)
        if (args[i] === "-c" && args[i + 1]!.startsWith(`${name}=`))
            return JSON.parse(args[i + 1]!.slice(name.length + 1)) as string;
    return undefined;
}

const url = setting("mcp_servers.hikari.url");
const token = process.env[setting("mcp_servers.hikari.bearer_token_env_var") ?? ""];
let mcpId = 0;
let turns = 0;
let interrupted = false;
let usage = { inputTokens: 0, cachedInputTokens: 0, cacheWriteInputTokens: 0, outputTokens: 0, totalTokens: 0,
    reasoningOutputTokens: 0 };

const send = (message: unknown) => process.stdout.write(JSON.stringify(message) + "\n");
let requestId = 0;
const asked = new Map<number, (answer: unknown) => void>();
const ask = (method: string, params: unknown) => new Promise<unknown>((resolve) => {
    asked.set(++requestId, resolve);
    send({ id: requestId, method, params });
});
const notify = (method: string, params: unknown) => send({ method, params });

async function mcp(method: string, params: unknown): Promise<{ result?: unknown; error?: { message: string } }> {
    const response = await fetch(url!, { method: "POST", headers: { "content-type": "application/json",
        authorization: `Bearer ${token}` }, body: JSON.stringify({ jsonrpc: "2.0", id: ++mcpId, method, params }) });
    return await response.json() as never;
}

async function run(threadId: string, turnId: string, script: Script): Promise<void> {
    await mcp("initialize", { protocolVersion: "2025-06-18", capabilities: {}, clientInfo: { name: "codex-mcp-client" } });
    if (script.fileChange) {
        const item = { type: "fileChange", id: `patch-${turnId}`, changes: [{ path: script.fileChange, kind: "update" }],
            status: "inProgress" };
        notify("item/started", { threadId, turnId, item });
        const answer = await ask("item/fileChange/requestApproval", { threadId, turnId, itemId: item.id });
        const accepted = (answer as { result?: { decision?: string } }).result?.decision === "accept";
        log(`fileChange ${accepted ? "applied" : "refused"} ${JSON.stringify(answer)}`);
        notify("item/completed", { threadId, turnId, item: { ...item, status: accepted ? "completed" : "declined" } });
    }
    for (const [n, call] of (script.calls ?? []).entries()) {
        if (interrupted)
            break;
        const item = { type: "mcpToolCall", id: `call-${turnId}-${n}`, server: "hikari", tool: call.tool,
            arguments: call.arguments, status: "inProgress", result: null, error: null };
        notify("item/started", { threadId, turnId, item });
        const answer = await mcp("tools/call", { name: call.tool, arguments: call.arguments });
        const failed = answer.error !== undefined || (answer.result as { isError?: boolean }).isError === true;
        notify("item/completed", { threadId, turnId, item: { ...item, status: failed ? "failed" : "completed",
            result: answer.error ? null : answer.result, error: answer.error ? { message: answer.error.message } : null } });
    }
    if (!interrupted && script.reply) {
        const id = `msg-${turnId}`;
        notify("item/agentMessage/delta", { threadId, turnId, itemId: id, delta: script.reply });
        notify("item/completed", { threadId, turnId, item: { type: "agentMessage", id, text: script.reply } });
    }
    usage = { ...usage, inputTokens: usage.inputTokens + 10, cachedInputTokens: usage.cachedInputTokens + 4,
        outputTokens: usage.outputTokens + 2 };
    notify("thread/tokenUsage/updated", { threadId, turnId, tokenUsage: { total: usage, last: usage, modelContextWindow: null } });
    notify("turn/completed", { threadId, turn: { id: turnId, status: interrupted ? "interrupted" : "completed", error: null,
        items: [] } });
}

let buffered = "";
process.stdin.on("data", (chunk: Buffer) => {
    buffered += chunk.toString("utf8");
    for (let newline = buffered.indexOf("\n"); newline >= 0; newline = buffered.indexOf("\n")) {
        const line = buffered.slice(0, newline);
        buffered = buffered.slice(newline + 1);
        const message = JSON.parse(line) as { id?: number; method?: string; params?: Record<string, unknown> };
        if (!message.method && message.id !== undefined && asked.has(message.id)) {
            asked.get(message.id)!(message);
            asked.delete(message.id);
            continue;
        }
        if (message.method)
            log(`${message.method} ${JSON.stringify(message.params ?? null)}`);
        const answer = (result: unknown) => send({ id: message.id, result });
        switch (message.method) {
        case "initialize":
            answer({ userAgent: "fake-codex/0.167.0", codexHome: "/codex", platformFamily: "unix", platformOs: "linux" });
            break;
        case "thread/start":
            answer({ thread: { id: "thread-1" }, model: "gpt-fake" });
            break;
        case "thread/resume":
            answer({ thread: { id: message.params!.threadId }, model: "gpt-fake" });
            break;
        case "turn/start": {
            const turnId = `turn-${++turns}`;
            interrupted = false;
            answer({ turn: { id: turnId, status: "inProgress", items: [], error: null } });
            const text = (message.params!.input as { text: string }[])[0]!.text;
            if (text === "exit")
                process.exit(3);
            void run(String(message.params!.threadId), turnId, JSON.parse(text) as Script);
            break;
        }
        case "turn/steer":
            answer({ turnId: message.params!.expectedTurnId });
            break;
        case "turn/interrupt":
            interrupted = true;
            answer({});
            break;
        default:
            if (message.id !== undefined && message.method)
                send({ id: message.id, error: { code: -32601, message: "unknown" } });
        }
    }
});
