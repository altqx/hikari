import { ErrorCode, Method, type HttpDataParams, type HttpRequestParams, type HttpResponseParams,
    type RelayTarget } from "../protocol";
import { type Peer, RpcError } from "../rpc";
import { errorDetail, kindOfStatus, ProviderError } from "./errors";
import type { ErrorData } from "./types";

/** What an SDK gets for a key; Qt puts the key there, or drops the header for a provider that needs none. */
export const kKeyPlaceholder = "hikari-key";

export interface RelayRequest {
    target: RelayTarget;
    method: string;
    url: string;
    headers: [string, string][];
    body?: Uint8Array;
}

export interface RelayResponse {
    status: number;
    headers: [string, string][];
    body: ReadableStream<Uint8Array>;
}

/** Sends a request out through Qt: the sidecar has no network of its own. */
export interface Relay {
    send(request: RelayRequest, signal: AbortSignal): Promise<RelayResponse>;
}

const kNoBody = new Set([101, 103, 204, 205, 304]);

/**
 * The SDKs' fetch: every request goes through `relay` to `target`. A response that isn't 2xx fails with the
 * provider's own message.
 */
export function relayFetch(relay: Relay, target: RelayTarget): typeof fetch {
    const relayed = async (input: string | URL | Request, init?: RequestInit): Promise<Response> => {
        const request = input instanceof Request ? new Request(input, init) : new Request(input.toString(), init);
        const signal = init?.signal ?? request.signal;
        const body = request.body ? new Uint8Array(await request.arrayBuffer()) : undefined;
        let response: RelayResponse;
        try {
            response = await relay.send({ target, method: request.method, url: request.url,
                headers: [...request.headers], body }, signal);
        } catch (error) {
            if (signal.aborted)
                throw signal.reason ?? error;
            throw error;
        }
        const out = new Response(kNoBody.has(response.status) || request.method === "HEAD" ? null : response.body,
            { status: response.status, headers: response.headers });
        if (response.status >= 300) {
            const text = await out.text().catch(() => "");
            throw new ProviderError(kindOfStatus(response.status), errorDetail(text), response.status);
        }
        return out;
    };
    return relayed as typeof fetch;
}

function relayError(error: unknown): ProviderError {
    if (error instanceof ProviderError)
        return error;
    if (error instanceof RpcError && error.code === ErrorCode.providerFailed) {
        const data = (error.data ?? {}) as Partial<ErrorData>;
        return new ProviderError(data.kind ?? "network", data.detail ?? "", data.httpStatus ?? 0);
    }
    if (error instanceof RpcError && error.code === ErrorCode.requestCancelled)
        return new ProviderError("cancelled");
    return new ProviderError("network", error instanceof Error ? error.message : String(error));
}

interface Stream {
    response(status: number, headers: [string, string][]): void;
    data(bytes: Uint8Array): void;
}

/** The relay over the link to Qt: `_hikari/http.request`, answered by streamed notifications. */
export class LinkRelay implements Relay {
    private nextStream = 1;
    private readonly streams = new Map<number, Stream>();

    constructor(private readonly peer: Peer) {
        peer.onNotification(Method.httpResponse, (params) => {
            const p = params as Partial<HttpResponseParams>;
            if (typeof p.stream === "number" && typeof p.status === "number" && Array.isArray(p.headers))
                this.streams.get(p.stream)?.response(p.status, p.headers);
        });
        peer.onNotification(Method.httpData, (params) => {
            const p = params as Partial<HttpDataParams>;
            if (typeof p.stream === "number" && typeof p.data === "string")
                this.streams.get(p.stream)?.data(Buffer.from(p.data, "base64"));
        });
    }

    send(request: RelayRequest, signal: AbortSignal): Promise<RelayResponse> {
        const stream = this.nextStream++;
        const cancel = new AbortController();
        const onAbort = () => cancel.abort(signal.reason);
        signal.addEventListener("abort", onAbort, { once: true });
        if (signal.aborted)
            onAbort();
        let body!: ReadableStreamDefaultController<Uint8Array>;
        let started = false;
        let dropped = false;
        return new Promise<RelayResponse>((resolve, reject) => {
            const readable = new ReadableStream<Uint8Array>({
                start: (controller) => {
                    body = controller;
                },
                cancel: () => {
                    dropped = true;
                    cancel.abort(new ProviderError("cancelled"));
                },
            });
            this.streams.set(stream, {
                response: (status, headers) => {
                    if (started)
                        return;
                    started = true;
                    resolve({ status, headers, body: readable });
                },
                data: (bytes) => {
                    if (started && !dropped)
                        body.enqueue(bytes);
                },
            });
            const params: HttpRequestParams = {
                stream, target: request.target, method: request.method, url: request.url, headers: request.headers,
                ...(request.body?.length ? { body: Buffer.from(request.body).toString("base64") } : {}),
            };
            this.peer.request(Method.httpRequest, params, cancel.signal).then(
                () => {
                    if (!started)
                        reject(new ProviderError("malformed", "the relay answered without a response"));
                    else if (!dropped)
                        body.close();
                },
                (error: unknown) => {
                    const e = relayError(error);
                    if (!started)
                        return reject(e);
                    if (!dropped)
                        body.error(e);
                },
            ).finally(() => {
                this.streams.delete(stream);
                signal.removeEventListener("abort", onAbort);
            });
        });
    }
}
