import { kBrowserWaitMs } from "./codex";
import { Loopback } from "./loopback";
import { challengeOf, randomToken } from "./pkce";
import { SignInError, type Deps, type Progress } from "./types";

export interface OpenRouterEndpoints {
    site: string;
}

export const kOpenRouter: OpenRouterEndpoints = { site: "https://openrouter.ai" };

export class OpenRouterSignIn {
    private readonly host: string;

    constructor(private readonly deps: Deps, private readonly endpoints: OpenRouterEndpoints = kOpenRouter) {
        this.host = new URL(endpoints.site).host;
    }

    /** A new OpenRouter key. OpenRouter echoes no state, so a random path stands in for it. */
    async key(signal: AbortSignal, report: (progress: Progress) => void): Promise<string> {
        const loopback = Loopback.open(0, `/${randomToken(16)}`);
        try {
            const verifier = randomToken();
            const auth = new URL(`${this.endpoints.site}/auth`);
            auth.search = new URLSearchParams({
                callback_url: `http://127.0.0.1:${loopback.port}${loopback.path}`,
                code_challenge: await challengeOf(verifier),
                code_challenge_method: "S256",
            }).toString();
            report({ step: "browser", openUrl: auth.toString() });
            const code = (await loopback.next(signal, kBrowserWaitMs)).get("code");
            if (!code)
                throw new SignInError("denied", this.host, "callback without a code");
            let reply: Response;
            try {
                reply = await this.deps.fetch(`${this.endpoints.site}/api/v1/auth/keys`, {
                    method: "POST",
                    headers: { "content-type": "application/json" },
                    body: JSON.stringify({ code, code_verifier: verifier, code_challenge_method: "S256" }),
                    signal,
                });
            } catch (error) {
                throw signal.aborted ? signal.reason : new SignInError("network", this.host, String((error as Error)?.message ?? error));
            }
            if (reply.status >= 500)
                throw new SignInError("network", this.host, `HTTP ${reply.status}`);
            if (!reply.ok)
                throw new SignInError("denied", this.host, `HTTP ${reply.status}`);
            const { key } = (await reply.json()) as { key?: string };
            if (!key)
                throw new SignInError("invalid", this.host, "keys reply without a key");
            return key;
        } finally {
            loopback.close();
        }
    }
}
