// A stand-in for `claude --input-format stream-json`, speaking the Agent SDK's control protocol as recorded from
// Claude Code 2.1.322. Each user message is a script: {"calls": [{"name", "arguments"}], "reply": "..."}.
import { appendFileSync } from "node:fs";
import { createInterface } from "node:readline";

const log = process.env.FAKE_CLAUDE_LOG;
const session = "fake-session";
const pending = new Map<string, (response: any) => void>();
let hooks: Record<string, string[]> = {};
let next = 0;

function send(message: unknown): void {
    process.stdout.write(`${JSON.stringify(message)}\n`);
}

function ask(request: Record<string, unknown>): Promise<any> {
    const id = `fake-${++next}`;
    send({ type: "control_request", request_id: id, request });
    return new Promise((resolve) => pending.set(id, resolve));
}

async function hook(event: string, input: Record<string, unknown>): Promise<any> {
    let out: any = {};
    for (const id of hooks[event] ?? [])
        out = { ...out, ...(await ask({ subtype: "hook_callback", callback_id: id, tool_use_id: input.tool_use_id,
            input: { session_id: session, hook_event_name: event, ...input } })) };
    return out;
}

let interrupted = false;

async function turn(script: { calls?: { name: string; arguments?: unknown }[]; reply?: string }): Promise<void> {
    interrupted = false;
    send({ type: "system", subtype: "init", session_id: session, tools: [], mcp_servers: [] });
    for (const [i, call] of (script.calls ?? []).entries()) {
        if (interrupted)
            break;
        const id = `toolu_${i}`;
        send({ type: "assistant", parent_tool_use_id: null, session_id: session,
            message: { role: "assistant", content: [{ type: "tool_use", id, name: call.name, input: call.arguments ?? {} }] } });
        const input = { tool_name: call.name, tool_input: call.arguments ?? {}, tool_use_id: id };
        const before = await hook("PreToolUse", input);
        if (before?.hookSpecificOutput?.permissionDecision === "deny") {
            log && appendFileSync(log, `denied ${call.name}\n`);
            continue;
        }
        const [, server, tool] = /^mcp__([^_]+)__(.+)$/.exec(call.name) ?? [];
        if (!server) {
            log && appendFileSync(log, `ran built-in ${call.name}\n`);
            continue;
        }
        const answer = await ask({ subtype: "mcp_message", server_name: server,
            message: { jsonrpc: "2.0", id: 100 + i, method: "tools/call", params: { name: tool, arguments: call.arguments ?? {} } } });
        const result = answer?.mcp_response?.result;
        if (!result || result.isError)
            await hook("PostToolUseFailure", { ...input, error: result?.content?.[0]?.text ?? "failed" });
        else
            await hook("PostToolUse", { ...input, tool_response: result.content });
    }
    if (interrupted) {
        send({ type: "result", subtype: "error_during_execution", is_error: true, errors: [], session_id: session,
            usage: { input_tokens: 1, output_tokens: 0 } });
        return;
    }
    send({ type: "assistant", parent_tool_use_id: null, session_id: session,
        message: { role: "assistant", content: [{ type: "text", text: script.reply ?? "done" }] } });
    send({ type: "result", subtype: "success", is_error: false, result: script.reply ?? "done", session_id: session,
        total_cost_usd: 0.001, usage: { input_tokens: 10, output_tokens: 2, cache_read_input_tokens: 4,
            cache_creation_input_tokens: 0 } });
}

log && appendFileSync(log, `args ${JSON.stringify(process.argv.slice(2))}\n`);
for await (const line of createInterface({ input: process.stdin })) {
    if (!line.trim())
        continue;
    const message = JSON.parse(line);
    if (message.type === "control_response") {
        pending.get(message.response.request_id)?.(message.response.response);
        pending.delete(message.response.request_id);
    } else if (message.type === "control_request" && message.request.subtype === "initialize") {
        hooks = Object.fromEntries(Object.entries(message.request.hooks ?? {}).map(([event, matchers]) =>
            [event, (matchers as { hookCallbackIds: string[] }[]).flatMap((m) => m.hookCallbackIds)]));
        send({ type: "control_response", response: { subtype: "success", request_id: message.request_id, response: {} } });
    } else if (message.type === "control_request" && message.request.subtype === "interrupt") {
        interrupted = true;
        send({ type: "control_response", response: { subtype: "success", request_id: message.request_id, response: {} } });
    } else if (message.type === "control_request") {
        send({ type: "control_response", response: { subtype: "success", request_id: message.request_id, response: {} } });
    } else if (message.type === "user") {
        const text = typeof message.message.content === "string" ? message.message.content
            : message.message.content.find((c: { type: string }) => c.type === "text")?.text;
        void turn(JSON.parse(text));
    }
}
