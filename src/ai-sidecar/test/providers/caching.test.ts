import { describe, expect, test } from "bun:test";

import { loadsToolReferences } from "../../src/providers/anthropic";
import type { ProviderConfig, Request, ToolDefinition, Wire } from "../../src/providers/types";
import { Harness, kKey } from "./harness";
import { data, type Reply, sse } from "./mock";

type Json = Record<string, any>;

const h = new Harness().install();

const tool = (name: string): ToolDefinition => ({ name, description: `The ${name} tool`, parametersJson: '{"type":"object"}' });

function conversation(turns: number, toolOrder: string[] = ["get_line", "add_line"]): Request {
    const messages: Request["messages"] = [];
    for (let i = 0; i < turns; ++i) {
        messages.push({ author: "user", parts: [{ type: "text", text: `Question ${i}` }] });
        if (i + 1 < turns)
            messages.push({ author: "assistant", parts: [{ type: "text", text: `Answer ${i}` }] });
    }
    return {
        model: "the-model",
        system: "You time subtitles.",
        context: "Styles: Default, Sign.",
        tools: toolOrder.map(tool),
        cacheKey: "thread-1",
        messages,
    };
}

const kDone: Record<Wire, () => Reply> = {
    "chat-completions": () => ({ chunks: [data({ choices: [{ delta: { content: "ok" }, finish_reason: "stop" }] }), "data: [DONE]\n\n"] }),
    responses: () => ({ chunks: [sse("response.completed", { type: "response.completed", response: { usage: {} } })] }),
    "anthropic-messages": () => ({
        chunks: [
            sse("message_start", { type: "message_start", message: { usage: { input_tokens: 1, output_tokens: 1 } } }),
            sse("message_delta", { type: "message_delta", delta: { stop_reason: "end_turn" }, usage: { output_tokens: 1 } }),
            sse("message_stop", { type: "message_stop" }),
        ],
    }),
    gemini: () => ({ chunks: [data({ candidates: [{ content: { role: "model", parts: [{ text: "ok" }] }, finishReason: "STOP" }] })] }),
};

const json = (body: unknown): Reply => ({ contentType: "application/json", chunks: [JSON.stringify(body)] });

/** Everything ahead of the conversation, as sent. */
function prefixOf(wire: Wire, body: Json): unknown {
    switch (wire) {
    case "chat-completions":
        return [body.messages.filter((m: Json) => m.role === "system"), body.tools];
    case "responses":
        return [body.instructions, body.input[0], body.tools];
    case "anthropic-messages":
        return [body.system, body.tools];
    case "gemini":
        return [body.systemInstruction, body.tools];
    }
}

describe("the stable prefix", () => {
    for (const wire of ["chat-completions", "responses", "anthropic-messages", "gemini"] as const) {
        test(`${wire}: system, sorted tools and context are byte-identical across turns`, async () => {
            h.mock.replies.push(kDone[wire](), kDone[wire]());
            const service = h.service(wire);
            expect((await h.turn(service, conversation(1, ["get_line", "add_line"]))).error).toBeUndefined();
            expect((await h.turn(service, conversation(2, ["add_line", "get_line"]))).error).toBeUndefined();
            const [first, second] = h.mock.received.map((r) => r.json());
            expect(JSON.stringify(prefixOf(wire, first!))).toBe(JSON.stringify(prefixOf(wire, second!)));
            expect(JSON.stringify(prefixOf(wire, first!))).toContain("Styles: Default, Sign.");
            expect(JSON.stringify(prefixOf(wire, first!)).indexOf("add_line")).toBeLessThan(JSON.stringify(prefixOf(wire, first!)).indexOf("get_line"));
        });
    }
});

