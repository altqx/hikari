export type Flow = "codexDevice" | "codexBrowser" | "openrouter";

export type SignInErrorKind =
    | "denied"
    | "codeExpired"
    | "codesOff"
    | "portBusy"
    | "network"
    | "expired"
    | "timedOut"
    | "invalid";

export class SignInError extends Error {
    constructor(readonly kind: SignInErrorKind, readonly host = "", readonly detail = "") {
        super(detail || kind);
    }

    get data(): SignInErrorData {
        return { kind: this.kind, host: this.host, detail: this.detail };
    }
}

export interface SignInErrorData {
    kind: SignInErrorKind;
    host: string;
    detail: string;
}

/** What Qt shows while a sign-in waits for the user. */
export interface Progress {
    step: "code" | "browser";
    userCode?: string;
    /** The page the user opens; Qt opens it only on its allow-list. */
    openUrl?: string;
    /** Milliseconds since the epoch. */
    expiresAt?: number;
    /** False while another app holds the Codex browser sign-in's port. */
    browserAvailable?: boolean;
}

/** HikariSub's own ChatGPT sign-in for Codex, kept in the keychain as `codex-chatgpt`. */
export interface CodexSession {
    access: string;
    refresh: string;
    idToken: string;
    accountId: string;
    email: string;
    /** When the access token expires, in milliseconds since the epoch. */
    expiresAt: number;
}

export interface Deps {
    fetch(url: string, init: RequestInit): Promise<Response>;
    now(): number;
    sleep(ms: number, signal: AbortSignal): Promise<void>;
}

export function sleep(ms: number, signal: AbortSignal): Promise<void> {
    return new Promise((resolve, reject) => {
        if (signal.aborted)
            return reject(signal.reason);
        const timer = setTimeout(() => {
            signal.removeEventListener("abort", stop);
            resolve();
        }, ms);
        const stop = () => {
            clearTimeout(timer);
            reject(signal.reason);
        };
        signal.addEventListener("abort", stop, { once: true });
    });
}

export const kSystemDeps: Deps = { fetch: (url, init) => fetch(url, init), now: () => Date.now(), sleep };
