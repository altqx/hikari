import { expect, test } from "bun:test";
import { Tiktoken } from "js-tiktoken/lite";
import o200k from "js-tiktoken/ranks/o200k_base";

import type { Request, ToolDefinition, Wire } from "../../src/providers/types";
import { ToolSet, type HikariCalls } from "../../src/tools/toolset";
import { WebSearch } from "../../src/tools/web";
import { Harness, TestRelay } from "../providers/harness";
import { data, sse } from "../providers/mock";

const h = new Harness().install();
const kBudget = 3_000;
const kNoHa1: HikariCalls = { call: () => Promise.reject(new Error("no HA1 here")) };

const kEnd: Record<Wire, string> = {
    "chat-completions": data({ choices: [{ delta: {}, finish_reason: "stop" }] }),
    responses: sse("response.completed", { type: "response.completed", response: { usage: {} } }),
    "anthropic-messages": sse("message_stop", { type: "message_stop" }),
    gemini: data({ candidates: [{ content: { parts: [{ text: "ok" }] }, finishReason: "STOP" }] }),
};

function upFront(): ToolDefinition[] {
    const tools = new ToolSet(kNoHa1, [new WebSearch(new TestRelay(), () => "exa")]);
    return tools.list().map((t) => ({ name: t.name, description: t.description ?? "", parametersJson: JSON.stringify(t.inputSchema) }));
}

test.each(Object.keys(kEnd) as Wire[])("the up-front tool block on %s stays within 3,000 tokens", async (wire) => {
    const tokens = new Tiktoken(o200k);
    const request: Request = {
        model: "m", tools: upFront(), webSearch: true,
        messages: [{ author: "user", parts: [{ type: "text", text: "Hi" }] }],
    };
    h.mock.replies.push({ chunks: [kEnd[wire]] });
    await h.stream(wire, request);
    const block = JSON.stringify((h.mock.received[0]!.json() as { tools: unknown[] }).tools);
    expect(block.length).toBeGreaterThan(0);
    expect(tokens.encode(block).length).toBeLessThanOrEqual(kBudget);
});
