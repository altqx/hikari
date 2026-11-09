import type { Message, ProviderConfig, Request } from "./types";

/** Whether the provider searches the web itself; the others search through the user's search service. */
export function searchesNatively(config: ProviderConfig): boolean {
    switch (config.preset) {
    case "anthropic":
        return config.wire === "anthropic-messages";
    case "openai":
        return config.wire === "responses";
    case "gemini":
        return config.wire === "gemini";
    case "openrouter":
        return config.wire === "chat-completions";
    case "custom":
        return false;
    }
}

export function nativeSearch(config: ProviderConfig, request: Request): boolean {
    return request.webSearch === true && searchesNatively(config);
}

/** The assistant message as this wire sent it, when the completion kept it. */
export function providerState(message: Message, wire: ProviderConfig["wire"]): unknown[] | undefined {
    for (const part of message.parts) {
        if (part.type !== "providerState" || part.wire !== wire)
            continue;
        try {
            const parsed: unknown = JSON.parse(part.json);
            if (Array.isArray(parsed))
                return parsed;
        } catch {}
    }
    return undefined;
}
