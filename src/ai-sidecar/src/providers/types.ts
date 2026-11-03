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

export interface Request {
    model: string;
    system?: string;
    messages: Message[];
    tools?: ToolDefinition[];
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

export interface ModelsListParams {
    providerId: string;
    key?: string;
}

export interface Usage {
    inputTokens: number;
    outputTokens: number;
    /** Part of inputTokens. */
    cachedInputTokens: number;
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
