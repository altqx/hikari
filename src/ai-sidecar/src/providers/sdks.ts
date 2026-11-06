import type * as AnthropicSdk from "@anthropic-ai/sdk";
import type * as GeminiSdk from "@google/genai";
import type * as OpenAiSdk from "openai";

export interface Sdks {
    anthropic: typeof AnthropicSdk;
    gemini: typeof GeminiSdk;
    openai: typeof OpenAiSdk;
}
export type SdkName = keyof Sdks;

const kPackages: Record<SdkName, string> = { anthropic: "@anthropic-ai/sdk", gemini: "@google/genai", openai: "openai" };
let load: (name: SdkName) => unknown = (name) => require(kPackages[name]);
const loaded = new Map<SdkName, unknown>();

/** Where each provider SDK comes from, loaded on its first use. */
export function loadSdksWith(loader: (name: SdkName) => unknown): void {
    load = loader;
    loaded.clear();
}

export function sdk<K extends SdkName>(name: K): Sdks[K] {
    if (!loaded.has(name))
        loaded.set(name, load(name));
    return loaded.get(name) as Sdks[K];
}
