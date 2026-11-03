import type { ErrorData, ErrorKind } from "./types";

export class ProviderError extends Error {
    constructor(readonly kind: ErrorKind, readonly detail = "", readonly httpStatus = 0) {
        super(detail || kind);
    }

    get data(): ErrorData {
        return { kind: this.kind, httpStatus: this.httpStatus, detail: this.detail };
    }
}

export function kindOfStatus(status: number): ErrorKind {
    if (status === 401 || status === 403)
        return "unauthorized";
    if (status === 429)
        return "rateLimited";
    if (status >= 400 && status < 500)
        return "rejected";
    return "server";
}

/** The provider's own message from an error body: `error.message`, `error` or `message`. */
export function errorDetail(body: string): string {
    let parsed: unknown;
    try {
        parsed = JSON.parse(body);
    } catch {
        return "";
    }
    if (typeof parsed !== "object" || parsed === null)
        return "";
    const { error, message } = parsed as { error?: unknown; message?: unknown };
    if (typeof error === "object" && error !== null) {
        const inner = (error as { message?: unknown }).message;
        return typeof inner === "string" ? inner : "";
    }
    if (typeof error === "string")
        return error;
    return typeof message === "string" ? message : "";
}

/** A provider error from an error object inside a stream, keyed by its numeric `code`. */
export function streamError(error: unknown): ProviderError {
    const e = (error ?? {}) as { code?: unknown; message?: unknown };
    const code = typeof e.code === "number" ? e.code : 0;
    return new ProviderError(code ? kindOfStatus(code) : "server", typeof e.message === "string" ? e.message : "");
}

function causeOf(error: unknown): ProviderError | undefined {
    for (let e: unknown = error, depth = 0; e && depth < 8; e = (e as { cause?: unknown }).cause, ++depth)
        if (e instanceof ProviderError)
            return e;
    return undefined;
}

/** An SDK error that only wraps one the egress fetch threw. */
export function wrapsProviderError(error: unknown): boolean {
    return causeOf(error) !== undefined;
}

/** The ProviderError behind `error`: itself, a cause the SDK wrapped, or an abort's reason. */
export function providerErrorOf(error: unknown, signal?: AbortSignal): ProviderError {
    if (signal?.aborted && signal.reason instanceof ProviderError)
        return signal.reason;
    const cause = causeOf(error);
    if (cause)
        return cause;
    if (error instanceof SyntaxError)
        return new ProviderError("malformed", error.message);
    const message = error instanceof Error ? error.message : String(error);
    return new ProviderError("network", message);
}
