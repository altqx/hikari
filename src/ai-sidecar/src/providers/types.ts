export const kWires = ["chat-completions", "responses", "anthropic-messages", "gemini"] as const;
export type Wire = typeof kWires[number];

export const kPresets = ["openrouter", "openai", "anthropic", "gemini", "custom"] as const;
export type Preset = typeof kPresets[number];

/** A provider as Qt sends it in `_hikari/providers.set`: everything but the key. */
export interface ProviderConfig {
    id: string;
    preset: Preset;
    wire: Wire;
    /** Ends before the endpoint path, e.g. "https://api.openai.com/v1". */
    baseUrl: string;
}

export interface ProvidersSetParams {
    providers: ProviderConfig[];
}

export type Part =
    | { type: "text"; text: string }
    | { type: "image"; mediaType: string; base64: string }
    | ToolCall
    | { type: "toolResult"; callId: string; content: string; isError?: boolean };

export interface ToolCall {
    type: "toolCall";
    id: string;
    name: string;
    argumentsJson: string;
    /** Opaque provider state that must go back with the call in the history (Gemini's thought signature). */
    signature?: string;
}

export interface Message {
    author: "user" | "assistant";
    parts: Part[];
}

export interface ToolDefinition {
    name: string;
    description: string;
    /** A JSON Schema object. */
    parametersJson: string;
}

export type CacheLifetime = "minutes" | "hour";

/**
 * Sent as system, tools sorted by name, context, then messages, so everything before the messages is a prefix the
 * provider can cache. Nothing that changes per turn belongs in system, tools or context.
 */
export interface Request {
    model: string;
    system?: string;
    messages: Message[];
    tools?: ToolDefinition[];
    /** Pinned material that changes rarely, such as the document summary or the style list. */
    context?: string;
    /** The same for every turn of one conversation, so the provider routes its turns to one cache. */
    cacheKey?: string;
    cacheLifetime?: CacheLifetime;
    /**
     * Keeps system, tools and context in an explicit provider cache for the conversation (Gemini), for a large fixed
     * context reused over many turns. Needs cacheKey. Where no cache can be made, the request goes out whole.
     */
    explicitCache?: boolean;
    maxOutputTokens?: number;
}

export interface CompleteParams {
    /** Names this completion in its `session/update` notifications. */
    stream: number;
    providerId: string;
    /** Empty or absent for a provider that needs none. */
    key?: string;
    request: Request;
}

/** Deletes the explicit cache kept for one conversation. */
export interface ConversationEndParams {
    providerId: string;
    key?: string;
    cacheKey: string;
}

export interface ModelsListParams {
    providerId: string;
    key?: string;
}

export interface Usage {
    inputTokens: number;
    outputTokens: number;
    /** Part of inputTokens: read from the provider's cache. */
    cachedInputTokens: number;
    /** Part of inputTokens: written to the provider's cache. */
    cacheWriteTokens: number;
    /** Part of cacheWriteTokens: kept for an hour. */
    cacheWriteHourTokens: number;
    /** What the provider says the request cost, when it says. */
    reportedCost?: number;
}

export type StopReason = "end" | "toolUse" | "maxTokens" | "refused";

export interface Finished {
    reason: StopReason;
    text: string;
    toolCalls: ToolCall[];
    usage: Usage;
}

export type Update =
    | { kind: "text"; text: string }
    | { kind: "toolCall"; index: number; id: string; name: string }
    | { kind: "toolArguments"; index: number; delta: string }
    | { kind: "usage"; usage: Usage };

export interface UpdateParams {
    stream: number;
    update: Update;
}

export const kErrorKinds = [
    "noProvider", "noModel", "key", "insecureKey", "network", "timeout", "unauthorized", "rateLimited", "rejected",
    "server", "malformed", "cancelled",
] as const;
export type ErrorKind = typeof kErrorKinds[number];

/** The `data` of a provider failure's JSON-RPC error. */
export interface ErrorData {
    kind: ErrorKind;
    httpStatus: number;
    /** The provider's own message, shown to the user as is. */
    detail: string;
}

export interface Capabilities {
    tools: boolean;
    vision: boolean;
    video: boolean;
}

/** US dollars per million tokens. */
export interface Price {
    input: number;
    output: number;
    cachedInput?: number;
    cacheWrite?: number;
    cacheWriteHour?: number;
}

export interface Model {
    id: string;
    name: string;
    /** Absent when the catalogue doesn't say; the provider's defaults apply. */
    capabilities?: Capabilities;
    price?: Price;
    contextTokens?: number;
}

export interface ModelsListResult {
    models: Model[];
}
