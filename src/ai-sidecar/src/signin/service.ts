import { CodexSignIn, kCodex, type CodexEndpoints } from "./codex";
import { kOpenRouter, OpenRouterSignIn, type OpenRouterEndpoints } from "./openrouter";
import { InvalidParams } from "../providers/service";
import { kSystemDeps, type CodexSession, type Deps, type Flow, type Progress } from "./types";

export interface SignInParams {
    flow: Flow;
    /** The keychain entry Qt expects the result under. */
    secret: string;
}
export interface SignInResult {
    email?: string;
    expiresAt?: number;
}
export interface RefreshParams {
    secret: string;
    session: CodexSession;
}
export interface ProgressParams extends Progress {
    flow: Flow;
}

export interface SignInOptions {
    deps?: Partial<Deps>;
    codex?: CodexEndpoints;
    openrouter?: OpenRouterEndpoints;
}

export interface SignInPorts {
    progress(params: ProgressParams): void;
    /** Hands a token to Qt, which alone writes the keychain. */
    store(name: string, value: string): Promise<unknown>;
}

const kFlows: readonly Flow[] = ["codexDevice", "codexBrowser", "openrouter"];

export class SignInService {
    private readonly codex: CodexSignIn;
    private readonly openrouter: OpenRouterSignIn;

    constructor(private readonly ports: SignInPorts, options: SignInOptions = {}) {
        const deps = { ...kSystemDeps, ...options.deps };
        this.codex = new CodexSignIn(deps, options.codex ?? kCodex);
        this.openrouter = new OpenRouterSignIn(deps, options.openrouter ?? kOpenRouter);
    }

    async start(params: unknown, signal: AbortSignal): Promise<SignInResult> {
        const p = (params ?? {}) as Partial<SignInParams>;
        if (!p.flow || !kFlows.includes(p.flow) || typeof p.secret !== "string" || !p.secret)
            throw new InvalidParams("_hikari/signin.start needs a flow and a secret name");
        const flow = p.flow;
        const report = (progress: Progress) => this.ports.progress({ flow, ...progress });
        if (flow === "openrouter") {
            await this.ports.store(p.secret, await this.openrouter.key(signal, report));
            return {};
        }
        const session = flow === "codexDevice" ? await this.codex.device(signal, report) : await this.codex.browser(signal, report);
        await this.ports.store(p.secret, JSON.stringify(session));
        return { email: session.email, expiresAt: session.expiresAt };
    }

    async refresh(params: unknown, signal: AbortSignal): Promise<SignInResult> {
        const p = (params ?? {}) as Partial<RefreshParams>;
        if (typeof p.secret !== "string" || !p.secret || typeof p.session?.refresh !== "string" || !p.session.refresh)
            throw new InvalidParams("_hikari/signin.refresh needs a secret name and a session");
        const session = await this.codex.refresh(p.session, signal);
        await this.ports.store(p.secret, JSON.stringify(session));
        return { email: session.email, expiresAt: session.expiresAt };
    }
}
