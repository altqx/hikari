import { describe, expect, test } from "bun:test";

import type { Finished } from "../../src/providers/types";
import { Harness } from "./harness";
import { data, sse } from "./mock";

const h = new Harness().install();

const kAnswer = "ร้องโดย Aimer 🎤 กับ LiSA.";

function cited(finished: Finished): Record<string, string | undefined> {
    expect(finished.text).toBe(kAnswer);
    return Object.fromEntries((finished.citations ?? []).map((c) => [c.title,
        c.start === undefined ? undefined : finished.text.slice(c.start, c.end)]));
}

describe("a citation lands on the characters it cites, in Thai and emoji text", () => {
    test("Anthropic: on the end of the cited text block", async () => {
        const location = (url: string, title: string) =>
            ({ type: "web_search_result_location", url, title, cited_text: title, encrypted_index: "e" });
        const block = (index: number, text: string, citation?: object) => [
            sse("content_block_start", { type: "content_block_start", index, content_block: { type: "text", text: "", citations: null } }),
            ...(citation ? [sse("content_block_delta", { type: "content_block_delta", index, delta: { type: "citations_delta", citation } })] : []),
            sse("content_block_delta", { type: "content_block_delta", index, delta: { type: "text_delta", text } }),
            sse("content_block_stop", { type: "content_block_stop", index }),
        ];
        h.mock.replies.push({
            chunks: [
                sse("message_start", { type: "message_start", message: { usage: { input_tokens: 1, output_tokens: 1 } } }),
                ...block(0, "ร้องโดย "),
                ...block(1, "Aimer 🎤", location("https://example.org/a", "a")),
                ...block(2, " กับ "),
                ...block(3, "LiSA", location("https://example.org/b", "b")),
                ...block(4, "."),
                sse("message_delta", { type: "message_delta", delta: { stop_reason: "end_turn" }, usage: { output_tokens: 9 } }),
                sse("message_stop", { type: "message_stop" }),
            ],
        });
        const run = await h.stream("anthropic-messages");
        expect(cited(run.finished!)).toEqual({ a: "Aimer 🎤", b: "LiSA" });
        expect(run.finished!.citations!.map((c) => [c.start, c.end])).toEqual([[8, 16], [21, 25]]);
    });

    test("OpenRouter: from character indices into the whole message", async () => {
        const citation = (url: string, title: string, start_index: number, end_index: number) =>
            ({ type: "url_citation", url_citation: { url, title, start_index, end_index } });
        h.mock.replies.push({
            chunks: [
                data({ choices: [{ delta: { content: "ร้องโดย Aimer 🎤 " } }] }),
                data({
                    choices: [{
                        delta: { content: "กับ LiSA.", annotations: [citation("https://example.org/a", "a", 8, 15)] },
                        message: { annotations: [citation("https://example.org/b", "b", 20, 24), citation("https://example.org/c", "c", 15, 99)] },
                        finish_reason: "stop",
                    }],
                }),
                "data: [DONE]\n\n",
            ],
        });
        const run = await h.stream("chat-completions");
        expect(cited(run.finished!)).toEqual({ a: "Aimer 🎤", b: "LiSA", c: undefined });
    });

    test("OpenAI Responses: from character indices into the annotated text part", async () => {
        const delta = (content_index: number, text: string) =>
            sse("response.output_text.delta", { type: "response.output_text.delta", item_id: "m1", output_index: 0, content_index, delta: text });
        const annotation = (item_id: string, content_index: number, title: string, start_index: number, end_index: number) =>
            sse("response.output_text.annotation.added", {
                type: "response.output_text.annotation.added", item_id, output_index: 0, content_index, annotation_index: 0,
                annotation: { type: "url_citation", url: `https://example.org/${title}`, title, start_index, end_index },
            });
        h.mock.replies.push({
            chunks: [
                delta(0, "ร้องโดย Aimer 🎤 "),
                annotation("m1", 0, "a", 8, 15),
                delta(1, "กับ LiSA."),
                annotation("m1", 1, "b", 4, 8),
                annotation("m2", 0, "c", 0, 4),
                sse("response.completed", { type: "response.completed", response: { status: "completed", usage: {} } }),
            ],
        });
        const run = await h.stream("responses");
        expect(cited(run.finished!)).toEqual({ a: "Aimer 🎤", b: "LiSA", c: undefined });
    });

    test("Gemini: from UTF-8 byte offsets, only where they land on the segment's text", async () => {
        const support = (startIndex: number, endIndex: number, text: string, chunk: number) =>
            ({ segment: { ...(startIndex ? { startIndex } : {}), endIndex, text }, groundingChunkIndices: [chunk] });
        h.mock.replies.push({
            chunks: [data({
                candidates: [{
                    content: { role: "model", parts: [{ text: kAnswer }] },
                    finishReason: "STOP",
                    groundingMetadata: {
                        groundingChunks: ["a", "b", "c", "d"].map((title) => ({ web: { uri: `https://example.org/${title}`, title } })),
                        groundingSupports: [
                            support(22, 32, "Aimer 🎤", 0),
                            support(43, 47, "LiSA", 1),
                            support(0, 5, "wrong", 2),
                            support(1, 21, "ร้องโดย", 3),
                        ],
                    },
                }],
            })],
        });
        const run = await h.stream("gemini");
        expect(cited(run.finished!)).toEqual({ a: "Aimer 🎤", b: "LiSA", c: undefined, d: undefined });
    });
});
