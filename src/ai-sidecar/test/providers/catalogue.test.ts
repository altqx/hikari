import { expect, test } from "bun:test";

import type { Model } from "../../src/providers/types";
import { Harness, kKey } from "./harness";

const h = new Harness().install();

test("the catalogue reads capabilities and prices", async () => {
    h.mock.replies.push({
        contentType: "application/json",
        chunks: [JSON.stringify({
            data: [
                {
                    id: "vendor/vision", name: "Vision", context_length: 200000,
                    architecture: { input_modalities: ["text", "image"] }, supported_parameters: ["tools", "temperature"],
                    pricing: { prompt: "0.000003", completion: "0.000015", input_cache_read: "0.0000003" },
                },
                { id: "vendor/plain", architecture: { input_modalities: ["text"] }, supported_parameters: [] },
                { id: "claude-x", display_name: "Claude X", type: "model" },
                { name: "no id" },
            ],
        })],
    });
    const models = await h.models("chat-completions") as Model[];
    expect(models).toHaveLength(3);
    expect(models[0]!.capabilities).toEqual({ tools: true, vision: true, video: false });
    expect(models[0]!.contextTokens).toBe(200000);
    expect(models[0]!.price!.input).toBeCloseTo(3.0, 9);
    expect(models[0]!.price!.output).toBeCloseTo(15.0, 9);
    expect(models[0]!.price!.cachedInput!).toBeCloseTo(0.3, 9);
    expect(models[1]!.capabilities).toEqual({ tools: false, vision: false, video: false });
    expect(models[1]!.name).toBe("vendor/plain");
    expect(models[2]!.name).toBe("Claude X");
    expect(models[2]!.capabilities).toBeUndefined();
    expect(h.mock.received[0]!.method).toBe("GET");
    expect(h.mock.received[0]!.path).toBe("/v1/models");

    h.mock.replies.push({ contentType: "application/json", chunks: ['{"data":[]}'] });
    expect(await h.models("anthropic-messages")).toEqual([]);
    expect(h.mock.received[1]!.path).toBe("/v1/models?limit=1000");
    expect(h.mock.received[1]!.headers["x-api-key"]).toBe(kKey);

    h.mock.replies.push({ status: 401, contentType: "application/json", chunks: ['{"error":{"message":"bad key"}}'] });
    expect(await h.models("responses")).toEqual({ kind: "unauthorized", httpStatus: 401, detail: "bad key" });
});

test("the Gemini catalogue lists generateContent models", async () => {
    h.mock.replies.push({
        contentType: "application/json",
        chunks: [JSON.stringify({
            models: [
                {
                    name: "models/gemini-x-flash", baseModelId: "gemini-x-flash", version: "001", displayName: "Gemini X Flash",
                    inputTokenLimit: 1048576, outputTokenLimit: 65536,
                    supportedGenerationMethods: ["generateContent", "countTokens", "createCachedContent"], thinking: true,
                },
                { name: "models/text-embedding", displayName: "Embedding", supportedGenerationMethods: ["embedContent"] },
                { name: "models/gemini-y", supportedGenerationMethods: ["generateContent"] },
            ],
            nextPageToken: "",
        })],
    });
    expect(await h.models("gemini")).toEqual([
        { id: "gemini-x-flash", name: "Gemini X Flash", contextTokens: 1048576 },
        { id: "gemini-y", name: "gemini-y" },
    ]);
    const r = h.mock.received[0]!;
    expect(r.method).toBe("GET");
    expect(r.path).toBe("/v1/models?pageSize=1000");
    expect(r.headers["x-goog-api-key"]).toBe(kKey);
    expect(r.path).not.toContain(kKey);
});
