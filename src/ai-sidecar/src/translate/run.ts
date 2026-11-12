import { ErrorCode, type TranslateParams, type TranslateProgressParams, type TranslateResult } from "../protocol";
import type { Finished, Request } from "../providers/types";
import { RpcError } from "../rpc";
import { kProjectFiles, type ProjectFileName, ProjectReference } from "./honya";
import { TranslationPipeline } from "./pipeline";

export interface TranslateDeps {
    complete(providerId: string, request: Request, signal: AbortSignal): Promise<Finished>;
    /** HA1's `translation.project`: the Document's project files, or undefined without a project folder. */
    project(document: number): Promise<{ name: string; data: string }[] | undefined>;
    notify(params: TranslateProgressParams): void;
}

function isModel(value: unknown): boolean {
    const m = value as Record<string, unknown> | null;
    return typeof m === "object" && m !== null && typeof m.providerId === "string" && typeof m.model === "string";
}

export function checkedTranslate(params: unknown): TranslateParams {
    const p = (params ?? {}) as Record<string, unknown>;
    const fail = (why: string) => new RpcError(ErrorCode.invalidParams, `_hikari/translate ${why}`);
    if (typeof p.stream !== "number")
        throw fail("needs a stream");
    if (typeof p.language !== "string" || p.language.trim().length === 0)
        throw fail("needs a language");
    const models = p.models as Record<string, unknown> | undefined;
    if (!models || !isModel(models.translator) || !isModel(models.reviewer))
        throw fail("needs translator and reviewer models");
    if (!Array.isArray(p.lines) || p.lines.length === 0 || !p.lines.every((l) =>
        typeof l?.key === "number" && typeof l.text === "string" && typeof l.start === "number" && typeof l.end === "number"
        && (l.speaker === undefined || typeof l.speaker === "string")))
        throw fail("needs lines, each with a key, a text, a start and an end");
    if (new Set(p.lines.map((l: { key: number }) => l.key)).size !== p.lines.length)
        throw fail("needs a different key for each Line");
    if (p.document !== undefined && typeof p.document !== "number")
        throw fail("names its document by number");
    if (p.before !== undefined && !(Array.isArray(p.before) && p.before.every((b) => typeof b === "string")))
        throw fail("takes before as a list of texts");
    if (p.retries !== undefined && !(Number.isInteger(p.retries) && (p.retries as number) >= 0 && (p.retries as number) <= 10))
        throw fail("takes 0 to 10 retries");
    if (p.chunkTokens !== undefined && !(Number.isInteger(p.chunkTokens) && (p.chunkTokens as number) >= 50))
        throw fail("takes chunkTokens of 50 or more");
    return p as unknown as TranslateParams;
}

export async function translate(params: TranslateParams, signal: AbortSignal, deps: TranslateDeps): Promise<TranslateResult> {
    const files: Partial<Record<ProjectFileName, string>> = {};
    if (params.document !== undefined)
        for (const file of (await deps.project(params.document)) ?? [])
            if ((kProjectFiles as readonly string[]).includes(file.name))
                files[file.name as ProjectFileName] = file.data;
    const pipeline = new TranslationPipeline({
        language: params.language,
        models: params.models,
        lines: params.lines,
        before: params.before,
        retries: params.retries,
        caching: params.caching,
        chunkTokens: params.chunkTokens,
        cacheKey: `translate:${params.stream}`,
    }, new ProjectReference(files), {
        complete: (model, request, s) => deps.complete(model.providerId, request, s),
        usage: (role, usage) => deps.notify({ stream: params.stream, role, usage }),
        progress: (stage, done, total) => deps.notify({ stream: params.stream, stage, done, total }),
    }, signal);
    return pipeline.run();
}
