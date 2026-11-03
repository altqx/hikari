import type { Model, Price } from "./types";

type Json = Record<string, unknown>;

const record = (value: unknown): Json => (typeof value === "object" && value !== null ? value as Json : {});
const text = (value: unknown): string => (typeof value === "string" ? value : "");
const count = (value: unknown): number | undefined => (typeof value === "number" ? value : undefined);

/**
 * Models from an OpenAI-style `data` list (OpenAI, OpenRouter, Anthropic, custom servers). Capabilities come
 * from OpenRouter's `architecture` and `supported_parameters`, prices from its per-token `pricing`.
 */
export function catalogueModels(data: unknown[]): Model[] {
    const models: Model[] = [];
    for (const entry of data) {
        const o = record(entry);
        const id = text(o.id);
        if (!id)
            continue;
        const model: Model = { id, name: text(o.name) || text(o.display_name) || id };
        const context = count(o.context_length) ?? count(o.max_input_tokens);
        if (context !== undefined)
            model.contextTokens = context;
        const modalities = record(o.architecture).input_modalities;
        const parameters = o.supported_parameters;
        if (Array.isArray(modalities) || Array.isArray(parameters))
            model.capabilities = {
                tools: Array.isArray(parameters) && parameters.includes("tools"),
                vision: Array.isArray(modalities) && modalities.includes("image"),
                video: false,
            };
        const pricing = record(o.pricing);
        if ("prompt" in pricing && "completion" in pricing) {
            const perMillion = (key: string) => Number(pricing[key]) * 1e6;
            const price: Price = { input: perMillion("prompt"), output: perMillion("completion") };
            if ("input_cache_read" in pricing)
                price.cachedInput = perMillion("input_cache_read");
            if ("input_cache_write" in pricing)
                price.cacheWrite = perMillion("input_cache_write");
            if ("input_cache_write_1h" in pricing)
                price.cacheWriteHour = perMillion("input_cache_write_1h");
            model.price = price;
        }
        models.push(model);
    }
    return models;
}

/** Gemini models that can generate content, without their `models/` prefix. */
export function geminiModels(list: unknown[]): Model[] {
    const models: Model[] = [];
    for (const entry of list) {
        const o = record(entry);
        const methods = o.supportedActions ?? o.supportedGenerationMethods;
        if (!Array.isArray(methods) || !methods.includes("generateContent"))
            continue;
        const id = text(o.name).replace(/^models\//, "");
        if (!id)
            continue;
        const model: Model = { id, name: text(o.displayName) || id };
        const context = count(o.inputTokenLimit);
        if (context !== undefined)
            model.contextTokens = context;
        models.push(model);
    }
    return models;
}
