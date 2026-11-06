import { afterEach, beforeEach, expect, test } from "bun:test";

import { ErrorCode, kProtocolVersion, Method } from "../../src/protocol";
import { kSchemas, validate } from "../../src/providers/schema";
import { Peer } from "../../src/rpc";
import { Sidecar } from "../../src/sidecar";
import { kKey, RelayPlayer } from "./harness";
import { data, MockProvider, waitFor } from "./mock";

type Message = Record<string, any>;

let mock: MockProvider;
let sent: Message[];
let feed: (message: unknown) => void;

beforeEach(async () => {
    mock = new MockProvider();
    sent = [];
    const player = new RelayPlayer((message) => feed(message));
    const peer = new Peer((line) => {
        const message = JSON.parse(line) as Message;
        sent.push(message);
        player.take(message);
    }, () => {});
    feed = (message) => peer.feed(new TextEncoder().encode(JSON.stringify(message) + "\n"));
    new Sidecar(peer, { buildId: "test", hikariApiVersion: 1 });
    feed({ jsonrpc: "2.0", id: 0, method: Method.initialize, params: { protocolVersion: kProtocolVersion, hikariApiVersion: 1, buildId: "test" } });
    feed({ jsonrpc: "2.0", id: 1, method: Method.providersSet, params: { providers: [{ id: "or", preset: "openrouter", wire: "chat-completions", baseUrl: mock.base }] } });
    await waitFor(() => sent.some((m) => m.id === 1));
});
afterEach(() => mock.stop());

const response = (id: number) => sent.find((m) => m.id === id && !("method" in m));

test("a completion streams session/update notifications and answers with the finished response", async () => {
    mock.replies.push({ chunks: [data({ choices: [{ delta: { content: "Hi" }, finish_reason: "stop" }] }), "data: [DONE]\n\n"] });
    feed({
        jsonrpc: "2.0", id: 2, method: Method.complete,
        params: { stream: 9, providerId: "or", request: { model: "m", messages: [{ author: "user", parts: [{ type: "text", text: "Hello" }] }] } },
    });
    expect(await waitFor(() => response(2) !== undefined)).toBe(true);
    expect(response(1)).toEqual({ jsonrpc: "2.0", id: 1, result: null });
    const updates = sent.filter((m) => m.method === Method.sessionUpdate).map((m) => m.params);
    expect(updates[0]).toEqual({ stream: 9, update: { kind: "text", text: "Hi" } });
    for (const params of updates)
        expect(validate(kSchemas.updateParams, params)).toBeUndefined();
    const result = response(2)!.result;
    expect(validate(kSchemas.finished, result)).toBeUndefined();
    expect(result).toMatchObject({ reason: "end", text: "Hi", toolCalls: [] });
    expect(JSON.stringify(sent)).not.toContain(kKey);
});

test("a failure answers with the provider error code and its kind, status and message", async () => {
    mock.replies.push({ status: 429, contentType: "application/json", chunks: ['{"error":{"message":"slow down"}}'] });
    feed({ jsonrpc: "2.0", id: 2, method: Method.complete, params: { stream: 1, providerId: "or", request: { model: "m", messages: [] } } });
    feed({ jsonrpc: "2.0", id: 3, method: Method.complete, params: { stream: 2, providerId: "nobody", request: { model: "m", messages: [] } } });
    feed({ jsonrpc: "2.0", id: 4, method: Method.complete, params: { stream: 3, providerId: "or", request: { messages: [] } } });
    expect(await waitFor(() => [2, 3, 4].every((id) => response(id) !== undefined))).toBe(true);
    expect(response(2)!.error).toEqual({ code: ErrorCode.providerFailed, message: "slow down", data: { kind: "rateLimited", httpStatus: 429, detail: "slow down" } });
    expect(validate(kSchemas.errorData, response(2)!.error.data)).toBeUndefined();
    expect(response(3)!.error.data.kind).toBe("noProvider");
    expect(response(4)!.error.code).toBe(ErrorCode.invalidParams);
    expect(mock.received).toHaveLength(1);
});

test("$/cancel_request ends a completion with the cancel code and closes its connection", async () => {
    mock.replies.push({ chunks: [data({ choices: [{ delta: { content: "a" } }] })], end: "hang" });
    feed({ jsonrpc: "2.0", id: 2, method: Method.complete, params: { stream: 1, providerId: "or", request: { model: "m", messages: [] } } });
    expect(await waitFor(() => sent.some((m) => m.method === Method.sessionUpdate))).toBe(true);
    feed({ jsonrpc: "2.0", method: Method.cancelRequest, params: { requestId: 2 } });
    expect(await waitFor(() => mock.disconnects === 1)).toBe(true);
    expect(response(2)!.error.code).toBe(ErrorCode.requestCancelled);
    await Bun.sleep(50);
    expect(sent.filter((m) => m.id === 2)).toHaveLength(1);
});

test("models.list answers with the catalogue", async () => {
    mock.replies.push({ contentType: "application/json", chunks: ['{"data":[{"id":"a/b","name":"B"}]}'] });
    feed({ jsonrpc: "2.0", id: 2, method: Method.modelsList, params: { providerId: "or" } });
    expect(await waitFor(() => response(2) !== undefined)).toBe(true);
    expect(response(2)!.result).toEqual({ models: [{ id: "a/b", name: "B" }] });
    expect(validate(kSchemas.modelsListResult, response(2)!.result)).toBeUndefined();
});
