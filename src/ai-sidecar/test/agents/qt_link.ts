import { ErrorCode, Method } from "../../src/protocol";
import { Peer } from "../../src/rpc";

export type Answer = unknown | { error: { code: string; message: string; data?: unknown } };

/** Qt's end of the link: answers `_hikari/api.call` from `answers`, each operation's in turn. */
export function qtLink(answers: Record<string, Answer[]>) {
    const calls: { operation: string; params?: unknown; session?: string }[] = [];
    let peer: Peer;
    const feed = (message: unknown) => peer.feed(new TextEncoder().encode(JSON.stringify(message) + "\n"));
    peer = new Peer((line) => {
        const message = JSON.parse(line) as { id?: number; method?: string;
            params: { operation: string; params?: unknown; session?: string } };
        if (message.method !== Method.apiCall)
            return;
        calls.push(message.params);
        const answer = answers[message.params.operation]?.shift();
        queueMicrotask(() => {
            if (typeof answer === "object" && answer !== null && "error" in answer) {
                const { code, message: text, data } = (answer as { error: { code: string; message: string; data?: unknown } }).error;
                feed({ jsonrpc: "2.0", id: message.id, error: { code: ErrorCode.hikariApi, message: text, data: { code, data } } });
            } else {
                feed({ jsonrpc: "2.0", id: message.id, result: answer ?? {} });
            }
        });
    }, () => {});
    return { peer, calls };
}

export const kStale = { error: { code: "stale_revision", message: "The Document changed.", data: { current: 9 } } };
