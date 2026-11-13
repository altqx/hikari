import { AgentSessions } from "../../src/agent/session";
import type { ToolSet } from "../../src/tools/toolset";
import type { Harness } from "../providers/harness";
import { type Reply, sse } from "../providers/mock";

type Json = Record<string, any>;

function reply(blocks: Json[], stop: "tool_use" | "end_turn"): Reply {
    return {
        chunks: [
            sse("message_start", { type: "message_start", message: { usage: { input_tokens: 10, output_tokens: 1 } } }),
            ...blocks.flatMap((block, index) => [
                sse("content_block_start", { type: "content_block_start", index, content_block: block.type === "text"
                    ? { type: "text", text: "" } : { type: "tool_use", id: block.id, name: block.name, input: {} } }),
                sse("content_block_delta", { type: "content_block_delta", index, delta: block.type === "text"
                    ? { type: "text_delta", text: block.text }
                    : { type: "input_json_delta", partial_json: JSON.stringify(block.input) } }),
                sse("content_block_stop", { type: "content_block_stop", index }),
            ]),
            sse("message_delta", { type: "message_delta", delta: { stop_reason: stop }, usage: { output_tokens: 5 } }),
            sse("message_stop", { type: "message_stop" }),
        ],
    };
}

export const toolUse = (id: string, name: string, input: Json) => reply([{ type: "tool_use", id, name, input }], "tool_use");
export const answer = (text: string) => reply([{ type: "text", text }], "end_turn");

export async function scriptedTurn(h: Harness, tools: (sessionId: string) => ToolSet, replies: Reply[],
    delegate = false): Promise<Json[]> {
    h.mock.replies.push(...replies);
    const service = h.service("anthropic-messages");
    const sessions = new AgentSessions((id) => tools(id), (params, signal, emit) => service.complete(params, signal, emit),
        () => {});
    sessions.open({ sessionId: "t1", model: { providerId: "mock", model: "claude-opus-5-5" }, ...(delegate ? { delegate } : {}) });
    await sessions.prompt({ sessionId: "t1", prompt: [{ type: "text", text: "Go" }] });
    return h.mock.received.map((r) => r.json() as Json);
}

export const toolNames = (body: Json): string[] => body.tools.map((t: Json) => t.name);

export function resultOf(body: Json, callId: string): Json | undefined {
    return body.messages.flatMap((m: Json) => (Array.isArray(m.content) ? m.content : []))
        .find((b: Json) => b.type === "tool_result" && b.tool_use_id === callId);
}