describe("cache markers", () => {
    test("Anthropic marks the end of the stable prefix and the last turn", async () => {
        h.mock.replies.push(kDone["anthropic-messages"](), kDone["anthropic-messages"](), kDone["anthropic-messages"]());
        const service = h.service("anthropic-messages");
        await h.turn(service, conversation(2));
        let body = h.mock.received[0]!.json() as Json;
        expect(body.system).toEqual([
            { type: "text", text: "You time subtitles." },
            { type: "text", text: "Styles: Default, Sign.", cache_control: { type: "ephemeral" } },
        ]);
        expect(body.tools.map((t: Json) => t.cache_control)).toEqual([undefined, undefined]);
        expect(body.messages.at(-1).content.at(-1).cache_control).toEqual({ type: "ephemeral" });
        expect(JSON.stringify(body).match(/cache_control/g)).toHaveLength(2);

        await h.turn(service, { ...conversation(1), cacheLifetime: "hour" });
        body = h.mock.received[1]!.json() as Json;
        expect(body.system.at(-1).cache_control).toEqual({ type: "ephemeral", ttl: "1h" });

        await h.turn(service, { model: "m", tools: [tool("b"), tool("a")], messages: conversation(1).messages });
        body = h.mock.received[2]!.json() as Json;
        expect(body.system).toBeUndefined();
        expect(body.tools.map((t: Json) => [t.name, t.cache_control])).toEqual([["a", undefined], ["b", { type: "ephemeral" }]]);
    });

    test("OpenRouter marks only the models that need markers, and sends the thread's cache key", async () => {
        h.mock.replies.push(kDone["chat-completions"](), kDone["chat-completions"]());
        const service = h.service("chat-completions");
        await h.turn(service, { ...conversation(2), model: "anthropic/claude-x" });
        await h.turn(service, { ...conversation(2), model: "openai/gpt-x" });
        const [marked, plain] = h.mock.received.map((r) => r.json() as Json);
        expect(marked!.prompt_cache_key).toBe("thread-1");
        const systems = marked!.messages.filter((m: Json) => m.role === "system");
        expect(systems[0].content).toBe("You time subtitles.");
        expect(systems[1].content).toEqual([{ type: "text", text: "Styles: Default, Sign.", cache_control: { type: "ephemeral" } }]);
        const lastUser = marked!.messages.findLast((m: Json) => m.role === "user");
        expect(lastUser.content.at(-1).cache_control).toEqual({ type: "ephemeral" });
        expect(JSON.stringify(marked).match(/cache_control/g)).toHaveLength(2);
        expect(plain!.prompt_cache_key).toBe("thread-1");
        expect(JSON.stringify(plain)).not.toContain("cache_control");
    });

    test("OpenAI gets the thread's prompt_cache_key; custom endpoints get nothing extra", async () => {
        h.mock.replies.push(kDone.responses(), kDone["chat-completions"](), kDone.responses());
        await h.turn(h.service("responses"), conversation(1));
        await h.turn(h.service("chat-completions", h.mock.base, "custom"), { ...conversation(1), model: "anthropic/claude-x" });
        await h.turn(h.service("responses", h.mock.base, "custom"), conversation(1));
        const [openai, customChat, customResponses] = h.mock.received.map((r) => r.json() as Json);
        expect(openai!.prompt_cache_key).toBe("thread-1");
        expect(openai!.input[0]).toEqual({ role: "developer", content: [{ type: "input_text", text: "Styles: Default, Sign." }] });
        for (const body of [customChat!, customResponses!]) {
            expect(body.prompt_cache_key).toBeUndefined();
            expect(JSON.stringify(body)).not.toContain("cache_control");
        }
    });
});

test("with caching off no wire gets markers, a cache key or an explicit cache", async () => {
    const off = (request: Request): Request => ({ ...request, caching: false, explicitCache: true, cacheLifetime: "hour" });
    h.mock.replies.push(kDone["anthropic-messages"](), kDone["chat-completions"](), kDone.responses(), kDone.gemini());
    await h.turn(h.service("anthropic-messages"), off(conversation(2)));
    await h.turn(h.service("chat-completions"), off({ ...conversation(2), model: "anthropic/claude-x" }));
    await h.turn(h.service("responses"), off(conversation(1)));
    await h.turn(h.service("gemini"), off(conversation(1)));
    expect(h.mock.received).toHaveLength(4);
    for (const r of h.mock.received) {
        const body = r.json() as Json;
        expect(r.path).not.toContain("cachedContents");
        expect(JSON.stringify(body)).not.toContain("cache_control");
        expect(body.prompt_cache_key).toBeUndefined();
        expect(body.cachedContent).toBeUndefined();
    }
    expect((h.mock.received[3]!.json() as Json).systemInstruction.parts).toHaveLength(2);
});

