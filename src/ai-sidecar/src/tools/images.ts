import type { Wire } from "../providers/types";

/** The largest image worth sending: past it a provider scales the image down or bills it as more tiles. */
export interface ImageBudget {
    maxEdge: number;
    maxPixels: number;
}

/** From docs/research/hikari-ai-integration.md §4.1 and each provider's vision guide. */
export const kImageBudgets: Record<Wire, ImageBudget> = {
    "anthropic-messages": { maxEdge: 1568, maxPixels: 1_192_464 },
    responses: { maxEdge: 2048, maxPixels: 2_560_000 },
    "chat-completions": { maxEdge: 2048, maxPixels: 1_048_576 },
    gemini: { maxEdge: 1536, maxPixels: 2_359_296 },
};

/** Every budget at once, for a caller whose provider isn't known, such as an external agent over MCP. */
export const kDefaultImageBudget: ImageBudget = {
    maxEdge: Math.min(...Object.values(kImageBudgets).map((b) => b.maxEdge)),
    maxPixels: Math.min(...Object.values(kImageBudgets).map((b) => b.maxPixels)),
};
