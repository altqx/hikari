import { Loopback } from "./loopback";
import { challengeOf, claimsOf, randomToken } from "./pkce";
import { SignInError, type CodexSession, type Deps, type Progress } from "./types";

export interface CodexEndpoints {
    issuer: string;
    clientId: string;
    /** The browser sign-in's redirect port; Codex's client id only accepts 1455. */
    browserPort: number;
}

export const kCodex: CodexEndpoints = {
    issuer: "https://auth.openai.com",
    clientId: "app_EMoamEEZ73f0CkXaXp7hrann",
    browserPort: 1455,
};

export const kCodeLifetimeMs = 15 * 60_000;
export const kBrowserWaitMs = 10 * 60_000;
const kMinPollMs = 3_000;
const kTransientLimit = 6;

type Report = (progress: Progress) => void;

interface TokenReply {
    id_token?: string;
    access_token?: string;
    refresh_token?: string;
    expires_in?: number;
}

export class CodexSignIn {
    private readonly host: string;

    constructor(private readonly deps: Deps, private readonly endpoints: CodexEndpoints = kCodex) {
        this.host = new URL(endpoints.issuer).host;
    }

    async device(signal: AbortSignal, report: Report): Promise<CodexSession> {
        const started = this.deps.now();
        const code = await this.post("/api/accounts/deviceauth/usercode", { client_id: this.endpoints.clientId }, signal);
        if (code.status === 404)
            throw new SignInError("codesOff", this.host);
        const issued = (await this.json(code, "usercode")) as { device_auth_id?: string; user_code?: string; interval?: number | string };
        if (!issued.device_auth_id || !issued.user_code)
            throw new SignInError("invalid", this.host, "usercode reply without a code");
        const expiresAt = started + kCodeLifetimeMs;
        report({
            step: "code",
            userCode: issued.user_code,
            openUrl: `${this.endpoints.issuer}/codex/device`,
            expiresAt,
            browserAvailable: Loopback.isFree(this.endpoints.browserPort),
        });
        const intervalMs = Math.max(kMinPollMs, Number(issued.interval ?? 0) * 1000 || 0);
        let transients = 0;
        for (;;) {
            await this.deps.sleep(intervalMs, signal);
            if (this.deps.now() >= expiresAt)
                throw new SignInError("codeExpired", this.host);
            let reply: Response;
            try {
                reply = await this.post("/api/accounts/deviceauth/token",
                    { device_auth_id: issued.device_auth_id, user_code: issued.user_code }, signal);
            } catch (error) {
                if (error instanceof SignInError && error.kind === "network" && ++transients < kTransientLimit)
                    continue;
                throw error;
            }
            if (reply.status === 403 || reply.status === 404)
                continue;
            if (reply.status >= 500 && ++transients < kTransientLimit)
                continue;
            if (!reply.ok)
                throw await this.refusal(reply);
            const grant = (await reply.json()) as { authorization_code?: string; code_verifier?: string };
            if (!grant.authorization_code || !grant.code_verifier)
                throw new SignInError("invalid", this.host, "device grant without a code");
            return this.exchange({
                grant_type: "authorization_code",
                code: grant.authorization_code,
                redirect_uri: `${this.endpoints.issuer}/deviceauth/callback`,
                client_id: this.endpoints.clientId,
                code_verifier: grant.code_verifier,
            }, signal);
        }
    }

    async browser(signal: AbortSignal, report: Report): Promise<CodexSession> {
        const loopback = Loopback.open(this.endpoints.browserPort, "/auth/callback");
        try {
            const verifier = randomToken();
            const state = randomToken(16);
            const redirect = `http://localhost:${loopback.port}/auth/callback`;
            const authorize = new URL(`${this.endpoints.issuer}/oauth/authorize`);
            authorize.search = new URLSearchParams({
                response_type: "code",
                client_id: this.endpoints.clientId,
                redirect_uri: redirect,
                scope: "openid profile email offline_access",
                code_challenge: await challengeOf(verifier),
                code_challenge_method: "S256",
                id_token_add_organizations: "true",
                state,
            }).toString();
            report({ step: "browser", openUrl: authorize.toString() });
            const callback = await loopback.next(signal, kBrowserWaitMs);
            if (callback.get("error"))
                throw new SignInError("denied", this.host, callback.get("error") ?? "");
            if (callback.get("state") !== state)
                throw new SignInError("invalid", this.host, "state mismatch");
            const code = callback.get("code");
            if (!code)
                throw new SignInError("invalid", this.host, "callback without a code");
            return await this.exchange({
                grant_type: "authorization_code",
                code,
                redirect_uri: redirect,
                client_id: this.endpoints.clientId,
                code_verifier: verifier,
            }, signal);
        } finally {
            loopback.close();
        }
    }

