import { afterEach, beforeEach } from "bun:test";

import { ErrorCode, type HttpRequestParams, Method } from "../../src/protocol";
import { ProviderError } from "../../src/providers/errors";
import { kKeyPlaceholder, type Relay, type RelayRequest, type RelayResponse } from "../../src/providers/relay";
import { ProviderService } from "../../src/providers/service";
import type { ErrorData, Finished, Model, Preset, Request, Update, Wire } from "../../src/providers/types";
import { MockProvider } from "./mock";

export const kKey = "sk-test-0123456789abcdef";

const kPresetOf: Record<Wire, Preset> = {
    "chat-completions": "openrouter",
    responses: "openai",
    "anthropic-messages": "anthropic",
    gemini: "gemini",
};

const kDroppedResponseHeaders = new Set(["content-encoding", "content-length", "transfer-encoding", "connection"]);

/** Qt's side of the relay, played with fetch: the key goes where the placeholder is, or the header goes. */
export class TestRelay implements Relay {
    key = kKey;
    sent: RelayRequest[] = [];

    async send(request: RelayRequest, signal: AbortSignal): Promise<RelayResponse> {
        this.sent.push(request);
        const headers = request.headers.flatMap(([name, value]): [string, string][] => {
            if (!value.includes(kKeyPlaceholder))
                return [[name, value]];
            return this.key ? [[name, value.replaceAll(kKeyPlaceholder, this.key)]] : [];
        });
        let response: Response;
        try {
            response = await fetch(request.url, { method: request.method, headers, body: request.body, redirect: "manual", signal });
        } catch (error) {
            if (signal.aborted)
                throw new ProviderError("cancelled");
            throw new ProviderError("network", error instanceof Error ? error.message : String(error));
        }
        return {
            status: response.status,
            headers: [...response.headers].filter(([name]) => !kDroppedResponseHeaders.has(name)),
            body: response.body ?? new ReadableStream({ start: (c) => c.close() }),
        };
    }
}

type Message = Record<string, any>;

/** Plays Qt's end of `_hikari/http.request` on a link: fetches through a TestRelay and feeds the notifications back. */
export class RelayPlayer {
    private readonly running = new Map<number, AbortController>();

    constructor(private readonly feed: (message: unknown) => void, readonly relay = new TestRelay()) {}

    /** Takes a message the sidecar sent if it belongs to the relay. */
    take(message: Message): void {
        if (message.method === Method.httpRequest)
            void this.run(message.id, message.params as HttpRequestParams);
        else if (message.method === Method.cancelRequest)
            this.running.get(message.params?.requestId)?.abort();
    }

    private async run(id: number, params: HttpRequestParams): Promise<void> {
        const controller = new AbortController();
        this.running.set(id, controller);
        const body = params.body ? new Uint8Array(Buffer.from(params.body, "base64")) : undefined;
        try {
            const response = await this.relay.send({ ...params, body }, controller.signal);
            this.feed({ jsonrpc: "2.0", method: Method.httpResponse,
                params: { stream: params.stream, status: response.status, headers: response.headers } });
            for await (const chunk of response.body)
                this.feed({ jsonrpc: "2.0", method: Method.httpData,
                    params: { stream: params.stream, data: Buffer.from(chunk).toString("base64") } });
            this.feed({ jsonrpc: "2.0", id, result: {} });
        } catch (error) {
            this.feed({ jsonrpc: "2.0", id, error: controller.signal.aborted
                ? { code: ErrorCode.requestCancelled, message: "cancelled" }
                : { code: ErrorCode.providerFailed, message: String(error), data: (error as ProviderError).data } });
        } finally {
            this.running.delete(id);
        }
    }
}

export interface Run {
    updates: Update[];
    finished?: Finished;
    error?: ErrorData;
    text(): string;
}

export class Harness {
    mock!: MockProvider;
    relay = new TestRelay();
    idleTimeoutMs?: number;
    clock = 0;
    private nextStream = 1;

    install(): this {
        beforeEach(() => {
            this.mock = new MockProvider();
            this.relay = new TestRelay();
            this.idleTimeoutMs = undefined;
            this.clock = 0;
        });
        afterEach(() => this.mock.stop());
        return this;
    }

    service(wire: Wire, baseUrl = this.mock.base, preset = kPresetOf[wire]): ProviderService {
        const service = new ProviderService({ relay: this.relay, idleTimeoutMs: this.idleTimeoutMs, now: () => this.clock });
        service.set({ providers: [{ id: "mock", preset, wire, baseUrl }] });
        return service;
    }

    async stream(wire: Wire, request: Request = { model: "m", messages: [{ author: "user", parts: [{ type: "text", text: "Hi" }] }] },
        options: { baseUrl?: string; signal?: AbortSignal } = {}): Promise<Run> {
        return this.turn(this.service(wire, options.baseUrl), request, options);
    }

    /** One completion on a service that lives across turns. */
    async turn(service: ProviderService, request: Request, options: { signal?: AbortSignal } = {}): Promise<Run> {
        const run: Run = { updates: [], text: () => run.updates.flatMap((u) => (u.kind === "text" ? [u.text] : [])).join("") };
        const stream = this.nextStream++;
        try {
            run.finished = await service.complete(
                { stream, providerId: "mock", request },
                options.signal ?? new AbortController().signal,
                (s, update) => {
                    if (s === stream)
                        run.updates.push(update);
                });
        } catch (error) {
            if (!(error instanceof ProviderError))
                throw error;
            run.error = error.data;
        }
        return run;
    }

    async models(wire: Wire, baseUrl = this.mock.base): Promise<Model[] | ErrorData> {
        try {
            return (await this.service(wire, baseUrl).models({ providerId: "mock" }, new AbortController().signal)).models;
        } catch (error) {
            if (!(error instanceof ProviderError))
                throw error;
            return error.data;
        }
    }
}

export function fullRequest(): Request {
    return {
        model: "the-model",
        system: "Be brief.",
        maxOutputTokens: 100,
        tools: [{ name: "get_line", description: "Reads a Line", parametersJson: '{"type":"object","properties":{"n":{"type":"integer"}}}' }],
        messages: [
            { author: "user", parts: [{ type: "text", text: "What is on screen?" }, { type: "image", mediaType: "image/png", base64: "iVBORw0KGgo=" }] },
            {
                author: "assistant",
                parts: [{ type: "text", text: "Let me look." },
                    { type: "toolCall", id: "call_1", name: "get_line", argumentsJson: '{"n":3}', signature: "c2lnLTE=" }],
            },
            { author: "user", parts: [{ type: "toolResult", callId: "call_1", content: "Dialogue: 0,...", isError: false }] },
        ],
    };
}
