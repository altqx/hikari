import { kErrorKinds, kPresets, kWires } from "./types";

/** The subset of JSON Schema the protocol uses; `validate` understands exactly this. */
export type Schema =
    | { type: "string"; enum?: readonly string[] }
    | { type: "number" | "integer" | "boolean" }
    | { type: "array"; items: Schema }
    | { type: "object"; properties: Record<string, Schema>; required?: readonly string[] }
    | { oneOf: readonly Schema[] }
    | { const: string };

const string = { type: "string" } as const;
const integer = { type: "integer" } as const;
const number = { type: "number" } as const;
const boolean = { type: "boolean" } as const;

function object(properties: Record<string, Schema>, required: readonly string[] = Object.keys(properties)): Schema {
    return { type: "object", properties, required };
}

const toolCall = object({ type: { const: "toolCall" }, id: string, name: string, argumentsJson: string, signature: string },
    ["type", "id", "name", "argumentsJson"]);
const part: Schema = {
    oneOf: [
        object({ type: { const: "text" }, text: string }),
        object({ type: { const: "image" }, mediaType: string, base64: string }),
        toolCall,
        object({ type: { const: "toolResult" }, callId: string, content: string, isError: boolean },
            ["type", "callId", "content"]),
    ],
};
const usage = object({ inputTokens: integer, outputTokens: integer, cachedInputTokens: integer, reportedCost: number },
    ["inputTokens", "outputTokens", "cachedInputTokens"]);

export const kRequestSchema = object({
    model: string,
    system: string,
    messages: { type: "array", items: object({ author: { type: "string", enum: ["user", "assistant"] }, parts: { type: "array", items: part } }) },
    tools: { type: "array", items: object({ name: string, description: string, parametersJson: string }) },
    maxOutputTokens: integer,
}, ["model", "messages"]);

export const kSchemas = {
    providersSetParams: object({
        providers: {
            type: "array",
            items: object({ id: string, preset: { type: "string", enum: kPresets }, wire: { type: "string", enum: kWires }, baseUrl: string }),
        },
    }),
    completeParams: object({ stream: integer, providerId: string, key: string, request: kRequestSchema },
        ["stream", "providerId", "request"]),
    modelsListParams: object({ providerId: string, key: string }, ["providerId"]),
    finished: object({
        reason: { type: "string", enum: ["end", "toolUse", "maxTokens", "refused"] },
        text: string,
        toolCalls: { type: "array", items: toolCall },
        usage,
    }),
    updateParams: object({
        stream: integer,
        update: {
            oneOf: [
                object({ kind: { const: "text" }, text: string }),
                object({ kind: { const: "toolCall" }, index: integer, id: string, name: string }),
                object({ kind: { const: "toolArguments" }, index: integer, delta: string }),
                object({ kind: { const: "usage" }, usage }),
            ],
        },
    }),
    errorData: object({ kind: { type: "string", enum: kErrorKinds }, httpStatus: integer, detail: string }),
    modelsListResult: object({
        models: {
            type: "array",
            items: object({
                id: string,
                name: string,
                capabilities: object({ tools: boolean, vision: boolean, video: boolean }),
                price: object({ input: number, output: number, cachedInput: number }, ["input", "output"]),
                contextTokens: integer,
            }, ["id", "name"]),
        },
    }),
} as const;

/** The first place `value` breaks `schema`, as a path and a reason, or undefined when it fits. */
export function validate(schema: Schema, value: unknown, path = "$"): string | undefined {
    if ("const" in schema)
        return value === schema.const ? undefined : `${path} must be "${schema.const}"`;
    if ("oneOf" in schema)
        return schema.oneOf.some((s) => validate(s, value, path) === undefined) ? undefined : `${path} matches no variant`;
    switch (schema.type) {
    case "string":
        if (typeof value !== "string")
            return `${path} must be a string`;
        return schema.enum && !schema.enum.includes(value) ? `${path} must be one of ${schema.enum.join(", ")}` : undefined;
    case "integer":
        return Number.isInteger(value) ? undefined : `${path} must be an integer`;
    case "number":
        return typeof value === "number" && Number.isFinite(value) ? undefined : `${path} must be a number`;
    case "boolean":
        return typeof value === "boolean" ? undefined : `${path} must be a boolean`;
    case "array":
        if (!Array.isArray(value))
            return `${path} must be an array`;
        for (const [i, item] of value.entries()) {
            const broken = validate(schema.items, item, `${path}[${i}]`);
            if (broken)
                return broken;
        }
        return undefined;
    case "object": {
        if (typeof value !== "object" || value === null || Array.isArray(value))
            return `${path} must be an object`;
        const record = value as Record<string, unknown>;
        for (const name of schema.required ?? [])
            if (record[name] === undefined)
                return `${path}.${name} is missing`;
        for (const [name, child] of Object.entries(schema.properties)) {
            if (record[name] === undefined)
                continue;
            const broken = validate(child, record[name], `${path}.${name}`);
            if (broken)
                return broken;
        }
        return undefined;
    }
    }
}
