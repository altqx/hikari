import { expect, test } from "bun:test";
import { Tiktoken } from "js-tiktoken/lite";
import o200k from "js-tiktoken/ranks/o200k_base";

import { kPlanTool } from "../../src/agent/session";
import { TaskTools } from "../../src/agent/tasks";
import { serverGroupDescription } from "../../src/mcp/client";
import type { Request, ToolDefinition, Wire } from "../../src/providers/types";
import { kReadSkillTool, Skills } from "../../src/skills/skills";
import { kCatalogue, tool } from "../../src/tools/catalogue";
import { ToolSet, type HikariCalls, type ToolSource } from "../../src/tools/toolset";
import { WebSearch } from "../../src/tools/web";
import { Harness, TestRelay } from "../providers/harness";
import { data, sse } from "../providers/mock";

const h = new Harness().install();
const kBudget = 5_000;
const kNoHa1: HikariCalls = { call: () => Promise.reject(new Error("no HA1 here")) };

const kEnd: Record<Wire, string> = {
    "chat-completions": data({ choices: [{ delta: {}, finish_reason: "stop" }] }),
    responses: sse("response.completed", { type: "response.completed", response: { usage: {} } }),
    "anthropic-messages": sse("message_stop", { type: "message_stop" }),
    gemini: data({ candidates: [{ content: { parts: [{ text: "ok" }] }, finishReason: "STOP" }] }),
};

function server(n: number): ToolSource {
    const id = `server_${n}_with_a_long_id`;
    return {
        group: `mcp_${id}`,
        description: serverGroupDescription(`A user's MCP server number ${n} with a name well past forty characters`),
        tools: () => [tool(`${id}__lookup`, "Looks something up.", { type: "object", properties: {} }, "read", `mcp_${id}`)],
        call: () => Promise.reject(new Error("not called")),
    };
}

const kSources = {
    thread: (): ToolSource[] => [new WebSearch(new TestRelay(), () => "exa")],
    "root thread with 10 MCP servers": (): ToolSource[] => [new WebSearch(new TestRelay(), () => "exa"),
        new TaskTools("t1", () => Promise.reject(new Error("not called"))), ...Array.from({ length: 10 }, (_, n) => server(n + 1))],
};

function upFront(sources: ToolSource[]): ToolDefinition[] {
    const tools = new ToolSet(kNoHa1, kCatalogue, sources);
    return [...tools.list(), kPlanTool, kReadSkillTool].map((t) => ({ name: t.name, description: t.description ?? "", parametersJson: JSON.stringify(t.inputSchema) }));
}

const kCases = (Object.keys(kEnd) as Wire[]).flatMap((wire) =>
    (Object.keys(kSources) as (keyof typeof kSources)[]).map((sources): [Wire, keyof typeof kSources] => [wire, sources]));

test.each(kCases)("the up-front tool block on %s stays within 5,000 tokens for a %s", async (wire, sources) => {
    const tokens = new Tiktoken(o200k);
    const request: Request = {
        model: "m", tools: upFront(kSources[sources]()), webSearch: true,
        messages: [{ author: "user", parts: [{ type: "text", text: "Hi" }] }],
    };
    h.mock.replies.push({ chunks: [kEnd[wire]] });
    await h.stream(wire, request);
    const block = JSON.stringify((h.mock.received[0]!.json() as { tools: unknown[] }).tools);
    expect(block.length).toBeGreaterThan(0);
    expect(tokens.encode(block).length).toBeLessThanOrEqual(kBudget);
});

test("the bundled skills' catalogue stays within 600 tokens", async () => {
    const catalogue = await new Skills().instructions();
    expect(catalogue).toContain("## Skills");
    expect(new Tiktoken(o200k).encode(catalogue).length).toBeLessThanOrEqual(600);
});
