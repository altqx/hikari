import { ErrorCode, type Scope } from "../protocol";
import { RpcError } from "../rpc";
import type { OperationName, Operations } from "../tools/api.gen";
import type { HikariCalls } from "../tools/toolset";

type Line = Record<string, unknown> & { id: number };

const kReads = new Set<string>(["lines.get", "lines.list", "lines.find"]);

function isObject(value: unknown): value is Record<string, unknown> {
    return typeof value === "object" && value !== null && !Array.isArray(value);
}

function isStale(error: unknown): boolean {
    return error instanceof RpcError && error.code === ErrorCode.hikariApi && isObject(error.data)
        && error.data.code === "stale_revision";
}

function editedLines(edits: unknown): number[] | undefined {
    if (!Array.isArray(edits))
        return undefined;
    const ids: number[] = [];
    for (const edit of edits) {
        if (!isObject(edit) || typeof edit.line !== "number")
            return undefined;
        ids.push(edit.line);
    }
    return ids;
}

function same(read: Line, now: Line): boolean {
    return Object.keys(read).every((key) => key === "row" || JSON.stringify(read[key]) === JSON.stringify(now[key]));
}

/**
 * A sub-agent's calls: a `document.apply` that fails with `stale_revision` runs once more against the new revision
 * when every Line it edits still reads as the sub-agent last read it. Only edits of scoped Lines qualify.
 */
export function retryingStaleInScope(calls: HikariCalls, scope: Scope): HikariCalls {
    const scoped = scope.lines ? new Set(scope.lines) : undefined;
    const read = new Map<number, Line>();
    const ours = (params: unknown) => {
        const document = isObject(params) ? params.document : undefined;
        return document === undefined || document === scope.document;
    };
    const remember = (result: unknown) => {
        if (isObject(result) && Array.isArray(result.lines))
            for (const line of result.lines)
                if (isObject(line) && typeof line.id === "number")
                    read.set(line.id, line as Line);
    };
    const unchanged = async (ids: number[], document: unknown): Promise<number | undefined> => {
        const fields = [...new Set(ids.flatMap((id) => Object.keys(read.get(id)!)))].filter((k) => k !== "id" && k !== "row");
        const params: Record<string, unknown> = { ids, fields };
        if (document !== undefined)
            params.document = document;
        const now = await calls.call("lines.get", params as never) as { revision: number; lines: Line[] };
        const byId = new Map(now.lines.map((line) => [line.id, line]));
        return ids.every((id) => byId.has(id) && same(read.get(id)!, byId.get(id)!)) ? now.revision : undefined;
    };
    return {
        async call<O extends OperationName>(operation: O, params?: Operations[O]["params"]) {
            try {
                const result = await calls.call(operation, params);
                if (kReads.has(operation) && ours(params))
                    remember(result);
                return result;
            } catch (error) {
                if (operation !== "document.apply" || !isStale(error) || !ours(params) || !scoped)
                    throw error;
                const p = params as Record<string, unknown>;
                const ids = editedLines(p.edits);
                if (!ids || ids.length === 0 || !ids.every((id) => scoped.has(id) && read.has(id)))
                    throw error;
                const revision = await unchanged(ids, p.document).catch(() => undefined);
                if (revision === undefined)
                    throw error;
                return calls.call(operation, { ...p, revision } as never);
            }
        },
    };
}
