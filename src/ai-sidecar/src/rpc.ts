import { ErrorCode, kLimits, Method, type CancelRequestParams, type Limits } from "./protocol";

export class RpcError extends Error {
    constructor(readonly code: number, message: string, readonly data?: unknown) {
        super(message);
    }
}

export type RequestHandler = (params: unknown, signal: AbortSignal) => unknown | Promise<unknown>;
export type NotificationHandler = (params: unknown) => void;

type Id = number | string;

interface Incoming {
    controller: AbortController;
    bytes: number;
}

interface Outgoing {
    resolve: (result: unknown) => void;
    reject: (error: RpcError) => void;
    bytes: number;
}

const encoder = new TextEncoder();
const decoder = new TextDecoder("utf-8", { fatal: true });

function isObject(value: unknown): value is Record<string, unknown> {
    return typeof value === "object" && value !== null && !Array.isArray(value);
}

function isId(value: unknown): value is Id {
    return typeof value === "string" || (typeof value === "number" && Number.isInteger(value));
}

/**
 * One end of the JSON-RPC 2.0 link: one message per line, requests both ways. A malformed or oversized line,
 * or a peer that goes over the limits, calls `fail` and the session is over.
 */
export class Peer {
    private readonly requestHandlers = new Map<string, RequestHandler>();
    private readonly notificationHandlers = new Map<string, NotificationHandler>();
    private readonly incoming = new Map<Id, Incoming>();
    private incomingBytes = 0;
    private readonly outgoing = new Map<Id, Outgoing>();
    private outgoingBytes = 0;
    private nextId = 1;
    private buffered: Uint8Array[] = [];
    private bufferedBytes = 0;
    private failed = false;

    constructor(
        private readonly send: (line: string) => void,
        private readonly fail: (reason: string) => void,
        private readonly limits: Limits = kLimits,
    ) {
        this.onNotification(Method.cancelRequest, (params) => {
            const id = isObject(params) ? (params as Partial<CancelRequestParams>).requestId : undefined;
            if (isId(id))
                this.cancelIncoming(id);
        });
    }

    onRequest(method: string, handler: RequestHandler): void {
        this.requestHandlers.set(method, handler);
    }

    onNotification(method: string, handler: NotificationHandler): void {
        this.notificationHandlers.set(method, handler);
    }

    get outstandingIncoming(): number {
        return this.incoming.size;
    }

    get outstandingOutgoing(): number {
        return this.outgoing.size;
    }

    feed(chunk: Uint8Array): void {
        let start = 0;
        for (let i = 0; i < chunk.length && !this.failed; ++i) {
            if (chunk[i] !== 0x0a)
                continue;
            this.buffered.push(chunk.subarray(start, i));
            this.bufferedBytes += i - start;
            start = i + 1;
            const line = this.takeLine();
            if (line.length > 0)
                this.receive(line);
        }
        if (this.failed || start >= chunk.length)
            return;
        this.buffered.push(chunk.slice(start));
        this.bufferedBytes += chunk.length - start;
        if (this.bufferedBytes > this.limits.maxMessageBytes)
            this.die(`a message is over ${this.limits.maxMessageBytes} bytes`);
    }

    /** Aborting `signal` asks the peer to cancel; the answer still comes, usually as requestCancelled. */
    request(method: string, params?: unknown, signal?: AbortSignal): Promise<unknown> {
        if (signal?.aborted)
            return Promise.reject(new RpcError(ErrorCode.requestCancelled, "cancelled"));
        const id = this.nextId++;
        const line = JSON.stringify({ jsonrpc: "2.0", id, method, params });
        const bytes = encoder.encode(line).length;
        if (this.outgoing.size >= this.limits.maxOutstanding ||
            this.outgoingBytes + bytes > this.limits.maxOutstandingBytes || bytes > this.limits.maxMessageBytes)
            return Promise.reject(new RpcError(ErrorCode.busy, "too many requests are outstanding"));
        const onAbort = () => {
            if (this.outgoing.has(id))
                this.notify(Method.cancelRequest, { requestId: id } satisfies CancelRequestParams);
        };
        signal?.addEventListener("abort", onAbort, { once: true });
        const done = () => signal?.removeEventListener("abort", onAbort);
        return new Promise((resolve, reject) => {
            this.outgoing.set(id, {
                resolve: (result) => {
                    done();
                    resolve(result);
                },
                reject: (error) => {
                    done();
                    reject(error);
                },
                bytes,
            });
            this.outgoingBytes += bytes;
            this.send(line + "\n");
        });
    }

    notify(method: string, params?: unknown): void {
        this.send(JSON.stringify({ jsonrpc: "2.0", method, params }) + "\n");
    }

