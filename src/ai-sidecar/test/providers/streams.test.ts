import { describe, expect, test } from "bun:test";

import type { ToolCall } from "../../src/providers/types";
import { fullRequest, Harness, kKey } from "./harness";
import { data, ragged, sse } from "./mock";

const h = new Harness().install();

const call = (id: string, name: string, argumentsJson: string, signature?: string): ToolCall =>
    ({ type: "toolCall", id, name, argumentsJson, ...(signature ? { signature } : {}) });

describe("Chat Completions", () => {
    test("streams text and usage", async () => {
        h.mock.replies.push({
            chunks: ragged([
                ": OPENROUTER PROCESSING\n\n",
                data({ choices: [{ delta: { role: "assistant", content: "Hel" } }] }),
                data({ choices: [{ delta: { content: "lo" }, finish_reason: "stop" }] }),
                data({ choices: [], usage: { prompt_tokens: 12, completion_tokens: 3, prompt_tokens_details: { cached_tokens: 4 }, cost: 0.00042 } }),
                "data: [DONE]\n\n",
            ]),
        });
        const run = await h.stream("chat-completions");
        expect(run.error).toBeUndefined();
        expect(run.text()).toBe("Hello");
        expect(run.finished!.reason).toBe("end");
        expect(run.finished!.text).toBe("Hello");
        expect(run.finished!.usage).toEqual({ inputTokens: 12, outputTokens: 3, cachedInputTokens: 4, reportedCost: 0.00042 });
        expect(run.updates.at(-1)).toEqual({ kind: "usage", usage: run.finished!.usage });
        expect(h.mock.received).toHaveLength(1);
        const r = h.mock.received[0]!;
        expect(r.path).toBe("/v1/chat/completions");
        expect(r.headers.authorization).toBe(`Bearer ${kKey}`);
        expect(r.headers["x-title"]).toBe("HikariSub");
        expect(r.headers["http-referer"]).toBe("https://hikarisub.org");
        expect(r.json().stream).toBe(true);
    });

    test("streams tool calls", async () => {
        h.mock.replies.push({
            chunks: [
                data({ choices: [{ delta: { tool_calls: [{ index: 0, id: "call_a", type: "function", function: { name: "get_line", arguments: "" } }] } }] }),
                data({ choices: [{ delta: { tool_calls: [{ index: 0, function: { arguments: '{"n":' } }] } }] }),
                data({ choices: [{ delta: { tool_calls: [{ index: 0, function: { arguments: "3}" } }, { index: 1, id: "call_b", function: { name: "seek", arguments: "{}" } }] } }] }),
                data({ choices: [{ delta: {}, finish_reason: "tool_calls" }] }),
                "data: [DONE]\n\n",
            ],
        });
        const run = await h.stream("chat-completions");
        expect(run.finished!.reason).toBe("toolUse");
        expect(run.finished!.toolCalls).toEqual([call("call_a", "get_line", '{"n":3}'), call("call_b", "seek", "{}")]);
        expect(run.updates[0]).toEqual({ kind: "toolCall", index: 0, id: "call_a", name: "get_line" });
    });

    test("encodes vision, tools and history", async () => {
        h.mock.replies.push({ chunks: [data({ choices: [{ delta: {}, finish_reason: "stop" }] }), "data: [DONE]\n\n"] });
        await h.stream("chat-completions", fullRequest());
        const body = h.mock.received[0]!.json() as any;
        expect(body.model).toBe("the-model");
        expect(body.max_tokens).toBe(100);
        expect(body.messages).toHaveLength(4);
        expect(body.messages[0].role).toBe("system");
        expect(body.messages[1].content[1].image_url.url).toBe("data:image/png;base64,iVBORw0KGgo=");
        expect(body.messages[2].content).toBe("Let me look.");
        expect(body.messages[2].tool_calls[0].function.arguments).toBe('{"n":3}');
        expect(body.messages[3].role).toBe("tool");
        expect(body.messages[3].tool_call_id).toBe("call_1");
        expect(body.tools[0].function.name).toBe("get_line");
        expect(body.tools[0].function.parameters.type).toBe("object");
    });
});

