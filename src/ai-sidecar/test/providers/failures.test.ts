import { expect, test } from "bun:test";

import type { ErrorKind, Wire } from "../../src/providers/types";
import { Harness } from "./harness";
import { data, sse, waitFor } from "./mock";

const h = new Harness().install();

const kWires: Wire[] = ["chat-completions", "responses", "anthropic-messages", "gemini"];

test("HTTP errors carry the provider's message", async () => {
    const cases: [number, ErrorKind][] = [[401, "unauthorized"], [403, "unauthorized"], [429, "rateLimited"], [400, "rejected"], [503, "server"]];
    for (const wire of kWires)
        for (const [status, kind] of cases) {
            h.mock.replies.push({ status, contentType: "application/json", chunks: [`{"error":{"type":"x","message":"nope ${status}"}}`] });
            const run = await h.stream(wire);
            expect({ wire, status, error: run.error }).toEqual({ wire, status, error: { kind, httpStatus: status, detail: `nope ${status}` } });
        }
});

test("errors inside the stream fail the request", async () => {
    h.mock.replies.push({ chunks: [data({ choices: [{ delta: { content: "a" } }] }), data({ error: { code: 429, message: "slow down" } })] });
    let run = await h.stream("chat-completions");
    expect(run.error).toEqual({ kind: "rateLimited", httpStatus: 0, detail: "slow down" });

    h.mock.replies.push({ chunks: [sse("error", { type: "error", code: "subscription_sharing_usage_limit_exceeded", message: "limit" })] });
    run = await h.stream("responses");
    expect(run.error!.kind).toBe("rateLimited");

    h.mock.replies.push({ chunks: [sse("error", { type: "error", error: { type: "overloaded_error", message: "Overloaded" } })] });
    run = await h.stream("anthropic-messages");
    expect(run.error).toEqual({ kind: "server", httpStatus: 0, detail: "Overloaded" });
    expect(run.finished).toBeUndefined();

    h.mock.replies.push({
        chunks: [data({ candidates: [{ content: { parts: [{ text: "a" }] } }] }),
            data({ error: { code: 429, message: "Resource exhausted", status: "RESOURCE_EXHAUSTED" } })],
    });
    run = await h.stream("gemini");
    expect(run.error!.kind).toBe("rateLimited");
    expect(run.error!.detail).toBe("Resource exhausted");
});

test("a stream that stops early fails", async () => {
    h.mock.replies.push({ chunks: [data({ choices: [{ delta: { content: "half" } }] })] });
    expect((await h.stream("chat-completions")).error!.kind).toBe("malformed");

    h.mock.replies.push({ chunks: [sse("content_block_delta", { type: "content_block_delta", index: 0, delta: { type: "text_delta", text: "x" } })], end: "reset" });
    expect(["network", "malformed"]).toContain((await h.stream("anthropic-messages")).error!.kind);

    h.mock.replies.push({ chunks: ["data: {not json\n\n"] });
    expect((await h.stream("responses")).error!.kind).toBe("malformed");

    h.mock.replies.push({ chunks: [data({ candidates: [{ content: { parts: [{ text: "half" }] } }] })] });
    expect((await h.stream("gemini")).error!.kind).toBe("malformed");
});

test("a silent stream times out and closes the connection", async () => {
    h.idleTimeoutMs = 300;
    h.mock.replies.push({ chunks: [data({ choices: [{ delta: { content: "a" } }] })], end: "hang" });
    const run = await h.stream("chat-completions");
    expect(run.text()).toBe("a");
    expect(run.error!.kind).toBe("timeout");
    expect(await waitFor(() => h.mock.disconnects === 1)).toBe(true);
});

test("cancel closes the connection and ends the request once", async () => {
    h.mock.replies.push({ chunks: [data({ choices: [{ delta: { content: "a" } }] })], end: "hang" });
    const controller = new AbortController();
    const pending = h.stream("chat-completions", undefined, { signal: controller.signal });
    expect(await waitFor(() => h.mock.received.length === 1)).toBe(true);
    await Bun.sleep(50);
    controller.abort();
    const run = await pending;
    expect(run.error!.kind).toBe("cancelled");
    expect(run.finished).toBeUndefined();
    expect(await waitFor(() => h.mock.disconnects === 1)).toBe(true);
});