test("cache reads and writes come back from every wire", async () => {
    h.mock.replies.push({
        chunks: [
            data({ choices: [{ delta: { content: "ok" }, finish_reason: "stop" }] }),
            data({ choices: [], usage: { prompt_tokens: 100, completion_tokens: 5, prompt_tokens_details: { cached_tokens: 60, cache_write_tokens: 30 }, cost: 0.001 } }),
            "data: [DONE]\n\n",
        ],
    });
    expect((await h.stream("chat-completions", conversation(1))).finished!.usage).toEqual(
        { inputTokens: 100, outputTokens: 5, cachedInputTokens: 60, cacheWriteTokens: 30, cacheWriteHourTokens: 0, reportedCost: 0.001 });

    h.mock.replies.push({
        chunks: [sse("response.completed", { type: "response.completed", response: { usage: { input_tokens: 90, output_tokens: 4, input_tokens_details: { cached_tokens: 64, cache_write_tokens: 20 } } } })],
    });
    expect((await h.stream("responses", conversation(1))).finished!.usage).toEqual(
        { inputTokens: 90, outputTokens: 4, cachedInputTokens: 64, cacheWriteTokens: 20, cacheWriteHourTokens: 0 });

    h.mock.replies.push({
        chunks: [
            sse("message_start", { type: "message_start", message: { usage: { input_tokens: 10, cache_read_input_tokens: 40, cache_creation_input_tokens: 50, cache_creation: { ephemeral_5m_input_tokens: 20, ephemeral_1h_input_tokens: 30 }, output_tokens: 1 } } }),
            sse("message_delta", { type: "message_delta", delta: { stop_reason: "end_turn" }, usage: { output_tokens: 6 } }),
            sse("message_stop", { type: "message_stop" }),
        ],
    });
    expect((await h.stream("anthropic-messages", conversation(1))).finished!.usage).toEqual(
        { inputTokens: 100, outputTokens: 6, cachedInputTokens: 40, cacheWriteTokens: 50, cacheWriteHourTokens: 30 });

    h.mock.replies.push({
        chunks: [data({
            candidates: [{ content: { role: "model", parts: [{ text: "ok" }] }, finishReason: "STOP" }],
            usageMetadata: { promptTokenCount: 80, cachedContentTokenCount: 70, candidatesTokenCount: 3 },
        })],
    });
    expect((await h.stream("gemini", conversation(1))).finished!.usage).toEqual(
        { inputTokens: 80, outputTokens: 3, cachedInputTokens: 70, cacheWriteTokens: 0, cacheWriteHourTokens: 0 });
});

