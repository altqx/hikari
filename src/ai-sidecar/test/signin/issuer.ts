import type { Deps } from "../../src/signin/types";

export interface Seen {
    path: string;
    body: string;
    json(): Record<string, any>;
    form(): URLSearchParams;
}

type Answer = Response | ((seen: Seen) => Response);

const jsonReply = (body: unknown, status = 200, headers: Record<string, string> = {}) =>
    new Response(JSON.stringify(body), { status, headers: { "content-type": "application/json", ...headers } });
export { jsonReply as json };

export const jwt = (claims: Record<string, unknown>) =>
    `e30.${Buffer.from(JSON.stringify(claims)).toString("base64url")}.sig`;

/** auth.openai.com or openrouter.ai on 127.0.0.1: each path answers with its queue, the last answer repeating. */
export class Issuer {
    readonly seen: Seen[] = [];
    private readonly answers = new Map<string, Answer[]>();
    private readonly server = Bun.serve({
        hostname: "127.0.0.1",
        port: 0,
        fetch: (request) => request.text().then((body) => this.answer(new URL(request.url).pathname, body)),
    });

    private answer(path: string, body: string): Response {
        const seen: Seen = { path, body, json: () => JSON.parse(body), form: () => new URLSearchParams(body) };
        this.seen.push(seen);
        const queue = this.answers.get(path) ?? [];
        const answer = queue.length > 1 ? queue.shift()! : queue[0];
        if (!answer)
            return new Response("Not found", { status: 404 });
        return typeof answer === "function" ? answer(seen) : new Response(answer.clone().body, answer);
    }

    get base(): string {
        return `http://127.0.0.1:${this.server.port}`;
    }

    on(path: string, ...answers: Answer[]): this {
        this.answers.set(path, answers);
        return this;
    }

    at(path: string): Seen[] {
        return this.seen.filter((s) => s.path === path);
    }

    stop(): void {
        this.server.stop(true);
    }
}

/** A clock that only moves when a sign-in sleeps. */
export class FakeTime {
    now = 1_000_000;
    readonly sleeps: number[] = [];

    deps(): Partial<Deps> {
        return {
            now: () => this.now,
            sleep: async (ms, signal) => {
                signal.throwIfAborted();
                this.sleeps.push(ms);
                this.now += ms;
                await Promise.resolve();
            },
        };
    }
}

export function freePort(): number {
    const probe = Bun.serve({ hostname: "127.0.0.1", port: 0, fetch: () => new Response() });
    const port = probe.port!;
    probe.stop(true);
    return port;
}
