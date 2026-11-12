// Stands in for an ACP agent such as `opencode acp`: each prompt is a script of Hikari tool calls, which it makes over
// the HTTP MCP server `session/new` names.
import { appendFileSync } from "node:fs";

interface Script {
    reply?: string;
    calls?: { tool: string; arguments: unknown }[];
    askFor?: string;
}

interface Server {
    type?: string;
    name: string;
    url?: string;
    headers?: { name: string; value: string }[];
}

const log = (line: string) => process.env.FAKE_ACP_LOG && appendFileSync(process.env.FAKE_ACP_LOG, line + "\n");
log(`args ${JSON.stringify(process.argv.slice(2))}`);

let server: Server | undefined;
let mcpId = 0;
let nextId = 1;
let sessions = 0;
let cancelled = false;
const waiting = new Map<number, (result: unknown) => void>();

const send = (message: object) => process.stdout.write(JSON.stringify({ jsonrpc: "2.0", ...message }) + "\n");
const update = (sessionId: string, u: object) => send({ method: "session/update", params: { sessionId, update: u } });
const ask = (method: string, params: unknown) => new Promise<unknown>((resolve) => {
    const id = nextId++;
    waiting.set(id, resolve);
    send({ id, method, params });
});

async function mcp(method: string, params: unknown): Promise<{ result?: unknown; error?: { message: string } }> {
    const headers: Record<string, string> = { "content-type": "application/json" };
    for (const h of server!.headers ?? [])
        headers[h.name] = h.value;
    const response = await fetch(server!.url!, { method: "POST", headers,
        body: JSON.stringify({ jsonrpc: "2.0", id: ++mcpId, method, params }) });
    return await response.json() as never;
}

async function run(sessionId: string, script: Script): Promise<string> {
    if (script.askFor) {
        const answer = await ask("session/request_permission", { sessionId,
            toolCall: { toolCallId: "ask-1", title: script.askFor, kind: "execute" },
            options: [{ optionId: "yes", name: "Allow", kind: "allow_once" }, { optionId: "no", name: "Reject", kind: "reject_once" }] });
        log(`permission ${JSON.stringify(answer)}`);
    }
    if (script.calls?.length)
        await mcp("initialize", { protocolVersion: "2025-06-18", capabilities: {}, clientInfo: { name: "fake-acp" } });
    for (const [n, call] of (script.calls ?? []).entries()) {
        if (cancelled)
            return "cancelled";
        const toolCallId = `call-${n}`;
        update(sessionId, { sessionUpdate: "tool_call", toolCallId, title: `hikari_${call.tool}`, kind: "other",
            status: "pending", rawInput: call.arguments });
        const answer = await mcp("tools/call", { name: call.tool, arguments: call.arguments });
        const failed = answer.error !== undefined || (answer.result as { isError?: boolean }).isError === true;
        update(sessionId, { sessionUpdate: "tool_call_update", toolCallId, status: failed ? "failed" : "completed",
            content: [{ type: "content", content: { type: "text", text: answer.error?.message ?? "ok" } }] });
    }
    if (cancelled)
        return "cancelled";
    if (script.reply)
        update(sessionId, { sessionUpdate: "agent_message_chunk", content: { type: "text", text: script.reply } });
    return "end_turn";
}

let buffered = "";
process.stdin.on("data", (chunk: Buffer) => {
    buffered += chunk.toString("utf8");
    for (let newline = buffered.indexOf("\n"); newline >= 0; newline = buffered.indexOf("\n")) {
        const line = buffered.slice(0, newline);
        buffered = buffered.slice(newline + 1);
        const message = JSON.parse(line) as { id?: number; method?: string; params?: Record<string, unknown>; result?: unknown };
        if (!message.method) {
            waiting.get(message.id!)?.(message.result ?? null);
            waiting.delete(message.id!);
            continue;
        }
        log(`${message.method} ${JSON.stringify(message.params ?? null)}`);
        const answer = (result: unknown) => send({ id: message.id, result });
        switch (message.method) {
        case "initialize":
            answer({ protocolVersion: 1, agentCapabilities: { loadSession: process.env.FAKE_ACP_LOAD !== "0",
                mcpCapabilities: { http: true, sse: false }, promptCapabilities: { image: false } }, authMethods: [] });
            break;
        case "session/new":
        case "session/load": {
            server = (message.params!.mcpServers as Server[]).find((s) => s.name === "hikari");
            answer(message.method === "session/new" ? { sessionId: `session-${++sessions}` } : null);
            break;
        }
        case "session/prompt": {
            cancelled = false;
            const prompt = message.params!.prompt as { type: string; text?: string }[];
            const text = prompt[prompt.length - 1]!.text!;
            if (text === "exit")
                process.exit(3);
            void run(String(message.params!.sessionId), JSON.parse(text) as Script).then((stopReason) => answer({ stopReason }));
            break;
        }
        case "session/cancel":
            cancelled = true;
            break;
        default:
            if (message.id !== undefined)
                send({ id: message.id, error: { code: -32601, message: "unknown" } });
        }
    }
});
