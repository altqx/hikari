import { afterEach, beforeEach } from "bun:test";

import type { Network } from "../../src/providers/egress";
import { ProviderError } from "../../src/providers/errors";
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

export interface Run {
    updates: Update[];
    finished?: Finished;
    error?: ErrorData;
    text(): string;
}

export class Harness {
    mock!: MockProvider;
    names = new Map<string, string[]>();
    resolved: string[] = [];
    localAddresses = ["192.168.1.10"];
    idleTimeoutMs?: number;
    private nextStream = 1;

    readonly network: Network = {
        resolve: async (host) => {
            this.resolved.push(host);
            return this.names.get(host) ?? [];
        },
        localAddresses: () => this.localAddresses,
    };

    install(): this {
        beforeEach(() => {
            this.mock = new MockProvider();
            this.names = new Map();
            this.resolved = [];
            this.localAddresses = ["192.168.1.10"];
            this.idleTimeoutMs = undefined;
        });
        afterEach(() => this.mock.stop());
        return this;
    }

    service(wire: Wire, baseUrl = this.mock.base): ProviderService {
        const service = new ProviderService({ network: this.network, idleTimeoutMs: this.idleTimeoutMs });
        service.set({ providers: [{ id: "mock", preset: kPresetOf[wire], wire, baseUrl }] });
        return service;
    }

    async stream(wire: Wire, request: Request = { model: "m", messages: [{ author: "user", parts: [{ type: "text", text: "Hi" }] }] },
        options: { baseUrl?: string; signal?: AbortSignal; key?: string } = {}): Promise<Run> {
        const run: Run = { updates: [], text: () => run.updates.flatMap((u) => (u.kind === "text" ? [u.text] : [])).join("") };
        const stream = this.nextStream++;
        try {
            run.finished = await this.service(wire, options.baseUrl).complete(
                { stream, providerId: "mock", key: options.key ?? kKey, request },
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
            return (await this.service(wire, baseUrl).models({ providerId: "mock", key: kKey }, new AbortController().signal)).models;
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
