import { describe, expect, test } from "bun:test";

import type { Message, Request, Wire } from "../../src/providers/types";
import { Harness } from "./harness";
import { data, sse } from "./mock";

const h = new Harness().install();

const kTool = { name: "get_line", description: "Reads a Line", parametersJson: '{"type":"object"}' };

function asking(webSearch: boolean | undefined, extra: Message[] = []): Request {
    return {
        model: "m",
        tools: [kTool],
        messages: [{ author: "user", parts: [{ type: "text", text: "Who sings this?" }] }, ...extra],
        ...(webSearch === undefined ? {} : { webSearch }),
    };
}

const kEnd: Record<Wire, () => string[]> = {
    "chat-completions": () => [data({ choices: [{ delta: {}, finish_reason: "stop" }] }), "data: [DONE]\n\n"],
    responses: () => [sse("response.completed", { type: "response.completed", response: { usage: {} } })],
    "anthropic-messages": () => [sse("message_stop", { type: "message_stop" })],
    gemini: () => [data({ candidates: [{ content: { parts: [{ text: "ok" }] }, finishReason: "STOP" }] })],
};

const kWires = Object.keys(kEnd) as Wire[];

function toolTypes(wire: Wire, body: any): string[] {
    if (wire === "gemini")
        return (body.tools ?? []).flatMap((t: object) => Object.keys(t));
    return (body.tools ?? []).map((t: { type: string }) => t.type);
}

describe("native web search", () => {
    test.each(kWires)("%s sends its search tools only while web search is on", async (wire) => {
        for (const webSearch of [undefined, false, true]) {
            h.mock.replies.push({ chunks: kEnd[wire]() });
            await h.stream(wire, asking(webSearch));
        }
        const [absent, off, on] = h.mock.received.map((r) => toolTypes(wire, r.json()));
        expect(absent).toEqual(off!);
        expect(on).not.toEqual(off!);
        expect(on!.length).toBeGreaterThan(off!.length);
    });

    test.each(kWires)("%s never searches natively for a custom provider", async (wire) => {
        h.mock.replies.push({ chunks: kEnd[wire]() });
        await h.turn(h.service(wire, h.mock.base, "custom"), asking(true));
        h.mock.replies.push({ chunks: kEnd[wire]() });
        await h.turn(h.service(wire, h.mock.base, "custom"), asking(false));
        const [on, off] = h.mock.received.map((r) => r.json());
        expect(on).toEqual(off!);
    });

    test("a preset on another wire searches through the search service", async () => {
        h.mock.replies.push({ chunks: kEnd["chat-completions"]() });
        await h.turn(h.service("chat-completions", h.mock.base, "openai"), asking(true));
        expect(toolTypes("chat-completions", h.mock.received[0]!.json())).toEqual(["function"]);
    });

    test("Anthropic asks for web_search and web_fetch with a cap on each", async () => {
        h.mock.replies.push({ chunks: kEnd["anthropic-messages"]() });
        await h.stream("anthropic-messages", asking(true));
        const tools = (h.mock.received[0]!.json() as any).tools;
        expect(tools.slice(1)).toEqual([
            { type: "web_search_20250305", name: "web_search", max_uses: 5 },
            { type: "web_fetch_20250910", name: "web_fetch", max_uses: 5 },
        ]);
    });

    test("OpenRouter asks for its server tools and OpenAI for web_search", async () => {
        h.mock.replies.push({ chunks: kEnd["chat-completions"]() });
        await h.stream("chat-completions", asking(true));
        h.mock.replies.push({ chunks: kEnd.responses() });
        await h.stream("responses", asking(true));
        const [chat, responses] = h.mock.received.map((r) => r.json() as any);
        expect(chat.tools.slice(1)).toEqual([{ type: "openrouter:web_search" }, { type: "openrouter:web_fetch" }]);
        expect(responses.tools.slice(1)).toEqual([{ type: "web_search" }]);
    });

    test("Gemini puts Google Search beside the functions and asks for validated calls", async () => {
        h.mock.replies.push({ chunks: kEnd.gemini() });
        await h.stream("gemini", asking(true));
        const body = h.mock.received[0]!.json() as any;
        expect(body.tools).toHaveLength(1);
        expect(body.tools[0].functionDeclarations[0].name).toBe("get_line");
        expect(body.tools[0].googleSearch).toEqual({});
        expect(body.tools[0].urlContext).toEqual({});
        expect(body.toolConfig).toEqual({ includeServerSideToolInvocations: true, functionCallingConfig: { mode: "VALIDATED" } });

        h.mock.replies.push({ chunks: kEnd.gemini() });
        await h.stream("gemini", { ...asking(true), tools: [] });
        const alone = h.mock.received[1]!.json() as any;
        expect(alone.tools).toEqual([{ googleSearch: {}, urlContext: {} }]);
        expect(alone.toolConfig).toBeUndefined();
    });
});