describe("Responses", () => {
    test("streams text, tool calls and usage", async () => {
        h.mock.replies.push({
            chunks: ragged([
                sse("response.created", { type: "response.created", response: { id: "r" } }),
                sse("response.output_text.delta", { type: "response.output_text.delta", output_index: 0, delta: "Look" }),
                sse("response.output_item.added", { type: "response.output_item.added", output_index: 1, item: { type: "function_call", id: "fc_1", call_id: "call_x", name: "get_line", arguments: "" } }),
                sse("response.function_call_arguments.delta", { type: "response.function_call_arguments.delta", output_index: 1, item_id: "fc_1", delta: '{"n":1}' }),
                sse("response.completed", { type: "response.completed", response: { status: "completed", usage: { input_tokens: 50, output_tokens: 7, input_tokens_details: { cached_tokens: 10 } } } }),
            ]),
        });
        const run = await h.stream("responses");
        expect(run.text()).toBe("Look");
        expect(run.finished!.reason).toBe("toolUse");
        expect(run.finished!.toolCalls).toEqual([call("call_x", "get_line", '{"n":1}')]);
        expect(run.finished!.usage).toEqual({ inputTokens: 50, outputTokens: 7, cachedInputTokens: 10 });
        expect(h.mock.received[0]!.path).toBe("/v1/responses");
    });

    test("encodes vision, tools and history", async () => {
        h.mock.replies.push({ chunks: [sse("response.completed", { type: "response.completed", response: { usage: {} } })] });
        await h.stream("responses", fullRequest());
        const body = h.mock.received[0]!.json() as any;
        expect(body.store).toBe(false);
        expect(body.instructions).toBe("Be brief.");
        expect(body.max_output_tokens).toBe(100);
        expect(body.input).toHaveLength(4);
        expect(body.input[0].content[1].type).toBe("input_image");
        expect(body.input[1].content[0].type).toBe("output_text");
        expect(body.input[2].type).toBe("function_call");
        expect(body.input[2].call_id).toBe("call_1");
        expect(body.input[3].type).toBe("function_call_output");
        expect(body.tools[0].name).toBe("get_line");
    });
});

describe("Anthropic Messages", () => {
    test("streams text, tool calls and usage", async () => {
        h.mock.replies.push({
            chunks: ragged([
                sse("message_start", { type: "message_start", message: { usage: { input_tokens: 20, cache_read_input_tokens: 30, cache_creation_input_tokens: 0, output_tokens: 1 } } }),
                sse("content_block_start", { type: "content_block_start", index: 0, content_block: { type: "text", text: "" } }),
                sse("ping", { type: "ping" }),
                sse("content_block_delta", { type: "content_block_delta", index: 0, delta: { type: "text_delta", text: "On it" } }),
                sse("content_block_stop", { type: "content_block_stop", index: 0 }),
                sse("content_block_start", { type: "content_block_start", index: 1, content_block: { type: "tool_use", id: "toolu_1", name: "get_line", input: {} } }),
                sse("content_block_delta", { type: "content_block_delta", index: 1, delta: { type: "input_json_delta", partial_json: '{"n":' } }),
                sse("content_block_delta", { type: "content_block_delta", index: 1, delta: { type: "input_json_delta", partial_json: "2}" } }),
                sse("message_delta", { type: "message_delta", delta: { stop_reason: "tool_use" }, usage: { output_tokens: 15 } }),
                sse("message_stop", { type: "message_stop" }),
            ]),
        });
        const run = await h.stream("anthropic-messages");
        expect(run.error).toBeUndefined();
        expect(run.text()).toBe("On it");
        expect(run.finished!.reason).toBe("toolUse");
        expect(run.finished!.toolCalls).toEqual([call("toolu_1", "get_line", '{"n":2}')]);
        expect(run.finished!.usage).toEqual({ inputTokens: 50, outputTokens: 15, cachedInputTokens: 30 });
        const r = h.mock.received[0]!;
        expect(r.path).toBe("/v1/messages");
        expect(r.headers["x-api-key"]).toBe(kKey);
        expect(r.headers["anthropic-version"]).toMatch(/^\d{4}-\d{2}-\d{2}$/);
        expect(r.headers.authorization).toBeUndefined();
    });

    test("encodes vision, tools and history", async () => {
        h.mock.replies.push({ chunks: [sse("message_stop", { type: "message_stop" })] });
        await h.stream("anthropic-messages", fullRequest());
        const body = h.mock.received[0]!.json() as any;
        expect(body.system).toBe("Be brief.");
        expect(body.max_tokens).toBe(100);
        expect(body.messages).toHaveLength(3);
        expect(body.messages[0].content[1].source.media_type).toBe("image/png");
        expect(body.messages[1].content[1].type).toBe("tool_use");
        expect(body.messages[1].content[1].input.n).toBe(3);
        expect(body.messages[2].content[0].type).toBe("tool_result");
        expect(body.messages[2].content[0].tool_use_id).toBe("call_1");
        expect(body.tools[0].input_schema.type).toBe("object");
    });
});