describe("Gemini explicit caches", () => {
    const explicit = (turns: number): Request => ({ ...conversation(turns), explicitCache: true });

    test("a cache is created, used, reused, made again when it runs out, and deleted when the thread ends", async () => {
        h.mock.replies.push(json({ name: "cachedContents/c1" }), kDone.gemini(), kDone.gemini());
        const service = h.service("gemini");
        expect((await h.turn(service, explicit(1))).error).toBeUndefined();
        h.clock = 200_000;
        expect((await h.turn(service, explicit(2))).error).toBeUndefined();

        const [create, first, second] = h.mock.received;
        expect([create!.method, create!.path]).toEqual(["POST", "/v1/cachedContents"]);
        expect(create!.headers["x-goog-api-key"]).toBe(kKey);
        const made = create!.json() as Json;
        expect(made.model).toBe("models/the-model");
        expect(made.ttl).toBe("300s");
        expect(made.systemInstruction.parts.map((p: Json) => p.text)).toEqual(["You time subtitles.", "Styles: Default, Sign."]);
        expect(made.tools[0].functionDeclarations.map((d: Json) => d.name)).toEqual(["add_line", "get_line"]);
        for (const turn of [first!, second!]) {
            const body = turn.json() as Json;
            expect(body.cachedContent).toBe("cachedContents/c1");
            expect(body.systemInstruction).toBeUndefined();
            expect(body.tools).toBeUndefined();
        }

        h.clock = 280_000;
        h.mock.replies.push(json({}), json({ name: "cachedContents/c2" }), kDone.gemini());
        expect((await h.turn(service, explicit(3))).error).toBeUndefined();
        expect(h.mock.received.slice(3).map((r) => [r.method, r.path])).toEqual([
            ["DELETE", "/v1/cachedContents/c1"], ["POST", "/v1/cachedContents"], ["POST", expect.stringContaining(":streamGenerateContent")],
        ]);
        expect((h.mock.received[5]!.json() as Json).cachedContent).toBe("cachedContents/c2");

        h.mock.replies.push(json({}));
        expect(await service.endConversation({ providerId: "mock", cacheKey: "thread-1" }, new AbortController().signal)).toBeNull();
        expect([h.mock.received[6]!.method, h.mock.received[6]!.path]).toEqual(["DELETE", "/v1/cachedContents/c2"]);
        expect(await service.endConversation({ providerId: "mock", cacheKey: "thread-1" }, new AbortController().signal)).toBeNull();
        expect(h.mock.received).toHaveLength(7);
    });

    test("an hour-long cache asks for an hour, and a changed prefix makes a new cache", async () => {
        h.mock.replies.push(json({ name: "cachedContents/h1" }), kDone.gemini(), json({}), json({ name: "cachedContents/h2" }), kDone.gemini());
        const service = h.service("gemini");
        await h.turn(service, { ...explicit(1), cacheLifetime: "hour" });
        expect((h.mock.received[0]!.json() as Json).ttl).toBe("3600s");
        await h.turn(service, { ...explicit(2), cacheLifetime: "hour", context: "Styles: Default." });
        expect(h.mock.received.slice(2).map((r) => r.method)).toEqual(["DELETE", "POST", "POST"]);
        expect((h.mock.received[4]!.json() as Json).cachedContent).toBe("cachedContents/h2");
    });

    test("a refused create sends the whole request and isn't tried again for that prefix", async () => {
        h.mock.replies.push(
            { status: 400, contentType: "application/json", chunks: ['{"error":{"code":400,"message":"Cached content is too small."}}'] },
            kDone.gemini(), kDone.gemini());
        const service = h.service("gemini");
        expect((await h.turn(service, explicit(1))).text()).toBe("ok");
        expect((await h.turn(service, explicit(2))).text()).toBe("ok");
        expect(h.mock.received.map((r) => r.path.includes("cachedContents"))).toEqual([true, false, false]);
        const whole = h.mock.received[1]!.json() as Json;
        expect(whole.cachedContent).toBeUndefined();
        expect(whole.systemInstruction.parts).toHaveLength(2);
    });

    test("a create that fails for another reason is tried again next turn", async () => {
        h.mock.replies.push(
            { status: 503, contentType: "application/json", chunks: ['{"error":{"code":503,"message":"busy"}}'] },
            kDone.gemini(), json({ name: "cachedContents/r1" }), kDone.gemini());
        const service = h.service("gemini");
        expect((await h.turn(service, explicit(1))).text()).toBe("ok");
        expect((await h.turn(service, explicit(2))).text()).toBe("ok");
        expect((h.mock.received[3]!.json() as Json).cachedContent).toBe("cachedContents/r1");
    });

    test("a request that fails on its cache drops it, and the next turn makes a new one", async () => {
        h.mock.replies.push(
            json({ name: "cachedContents/gone" }),
            { status: 403, contentType: "application/json", chunks: ['{"error":{"code":403,"message":"CachedContent not found"}}'] },
            json({ name: "cachedContents/new" }), kDone.gemini());
        const service = h.service("gemini");
        expect((await h.turn(service, explicit(1))).error).toMatchObject({ kind: "unauthorized", httpStatus: 403 });
        expect((await h.turn(service, explicit(2))).error).toBeUndefined();
        expect(h.mock.received.map((r) => r.path.includes("cachedContents"))).toEqual([true, false, true, false]);
        expect((h.mock.received[3]!.json() as Json).cachedContent).toBe("cachedContents/new");
    });

    test("other providers and requests without a cache key never make one", async () => {
        h.mock.replies.push(kDone["anthropic-messages"](), kDone.gemini());
        await h.stream("anthropic-messages", explicit(1));
        await h.stream("gemini", { ...explicit(1), cacheKey: undefined });
        expect(h.mock.received.some((r) => r.path.includes("cachedContents"))).toBe(false);
    });
});

