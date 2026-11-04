import { SignInError } from "./types";

const kDonePage = `<!doctype html><meta charset="utf-8"><title>HikariSub</title>
<p style="font:16px system-ui;margin:3em">You're signed in. You can close this tab and go back to HikariSub.</p>`;

/** The browser's redirect back to HikariSub: listens on 127.0.0.1 only, and only until it closes. */
export class Loopback {
    private waiting?: (params: URLSearchParams) => void;
    private early?: URLSearchParams;

    private constructor(private readonly server: ReturnType<typeof Bun.serve>, readonly path: string) {}

    /** Port 0 picks a free port. A taken port throws portBusy. */
    static open(port: number, path: string): Loopback {
        let loopback: Loopback | undefined;
        try {
            const server = Bun.serve({
                hostname: "127.0.0.1",
                port,
                fetch: (request) => loopback!.answer(request),
            });
            loopback = new Loopback(server, path);
            return loopback;
        } catch (error) {
            if ((error as { code?: string }).code === "EADDRINUSE")
                throw new SignInError("portBusy", "", `port ${port} is in use`);
            throw error;
        }
    }

    static isFree(port: number): boolean {
        try {
            Loopback.open(port, "/").close();
            return true;
        } catch {
            return false;
        }
    }

    get port(): number {
        return this.server.port ?? 0;
    }

    /** The first callback's query; cancelling or running out of time closes the listener. */
    next(signal: AbortSignal, timeoutMs: number): Promise<URLSearchParams> {
        return new Promise((resolve, reject) => {
            const timer = setTimeout(() => finish(() => reject(new SignInError("timedOut"))), timeoutMs);
            const abort = () => finish(() => reject(signal.reason));
            const finish = (settle: () => void) => {
                clearTimeout(timer);
                signal.removeEventListener("abort", abort);
                this.waiting = undefined;
                this.close();
                settle();
            };
            if (signal.aborted)
                return abort();
            signal.addEventListener("abort", abort, { once: true });
            if (this.early)
                return finish(() => resolve(this.early!));
            this.waiting = (params) => finish(() => resolve(params));
        });
    }

    /** Stops listening at once; a callback page still being sent finishes. */
    close(): void {
        void this.server.stop();
    }

    private answer(request: Request): Response {
        const url = new URL(request.url);
        if (request.method !== "GET" || url.pathname !== this.path)
            return new Response("Not found", { status: 404 });
        if (this.waiting)
            this.waiting(url.searchParams);
        else
            this.early ??= url.searchParams;
        return new Response(kDonePage, { headers: { "content-type": "text/html; charset=utf-8" } });
    }
}