    /** Ends every outstanding request; outgoing ones reject with `error`. */
    close(error: RpcError): void {
        for (const entry of this.incoming.values())
            entry.controller.abort();
        this.incoming.clear();
        this.incomingBytes = 0;
        const outgoing = [...this.outgoing.values()];
        this.outgoing.clear();
        this.outgoingBytes = 0;
        for (const entry of outgoing)
            entry.reject(error);
    }

    private takeLine(): Uint8Array {
        const line = this.buffered.length === 1 ? this.buffered[0]! : Buffer.concat(this.buffered);
        this.buffered = [];
        this.bufferedBytes = 0;
        return line;
    }

    private receive(bytes: Uint8Array): void {
        if (bytes.length > this.limits.maxMessageBytes)
            return this.die(`a message is over ${this.limits.maxMessageBytes} bytes`);
        let message: unknown;
        try {
            message = JSON.parse(decoder.decode(bytes));
        } catch {
            return this.die("a line isn't JSON");
        }
        if (!isObject(message) || message.jsonrpc !== "2.0")
            return this.die("a line isn't a JSON-RPC 2.0 message");
        if (typeof message.method === "string") {
            if (message.id === undefined)
                return this.receiveNotification(message.method, message.params);
            if (!isId(message.id))
                return this.die("a request has an invalid id");
            return this.receiveRequest(message.id, message.method, message.params, bytes.length);
        }
        if (isId(message.id) && ("result" in message || "error" in message))
            return this.receiveResponse(message.id, message);
        this.die("a message is neither a request, a notification nor a response");
    }

    private receiveNotification(method: string, params: unknown): void {
        this.notificationHandlers.get(method)?.(params);
    }

    private receiveRequest(id: Id, method: string, params: unknown, bytes: number): void {
        if (this.incoming.has(id))
            return this.die(`request ${id} is already outstanding`);
        if (this.incoming.size >= this.limits.maxOutstanding ||
            this.incomingBytes + bytes > this.limits.maxOutstandingBytes)
            return this.die("the peer went over the outstanding-request limits");
        const handler = this.requestHandlers.get(method);
        if (!handler)
            return this.respondError(id, new RpcError(ErrorCode.methodNotFound, `unknown method ${method}`));
        const controller = new AbortController();
        this.incoming.set(id, { controller, bytes });
        this.incomingBytes += bytes;
        Promise.resolve()
            .then(() => handler(params, controller.signal))
            .then(
                (result) => this.finishIncoming(id, controller, { result: result ?? null }),
                (error: unknown) => this.finishIncoming(id, controller, { error: toWireError(error) }),
            );
    }

    private finishIncoming(id: Id, controller: AbortController, body: Record<string, unknown>): void {
        const entry = this.incoming.get(id);
        if (!entry || entry.controller !== controller)
            return;
        this.incoming.delete(id);
        this.incomingBytes -= entry.bytes;
        if (!this.failed)
            this.send(JSON.stringify({ jsonrpc: "2.0", id, ...body }) + "\n");
    }

    private cancelIncoming(id: Id): void {
        const entry = this.incoming.get(id);
        if (!entry)
            return;
        entry.controller.abort();
        this.finishIncoming(id, entry.controller, {
            error: { code: ErrorCode.requestCancelled, message: "cancelled" },
        });
    }

    private receiveResponse(id: Id, message: Record<string, unknown>): void {
        const entry = this.outgoing.get(id);
        if (!entry)
            return this.die(`a response answers unknown request ${id}`);
        this.outgoing.delete(id);
        this.outgoingBytes -= entry.bytes;
        if ("error" in message) {
            const error = isObject(message.error) ? message.error : {};
            entry.reject(new RpcError(typeof error.code === "number" ? error.code : ErrorCode.internalError,
                typeof error.message === "string" ? error.message : "", error.data));
        } else {
            entry.resolve(message.result);
        }
    }

    private respondError(id: Id, error: RpcError): void {
        this.send(JSON.stringify({ jsonrpc: "2.0", id, error: toWireError(error) }) + "\n");
    }

    private die(reason: string): void {
        if (this.failed)
            return;
        this.failed = true;
        this.buffered = [];
        this.bufferedBytes = 0;
        this.close(new RpcError(ErrorCode.internalError, reason));
        this.fail(reason);
    }
}

function toWireError(error: unknown): { code: number; message: string; data?: unknown } {
    if (error instanceof RpcError)
        return error.data === undefined ? { code: error.code, message: error.message }
            : { code: error.code, message: error.message, data: error.data };
    return { code: ErrorCode.internalError, message: error instanceof Error ? error.message : String(error) };
}