describe("Gemini", () => {
    test("streams text, tool calls and usage", async () => {
        h.mock.replies.push({
            chunks: ragged([
                data({ candidates: [{ content: { role: "model", parts: [{ text: "thinking", thought: true }, { text: "On" }] } }], usageMetadata: { promptTokenCount: 40 } }),
                data({ candidates: [{ content: { role: "model", parts: [{ text: " it" }] } }] }),
                data({
                    candidates: [{
                        content: { role: "model", parts: [{ functionCall: { id: "fc-1", name: "get_line", args: { n: 2 } }, thoughtSignature: "c2lnLUE=" }, { functionCall: { name: "seek", args: {} } }] },
                        finishReason: "STOP",
                    }],
                    usageMetadata: { promptTokenCount: 40, cachedContentTokenCount: 32, candidatesTokenCount: 12, thoughtsTokenCount: 5, toolUsePromptTokenCount: 3, totalTokenCount: 60 },
                    modelVersion: "the-model",
                    responseId: "r1",
                }),
            ]),
        });
        const run = await h.stream("gemini", { model: "the-model", messages: [{ author: "user", parts: [{ type: "text", text: "Hi" }] }] });
        expect(run.error).toBeUndefined();
        expect(run.text()).toBe("On it");
        expect(run.finished!.reason).toBe("toolUse");
        expect(run.finished!.toolCalls).toEqual([call("fc-1", "get_line", '{"n":2}', "c2lnLUE="), call("call_1", "seek", "{}")]);
        expect(run.finished!.usage).toEqual({ inputTokens: 43, outputTokens: 17, cachedInputTokens: 32 });
        const r = h.mock.received[0]!;
        expect(r.method).toBe("POST");
        expect(r.path).toBe("/v1/models/the-model:streamGenerateContent?alt=sse");
        expect(r.headers["x-goog-api-key"]).toBe(kKey);
        expect(r.headers.authorization).toBeUndefined();
        expect(r.path).not.toContain(kKey);
    });

    test("encodes vision, tools and history", async () => {
        h.mock.replies.push({ chunks: [data({ candidates: [{ content: { parts: [{ text: "ok" }] }, finishReason: "STOP" }] })] });
        await h.stream("gemini", fullRequest());
        const body = h.mock.received[0]!.json() as any;
        expect(body.model).toBeUndefined();
        expect(body.systemInstruction.parts[0].text).toBe("Be brief.");
        expect(body.generationConfig.maxOutputTokens).toBe(100);
        expect(body.contents).toHaveLength(3);
        expect(body.contents[0].role).toBe("user");
        expect(body.contents[0].parts[0].text).toBe("What is on screen?");
        expect(body.contents[0].parts[1].inlineData).toEqual({ mimeType: "image/png", data: "iVBORw0KGgo=" });
        expect(body.contents[1].role).toBe("model");
        expect(body.contents[1].parts[0].text).toBe("Let me look.");
        expect(body.contents[1].parts[1]).toEqual({ functionCall: { id: "call_1", name: "get_line", args: { n: 3 } }, thoughtSignature: "c2lnLTE=" });
        expect(body.contents[2].parts[0]).toEqual({ functionResponse: { id: "call_1", name: "get_line", response: { result: "Dialogue: 0,..." } } });
        const declaration = body.tools[0].functionDeclarations[0];
        expect(declaration.name).toBe("get_line");
        expect(declaration.parametersJsonSchema.type).toBe("object");

        const failed = fullRequest();
        (failed.messages[2]!.parts[0] as { isError?: boolean }).isError = true;
        h.mock.replies.push({ chunks: [data({ candidates: [{ content: { parts: [{ text: "ok" }] }, finishReason: "STOP" }] })] });
        await h.stream("gemini", failed);
        const again = h.mock.received[1]!.json() as any;
        expect(again.contents[2].parts[0].functionResponse.response).toEqual({ error: "Dialogue: 0,..." });
    });

    test("safety blocks and bad calls end the request", async () => {
        h.mock.replies.push({ chunks: [data({ promptFeedback: { blockReason: "SAFETY", safetyRatings: [{ category: "HARM_CATEGORY_HARASSMENT", probability: "HIGH", blocked: true }] }, usageMetadata: { promptTokenCount: 9 } })] });
        let run = await h.stream("gemini");
        expect(run.finished!.reason).toBe("refused");
        expect(run.finished!.usage.inputTokens).toBe(9);

        h.mock.replies.push({
            chunks: [data({ candidates: [{ content: { parts: [{ text: "Once" }] } }] }),
                data({ candidates: [{ finishReason: "SAFETY", safetyRatings: [{ category: "HARM_CATEGORY_DANGEROUS_CONTENT", probability: "MEDIUM", blocked: true }] }] })],
        });
        run = await h.stream("gemini");
        expect(run.finished!.reason).toBe("refused");
        expect(run.finished!.text).toBe("Once");

        h.mock.replies.push({ chunks: [data({ candidates: [{ content: { parts: [{ text: "x" }] }, finishReason: "MAX_TOKENS" }] })] });
        expect((await h.stream("gemini")).finished!.reason).toBe("maxTokens");

        h.mock.replies.push({ chunks: [data({ candidates: [{ finishReason: "MALFORMED_FUNCTION_CALL", finishMessage: "bad call" }] })] });
        run = await h.stream("gemini");
        expect(run.error).toEqual({ kind: "malformed", httpStatus: 0, detail: "bad call" });

        h.mock.replies.push({ chunks: [data({ candidates: [{ finishReason: "MISSING_THOUGHT_SIGNATURE" }] })] });
        expect((await h.stream("gemini")).error!.kind).toBe("rejected");
    });
});