function found(names: string[]): Request["messages"] {
    return [
        { author: "user", parts: [{ type: "text", text: "Play it" }] },
        { author: "assistant", parts: [{ type: "toolCall", id: "s1", name: "search_tools", argumentsJson: '{"query":"audio"}' }] },
        { author: "user", parts: [{ type: "toolResult", callId: "s1", content: JSON.stringify({ tools: names.map((name) => ({ name })) }) }] },
    ];
}

function toolNames(wire: Wire, body: Json): string[] {
    switch (wire) {
    case "chat-completions":
        return body.tools.map((t: Json) => t.function.name);
    case "gemini":
        return body.tools[0].functionDeclarations.map((t: Json) => t.name);
    default:
        return body.tools.map((t: Json) => t.name);
    }
}

describe("tools search_tools found", () => {
    for (const wire of ["chat-completions", "responses", "anthropic-messages", "gemini"] as const) {
        test(`${wire}: follow the sorted tools, in the order found, so the prefix stays as it was`, async () => {
            h.mock.replies.push(kDone[wire](), kDone[wire]());
            const service = h.service(wire);
            await h.turn(service, conversation(1));
            await h.turn(service, {
                ...conversation(1),
                tools: ["get_line", "video_frame", "add_line", "audio_play"].map(tool),
                messages: found(["video_frame", "audio_play"]),
            });
            const [before, after] = h.mock.received.map((r) => r.json() as Json);
            expect(toolNames(wire, before!)).toEqual(["add_line", "get_line"]);
            expect(toolNames(wire, after!)).toEqual(["add_line", "get_line", "video_frame", "audio_play"]);
            const core = JSON.stringify(before!.tools).replace(/(\]\})?\]$/, "");
            expect(JSON.stringify(after!.tools).startsWith(core)).toBe(true);
        });
    }

    test("Anthropic loads them as tool_reference blocks on models with tool search, and defers them", async () => {
        h.mock.replies.push(kDone["anthropic-messages"](), kDone["anthropic-messages"]());
        const service = h.service("anthropic-messages");
        const asking = (model: string): Request => ({
            ...conversation(1), model, tools: ["get_line", "search_tools", "audio_play", "gone"].map(tool),
            messages: found(["audio_play", "unsent"]),
        });
        await h.turn(service, asking("claude-opus-5-5"));
        await h.turn(service, asking("claude-opus-4-1-20250805"));
        const [loaded, older] = h.mock.received.map((r) => r.json() as Json);
        expect(loaded!.tools.map((t: Json) => [t.name, t.defer_loading, t.cache_control])).toEqual([
            ["get_line", undefined, undefined], ["gone", undefined, undefined], ["search_tools", undefined, undefined],
            ["audio_play", true, undefined],
        ]);
        expect(loaded!.messages[2].content[0].content).toEqual([{ type: "tool_reference", tool_name: "audio_play" }]);
        expect(older!.tools.map((t: Json) => [t.name, t.defer_loading])).toEqual([
            ["get_line", undefined], ["gone", undefined], ["search_tools", undefined], ["audio_play", undefined],
        ]);
        expect(older!.messages[2].content[0].content).toBe(JSON.stringify({ tools: [{ name: "audio_play" }, { name: "unsent" }] }));
    });

    test("only Anthropic's own API on Claude 4.5 and later takes tool_reference blocks", () => {
        const anthropic = { id: "a", preset: "anthropic", wire: "anthropic-messages", baseUrl: "" } as const;
        const loads = (model: string, config: ProviderConfig = anthropic) => loadsToolReferences(config, model);
        for (const model of ["claude-opus-5-5", "claude-sonnet-5", "claude-fable-5-1", "claude-opus-4-8", "claude-haiku-4-5-20251001",
            "claude-sonnet-4-5-20250929"])
            expect(loads(model)).toBe(true);
        for (const model of ["claude-opus-4-1-20250805", "claude-sonnet-4-20250514", "claude-opus-4-0", "claude-3-7-sonnet-latest", "m"])
            expect(loads(model)).toBe(false);
        expect(loads("claude-opus-5-5", { ...anthropic, preset: "custom" })).toBe(false);
    });
});
