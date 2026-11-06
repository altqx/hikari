import { expect, test } from "bun:test";

import { ErrorCode, Method } from "../../src/protocol";
import { ProviderError } from "../../src/providers/errors";
import { kKeyPlaceholder, LinkRelay, relayFetch } from "../../src/providers/relay";
import { Peer } from "../../src/rpc";
import { Harness, kKey } from "./harness";
import { data, waitFor } from "./mock";

const h = new Harness().install();

type Message = Record<string, any>;

function link() {
    const sent: Message[] = [];
    const peer = new Peer((line) => sent.push(JSON.parse(line)), () => {});
    const feed = (message: unknown) => peer.feed(new TextEncoder().encode(JSON.stringify(message) + "\n"));
    const relay = new LinkRelay(peer);
    const fetchFn = relayFetch(relay, { kind: "provider", id: "p" });
    const request = () => sent.find((m) => m.method === Method.httpRequest);
    return { sent, feed, fetchFn, request };
}

test("the SDKs send only the placeholder; the key never reaches the sidecar", async () => {
    h.mock.replies.push({ chunks: [data({ choices: [{ delta: { content: "ok" }, finish_reason: "stop" }] }), "data: [DONE]\n\n"] });
    expect((await h.stream("chat-completions")).text()).toBe("ok");
    const sent = h.relay.sent[0]!;
    expect(sent.target).toEqual({ kind: "provider", id: "mock" });
    expect(sent.headers).toContainEqual(["authorization", `Bearer ${kKeyPlaceholder}`]);
    expect(JSON.stringify(sent)).not.toContain(kKey);
    expect(h.mock.received[0]!.headers.authorization).toBe(`Bearer ${kKey}`);
});

test("a keyless provider's key header is dropped by Qt", async () => {
    h.relay.key = "";
    h.mock.replies.push({ chunks: [data({ choices: [{ delta: { content: "ok" }, finish_reason: "stop" }] }), "data: [DONE]\n\n"] });
    expect((await h.stream("chat-completions")).text()).toBe("ok");
    expect(h.mock.received[0]!.headers.authorization).toBeUndefined();
});

test("a relayed response streams its body as the data notifications arrive", async () => {
    const l = link();
    const pending = l.fetchFn("https://api.example.com/v1/chat", { method: "POST", body: "{\"x\":1}",
        headers: { authorization: `Bearer ${kKeyPlaceholder}` } });
    expect(await waitFor(() => l.request() !== undefined)).toBe(true);
    const { id, params } = l.request()!;
    expect(params).toMatchObject({ target: { kind: "provider", id: "p" }, method: "POST", url: "https://api.example.com/v1/chat" });
    expect(Buffer.from(params.body, "base64").toString()).toBe("{\"x\":1}");
    l.feed({ jsonrpc: "2.0", method: Method.httpResponse, params: { stream: params.stream, status: 200, headers: [["content-type", "text/event-stream"]] } });
    const response = await pending;
    expect(response.headers.get("content-type")).toBe("text/event-stream");
    const reader = response.body!.getReader();
    l.feed({ jsonrpc: "2.0", method: Method.httpData, params: { stream: params.stream, data: Buffer.from("data: one\n\n").toString("base64") } });
    expect(new TextDecoder().decode((await reader.read()).value)).toBe("data: one\n\n");
    l.feed({ jsonrpc: "2.0", id, result: {} });
    expect((await reader.read()).done).toBe(true);
});

test("aborting a relayed request asks Qt to cancel it", async () => {
    const l = link();
    const controller = new AbortController();
    const pending = l.fetchFn("https://api.example.com/v1/chat", { signal: controller.signal });
    expect(await waitFor(() => l.request() !== undefined)).toBe(true);
    controller.abort(new ProviderError("cancelled"));
    expect(l.sent).toContainEqual({ jsonrpc: "2.0", method: Method.cancelRequest, params: { requestId: l.request()!.id } });
    l.feed({ jsonrpc: "2.0", id: l.request()!.id, error: { code: ErrorCode.requestCancelled, message: "cancelled" } });
    await expect(pending).rejects.toMatchObject({ kind: "cancelled" });
});

test("Qt's refusal keeps its kind, and a status that isn't 2xx fails with the provider's message", async () => {
    const l = link();
    const refused = l.fetchFn("http://192.168.1.20/v1/chat");
    expect(await waitFor(() => l.request() !== undefined)).toBe(true);
    l.feed({ jsonrpc: "2.0", id: l.request()!.id, error: { code: ErrorCode.providerFailed, message: "insecureKey",
        data: { kind: "insecureKey", httpStatus: 0, detail: "" } } });
    await expect(refused).rejects.toMatchObject({ kind: "insecureKey" });

    const m = link();
    const limited = m.fetchFn("https://api.example.com/v1/chat");
    expect(await waitFor(() => m.request() !== undefined)).toBe(true);
    const { id, params } = m.request()!;
    m.feed({ jsonrpc: "2.0", method: Method.httpResponse, params: { stream: params.stream, status: 429, headers: [] } });
    m.feed({ jsonrpc: "2.0", method: Method.httpData, params: { stream: params.stream,
        data: Buffer.from('{"error":{"message":"slow down"}}').toString("base64") } });
    m.feed({ jsonrpc: "2.0", id, result: {} });
    await expect(limited).rejects.toMatchObject({ kind: "rateLimited", httpStatus: 429, detail: "slow down" });
});

test("a 204 has no body", async () => {
    const l = link();
    const pending = l.fetchFn("https://api.example.com/v1/caches/x", { method: "DELETE" });
    expect(await waitFor(() => l.request() !== undefined)).toBe(true);
    const { id, params } = l.request()!;
    l.feed({ jsonrpc: "2.0", method: Method.httpResponse, params: { stream: params.stream, status: 204, headers: [] } });
    l.feed({ jsonrpc: "2.0", id, result: {} });
    const response = await pending;
    expect(response.status).toBe(204);
    expect(response.body).toBeNull();
});