    async refresh(session: CodexSession, signal: AbortSignal): Promise<CodexSession> {
        const renewed = await this.exchange({
            grant_type: "refresh_token",
            refresh_token: session.refresh,
            client_id: this.endpoints.clientId,
        }, signal, true);
        return { ...renewed, refresh: renewed.refresh || session.refresh, idToken: renewed.idToken || session.idToken,
            accountId: renewed.accountId || session.accountId, email: renewed.email || session.email };
    }

    private async exchange(form: Record<string, string>, signal: AbortSignal, refreshing = false): Promise<CodexSession> {
        let reply: Response;
        try {
            reply = await this.deps.fetch(`${this.endpoints.issuer}/oauth/token`, {
                method: "POST",
                headers: { "content-type": "application/x-www-form-urlencoded" },
                body: new URLSearchParams(form).toString(),
                signal,
            });
        } catch (error) {
            throw this.unreachable(error, signal);
        }
        if (!reply.ok) {
            const refusal = await this.refusal(reply);
            throw refreshing && reply.status >= 400 && reply.status < 500
                ? new SignInError("expired", this.host, refusal.detail) : refusal;
        }
        const tokens = (await reply.json()) as TokenReply;
        if (!tokens.access_token)
            throw new SignInError("invalid", this.host, "token reply without an access token");
        const idToken = tokens.id_token ?? "";
        const identity = claimsOf(idToken);
        const auth = (identity["https://api.openai.com/auth"] ?? {}) as { chatgpt_account_id?: string };
        const exp = Number(claimsOf(tokens.access_token).exp);
        return {
            access: tokens.access_token,
            refresh: tokens.refresh_token ?? "",
            idToken,
            accountId: auth.chatgpt_account_id ?? "",
            email: typeof identity.email === "string" ? identity.email : "",
            expiresAt: exp > 0 ? exp * 1000 : this.deps.now() + (tokens.expires_in ?? 3600) * 1000,
        };
    }

    /** A JSON POST; a 429 waits out its Retry-After before trying again. */
    private async post(path: string, body: unknown, signal: AbortSignal): Promise<Response> {
        for (;;) {
            let reply: Response;
            try {
                reply = await this.deps.fetch(`${this.endpoints.issuer}${path}`, {
                    method: "POST",
                    headers: { "content-type": "application/json" },
                    body: JSON.stringify(body),
                    signal,
                });
            } catch (error) {
                throw this.unreachable(error, signal);
            }
            if (reply.status !== 429)
                return reply;
            const seconds = Number(reply.headers.get("retry-after"));
            await this.deps.sleep((seconds > 0 ? Math.min(seconds, 60) : 5) * 1000, signal);
        }
    }

    private async json(reply: Response, what: string): Promise<unknown> {
        if (!reply.ok)
            throw await this.refusal(reply);
        try {
            return await reply.json();
        } catch {
            throw new SignInError("invalid", this.host, `${what} reply isn't JSON`);
        }
    }

    private async refusal(reply: Response): Promise<SignInError> {
        const text = await reply.text().catch(() => "");
        let code = "";
        try {
            const body = JSON.parse(text) as { error?: string | { code?: string } };
            code = typeof body.error === "string" ? body.error : body.error?.code ?? "";
        } catch {}
        if (reply.status >= 500)
            return new SignInError("network", this.host, `HTTP ${reply.status}`);
        if (code === "expired_token")
            return new SignInError("codeExpired", this.host, code);
        return new SignInError("denied", this.host, code || `HTTP ${reply.status}`);
    }

    private unreachable(error: unknown, signal: AbortSignal): unknown {
        return signal.aborted ? signal.reason : new SignInError("network", this.host, String((error as Error)?.message ?? error));
    }
}