describe("citations", () => {
    test("Anthropic numbers the search results it cites and keeps the turn to replay", async () => {
        const location = (url: string, title: string, cited_text: string) =>
            ({ type: "web_search_result_location", url, title, cited_text, encrypted_index: "e" });
        h.mock.replies.push({
            chunks: [
                sse("message_start", { type: "message_start", message: { usage: { input_tokens: 5, output_tokens: 1 } } }),
                sse("content_block_start", { type: "content_block_start", index: 0, content_block: { type: "server_tool_use", id: "srvtoolu_1", name: "web_search", input: {} } }),
                sse("content_block_delta", { type: "content_block_delta", index: 0, delta: { type: "input_json_delta", partial_json: '{"query":"aimer"}' } }),
                sse("content_block_stop", { type: "content_block_stop", index: 0 }),
                sse("content_block_start", { type: "content_block_start", index: 1, content_block: { type: "web_search_tool_result", tool_use_id: "srvtoolu_1", content: [{ type: "web_search_result", url: "https://example.org/a", title: "A", encrypted_content: "x" }] } }),
                sse("content_block_stop", { type: "content_block_stop", index: 1 }),
                sse("content_block_start", { type: "content_block_start", index: 2, content_block: { type: "text", text: "", citations: null } }),
                sse("content_block_delta", { type: "content_block_delta", index: 2, delta: { type: "citations_delta", citation: location("https://example.org/a", "A", "Aimer sings it") } }),
                sse("content_block_delta", { type: "content_block_delta", index: 2, delta: { type: "text_delta", text: "Aimer." } }),
                sse("content_block_delta", { type: "content_block_delta", index: 2, delta: { type: "citations_delta", citation: location("https://example.org/a", "A", "again") } }),
                sse("content_block_delta", { type: "content_block_delta", index: 2, delta: { type: "citations_delta", citation: location("https://example.org/b", "", "B text") } }),
                sse("content_block_stop", { type: "content_block_stop", index: 2 }),
                sse("message_delta", { type: "message_delta", delta: { stop_reason: "end_turn" }, usage: { output_tokens: 9 } }),
                sse("message_stop", { type: "message_stop" }),
            ],
        });
        const run = await h.stream("anthropic-messages", asking(true));
        expect(run.error).toBeUndefined();
        expect(run.finished!.text).toBe("Aimer.");
        expect(run.finished!.citations).toEqual([
            { index: 1, url: "https://example.org/a", title: "A", quote: "Aimer sings it", start: 0, end: 6 },
            { index: 2, url: "https://example.org/b", title: "https://example.org/b", quote: "B text", start: 0, end: 6 },
        ]);
        expect(run.updates.filter((u) => u.kind === "citation")).toHaveLength(2);
        expect(run.finished!.searches).toEqual([{ kind: "search", query: "aimer", urls: ["https://example.org/a"] }]);

        const state = run.finished!.providerState!;
        expect(state.wire).toBe("anthropic-messages");
        const blocks = JSON.parse(state.json);
        expect(blocks.map((b: { type: string }) => b.type)).toEqual(["server_tool_use", "web_search_tool_result", "text"]);
        expect(blocks[0].input).toEqual({ query: "aimer" });
        expect(blocks[2].text).toBe("Aimer.");
        expect(blocks[2].citations).toHaveLength(3);

        h.mock.replies.push({ chunks: kEnd["anthropic-messages"]() });
        await h.stream("anthropic-messages", asking(true, [
            { author: "assistant", parts: [{ type: "text", text: "Aimer." }, state] },
            { author: "user", parts: [{ type: "text", text: "Thanks" }] },
        ]));
        const replay = (h.mock.received[1]!.json() as any).messages[1];
        expect(replay).toEqual({ role: "assistant", content: blocks });
    });

    test("a turn without a server search keeps no provider state", async () => {
        h.mock.replies.push({ chunks: kEnd["anthropic-messages"]() });
        const run = await h.stream("anthropic-messages", asking(true));
        expect(run.finished!.providerState).toBeUndefined();
        expect(run.finished!.citations).toBeUndefined();
        expect(run.finished!.searches).toBeUndefined();
    });

    test("OpenRouter cites url_citation annotations on the delta and on the message", async () => {
        const citation = (url: string, title: string, content?: string) =>
            ({ type: "url_citation", url_citation: { url, title, start_index: 0, end_index: 3, ...(content ? { content } : {}) } });
        h.mock.replies.push({
            chunks: [
                data({ choices: [{ delta: { content: "Aim", annotations: [citation("https://example.org/a", "A", "Aimer")] } }] }),
                data({ choices: [{ delta: { content: "er" }, message: { annotations: [citation("https://example.org/b", "B")] }, finish_reason: "stop" }] }),
                "data: [DONE]\n\n",
            ],
        });
        const run = await h.stream("chat-completions", asking(true));
        expect(run.finished!.citations).toEqual([
            { index: 1, url: "https://example.org/a", title: "A", quote: "Aimer", start: 0, end: 3 },
            { index: 2, url: "https://example.org/b", title: "B", start: 0, end: 3 },
        ]);
    });

    test("OpenAI cites the annotations Responses adds to its text", async () => {
        h.mock.replies.push({
            chunks: [
                sse("response.output_text.delta", { type: "response.output_text.delta", output_index: 1, delta: "Aimer." }),
                sse("response.output_text.annotation.added", { type: "response.output_text.annotation.added", output_index: 1, annotation_index: 0, annotation: { type: "url_citation", url: "https://example.org/a", title: "A", start_index: 0, end_index: 6 } }),
                sse("response.output_text.annotation.added", { type: "response.output_text.annotation.added", output_index: 1, annotation_index: 1, annotation: { type: "file_citation", file_id: "f" } }),
                sse("response.completed", { type: "response.completed", response: { status: "completed", usage: {} } }),
            ],
        });
        const run = await h.stream("responses", asking(true));
        expect(run.finished!.citations).toEqual([{ index: 1, url: "https://example.org/a", title: "A", start: 0, end: 6 }]);
    });

    test("OpenAI's web_search_call items become the turn's searches and page reads", async () => {
        const call = (action: object) => sse("response.output_item.done", { type: "response.output_item.done", output_index: 0, item: { type: "web_search_call", status: "completed", action } });
        h.mock.replies.push({
            chunks: [
                call({ type: "search", query: "aimer", sources: [{ type: "url", url: "https://example.org/a" }] }),
                call({ type: "open_page", url: "https://example.org/b" }),
                call({ type: "find_in_page", url: "https://example.org/b", pattern: "x" }),
                sse("response.completed", { type: "response.completed", response: { status: "completed", usage: {} } }),
            ],
        });
        const run = await h.stream("responses", asking(true));
        expect(run.finished!.searches).toEqual([
            { kind: "search", query: "aimer", urls: ["https://example.org/a"] },
            { kind: "read", url: "https://example.org/b" },
        ]);
        expect((h.mock.received[0]!.json() as any).include).toEqual(["web_search_call.action.sources"]);
    });

    test("Gemini cites the grounded sources, passes Google's suggestions on and keeps server tool parts", async () => {
        const toolCall = { toolCall: { id: "s1", toolType: "GOOGLE_SEARCH_WEB", args: { queries: ["aimer"] } } };
        const toolResponse = { toolResponse: { id: "s1", toolType: "GOOGLE_SEARCH_WEB", response: {} } };
        h.mock.replies.push({
            chunks: [
                data({ candidates: [{ content: { role: "model", parts: [toolCall, toolResponse] } }] }),
                data({
                    candidates: [{
                        content: { role: "model", parts: [{ text: "Aimer sings it." }] },
                        finishReason: "STOP",
                        groundingMetadata: {
                            webSearchQueries: ["aimer"],
                            searchEntryPoint: { renderedContent: "<div>chips</div>" },
                            groundingChunks: [{ web: { uri: "https://example.org/a", title: "a.org" } }, { web: { uri: "https://example.org/b", title: "b.org" } }],
                            groundingSupports: [{ segment: { startIndex: 0, endIndex: 15, text: "Aimer sings it." }, groundingChunkIndices: [1] }],
                        },
                    }],
                }),
            ],
        });
        const run = await h.stream("gemini", asking(true));
        expect(run.error).toBeUndefined();
        expect(run.finished!.citations).toEqual([
            { index: 1, url: "https://example.org/b", title: "b.org", quote: "Aimer sings it.", start: 0, end: 15 },
            { index: 2, url: "https://example.org/a", title: "a.org" },
        ]);
        expect(run.finished!.searchSuggestions).toBe("<div>chips</div>");
        expect(run.finished!.searches).toEqual([{ kind: "search", query: "aimer", urls: ["https://example.org/a", "https://example.org/b"] }]);
        const state = run.finished!.providerState!;
        expect(state.wire).toBe("gemini");
        const parts = JSON.parse(state.json);
        expect(parts).toHaveLength(3);
        expect(parts[0].toolCall.id).toBe("s1");

        h.mock.replies.push({ chunks: kEnd.gemini() });
        await h.stream("gemini", asking(true, [
            { author: "assistant", parts: [{ type: "text", text: "Aimer sings it." }, state] },
            { author: "user", parts: [{ type: "text", text: "Thanks" }] },
        ]));
        const replay = (h.mock.received[1]!.json() as any).contents[1];
        expect(replay.role).toBe("model");
        expect(replay.parts[0].toolCall.id).toBe("s1");
    });

    test("provider state from another wire is ignored", async () => {
        h.mock.replies.push({ chunks: kEnd["anthropic-messages"]() });
        await h.stream("anthropic-messages", asking(true, [
            { author: "assistant", parts: [{ type: "text", text: "Hi" }, { type: "providerState", wire: "gemini", json: "[{\"text\":\"x\"}]" }] },
            { author: "user", parts: [{ type: "text", text: "Thanks" }] },
        ]));
        const replay = (h.mock.received[0]!.json() as any).messages[1];
        expect(replay.content).toEqual([{ type: "text", text: "Hi" }]);
    });
});
