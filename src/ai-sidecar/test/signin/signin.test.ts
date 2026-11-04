import { afterEach, beforeEach, expect, test } from "bun:test";

import { ErrorCode, kProtocolVersion, Method } from "../../src/protocol";
import { InvalidParams } from "../../src/providers/service";
import { Peer } from "../../src/rpc";
import { Sidecar } from "../../src/sidecar";
import { kCodeLifetimeMs } from "../../src/signin/codex";
import { challengeOf } from "../../src/signin/pkce";
import { SignInService, type ProgressParams } from "../../src/signin/service";
import { SignInError, type CodexSession } from "../../src/signin/types";
import { waitFor } from "../providers/mock";
import { FakeTime, freePort, Issuer, json, jwt } from "./issuer";

const kSecret = "codex-chatgpt";
const kUsercode = "/api/accounts/deviceauth/usercode";
const kPoll = "/api/accounts/deviceauth/token";
const kToken = "/oauth/token";

let issuer: Issuer;
let time: FakeTime;
let progress: ProgressParams[];
let stored: Map<string, string>;
let browserPort: number;
let held: ReturnType<typeof Bun.serve> | undefined;

beforeEach(() => {
    issuer = new Issuer();
    time = new FakeTime();
    progress = [];
    stored = new Map();
    browserPort = freePort();
    held = undefined;
});
afterEach(() => {
    issuer.stop();
    held?.stop(true);
});

function service(): SignInService {
    return new SignInService({
        progress: (p) => progress.push(p),
        store: async (name, value) => stored.set(name, value),
    }, {
        deps: time.deps(),
        codex: { issuer: issuer.base, clientId: "app_test", browserPort },
        openrouter: { site: issuer.base },
    });
}

const tokens = (overrides: Record<string, unknown> = {}) => json({
    id_token: jwt({ email: "owner@example.com", "https://api.openai.com/auth": { chatgpt_account_id: "acct-1" } }),
    access_token: jwt({ exp: 2_000_000 }),
    refresh_token: "refresh-1",
    ...overrides,
});

async function failure(work: Promise<unknown>): Promise<SignInError> {
    const error = await work.then(() => undefined, (e: unknown) => e);
    expect(error).toBeInstanceOf(SignInError);
    return error as SignInError;
}

const session = (): CodexSession => JSON.parse(stored.get(kSecret)!);

test("a device code sign-in shows the code, polls until it's entered and hands the session to Qt", async () => {
    issuer.on(kUsercode, json({ device_auth_id: "dev-1", user_code: "ABCD-1234", interval: "5" }))
        .on(kPoll, json({}, 403), json({}, 404), json({ authorization_code: "auth-1", code_verifier: "verifier-1" }))
        .on(kToken, tokens());
    const result = await service().start({ flow: "codexDevice", secret: kSecret }, new AbortController().signal);

    expect(issuer.at(kUsercode)[0]!.json()).toEqual({ client_id: "app_test" });
    expect(progress).toEqual([{
        flow: "codexDevice", step: "code", userCode: "ABCD-1234", openUrl: `${issuer.base}/codex/device`,
        expiresAt: 1_000_000 + kCodeLifetimeMs, browserAvailable: true,
    }]);
    expect(issuer.at(kPoll).map((s) => s.json())).toEqual(Array(3).fill({ device_auth_id: "dev-1", user_code: "ABCD-1234" }));
    expect(time.sleeps).toEqual([5000, 5000, 5000]);
    expect(Object.fromEntries(issuer.at(kToken)[0]!.form())).toEqual({
        grant_type: "authorization_code", code: "auth-1", redirect_uri: `${issuer.base}/deviceauth/callback`,
        client_id: "app_test", code_verifier: "verifier-1",
    });
    expect(session()).toMatchObject({ refresh: "refresh-1", accountId: "acct-1", email: "owner@example.com", expiresAt: 2_000_000_000 });
    expect(result).toEqual({ email: "owner@example.com", expiresAt: 2_000_000_000 });
});

test("polls never come faster than every 3 s", async () => {
    issuer.on(kUsercode, json({ device_auth_id: "d", user_code: "C", interval: 0 }))
        .on(kPoll, json({ authorization_code: "a", code_verifier: "v" }))
        .on(kToken, tokens());
    await service().start({ flow: "codexDevice", secret: kSecret }, new AbortController().signal);
    expect(time.sleeps).toEqual([3000]);
});

test("a 404 on the code request means the workspace turns codes off", async () => {
    issuer.on(kUsercode, json({}, 404));
    expect((await failure(service().start({ flow: "codexDevice", secret: kSecret }, new AbortController().signal))).data)
        .toEqual({ kind: "codesOff", host: new URL(issuer.base).host, detail: "" });
    expect(progress).toEqual([]);
});

test("a code nobody enters expires after 15 minutes", async () => {
    issuer.on(kUsercode, json({ device_auth_id: "d", user_code: "C", interval: 60 })).on(kPoll, json({}, 403));
    expect((await failure(service().start({ flow: "codexDevice", secret: kSecret }, new AbortController().signal))).kind)
        .toBe("codeExpired");
    expect(issuer.at(kPoll)).toHaveLength(kCodeLifetimeMs / 60_000 - 1);
    expect(stored.size).toBe(0);
});

test("a refused code and a refused exchange are denials", async () => {
    issuer.on(kUsercode, json({ device_auth_id: "d", user_code: "C" })).on(kPoll, json({ error: "access_denied" }, 400));
    expect((await failure(service().start({ flow: "codexDevice", secret: kSecret }, new AbortController().signal))).data)
        .toMatchObject({ kind: "denied", detail: "access_denied" });
    issuer.on(kPoll, json({ authorization_code: "a", code_verifier: "v" })).on(kToken, json({ error: "invalid_grant" }, 400));
    expect((await failure(service().start({ flow: "codexDevice", secret: kSecret }, new AbortController().signal))).kind)
        .toBe("denied");
    expect(stored.size).toBe(0);
});

test("a 429 waits out Retry-After, and a few server errors while polling are tolerated", async () => {
    issuer.on(kUsercode, json({}, 429, { "retry-after": "7" }), json({ device_auth_id: "d", user_code: "C" }))
        .on(kPoll, json({}, 502), json({}, 503), json({ authorization_code: "a", code_verifier: "v" }))
        .on(kToken, tokens());
    await service().start({ flow: "codexDevice", secret: kSecret }, new AbortController().signal);
    expect(time.sleeps).toEqual([7000, 3000, 3000, 3000]);
    expect(stored.has(kSecret)).toBe(true);
});

test("an issuer that can't be reached names its host", async () => {
    issuer.stop();
    const error = await failure(service().start({ flow: "codexDevice", secret: kSecret }, new AbortController().signal));
    expect(error.kind).toBe("network");
    expect(error.host).toBe(new URL(issuer.base).host);
});

test("cancelling stops the polling and stores nothing", async () => {
    const controller = new AbortController();
    issuer.on(kUsercode, json({ device_auth_id: "d", user_code: "C" }))
        .on(kPoll, () => {
            controller.abort(new Error("cancelled"));
            return json({}, 403);
        });
    const error = await service().start({ flow: "codexDevice", secret: kSecret }, controller.signal).catch((e: Error) => e);
    expect((error as Error).message).toBe("cancelled");
    expect(issuer.at(kPoll)).toHaveLength(1);
    expect(stored.size).toBe(0);
});

async function browserUrl(flow: ProgressParams["flow"]): Promise<URL> {
    expect(await waitFor(() => progress.some((p) => p.flow === flow))).toBe(true);
    return new URL(progress.find((p) => p.flow === flow)!.openUrl!);
}

test("a browser sign-in checks the state, then exchanges the code with its own verifier", async () => {
    issuer.on(kToken, tokens());
    const running = service().start({ flow: "codexBrowser", secret: kSecret }, new AbortController().signal);
    const authorize = await browserUrl("codexBrowser");
    const query = authorize.searchParams;
    expect(authorize.origin + authorize.pathname).toBe(`${issuer.base}/oauth/authorize`);
    expect(query.get("redirect_uri")).toBe(`http://localhost:${browserPort}/auth/callback`);
    expect(query.get("scope")).toBe("openid profile email offline_access");
    expect(query.get("code_challenge_method")).toBe("S256");

    const callback = `http://127.0.0.1:${browserPort}/auth/callback`;
    expect((await fetch(`${callback}x`)).status).toBe(404);
    const page = await fetch(`${callback}?code=auth-2&state=${query.get("state")}`);
    expect(await page.text()).toContain("You can close this tab");
    await running;

    const form = issuer.at(kToken)[0]!.form();
    expect(form.get("code")).toBe("auth-2");
    expect(await challengeOf(form.get("code_verifier")!)).toBe(query.get("code_challenge")!);
    expect(session().email).toBe("owner@example.com");
    expect((await fetch(callback).catch(() => undefined))?.status).toBeUndefined();
});

test("a callback with the wrong state is refused and nothing is stored", async () => {
    const refused = failure(service().start({ flow: "codexBrowser", secret: kSecret }, new AbortController().signal));
    await browserUrl("codexBrowser");
    await fetch(`http://127.0.0.1:${browserPort}/auth/callback?code=c&state=forged`);
    expect((await refused).kind).toBe("invalid");
    expect(issuer.at(kToken)).toHaveLength(0);
});

test("a denied consent page comes back as a denial", async () => {
    const refused = failure(service().start({ flow: "codexBrowser", secret: kSecret }, new AbortController().signal));
    await browserUrl("codexBrowser");
    await fetch(`http://127.0.0.1:${browserPort}/auth/callback?error=access_denied`);
    expect((await refused).kind).toBe("denied");
});

test("another app on port 1455 makes the browser sign-in unavailable", async () => {
    held = Bun.serve({ hostname: "127.0.0.1", port: browserPort, fetch: () => new Response() });
    expect((await failure(service().start({ flow: "codexBrowser", secret: kSecret }, new AbortController().signal))).kind)
        .toBe("portBusy");
    issuer.on(kUsercode, json({ device_auth_id: "d", user_code: "C" }))
        .on(kPoll, json({ authorization_code: "a", code_verifier: "v" })).on(kToken, tokens());
    await service().start({ flow: "codexDevice", secret: kSecret }, new AbortController().signal);
    expect(progress[0]!.browserAvailable).toBe(false);
});

test("cancelling a browser sign-in closes the listener", async () => {
    const controller = new AbortController();
    const running = service().start({ flow: "codexBrowser", secret: kSecret }, controller.signal);
    await browserUrl("codexBrowser");
    controller.abort(new Error("cancelled"));
    expect(((await running.catch((e: Error) => e)) as Error).message).toBe("cancelled");
    expect((await fetch(`http://127.0.0.1:${browserPort}/auth/callback`).catch(() => undefined))?.status).toBeUndefined();
});

test("an OpenRouter sign-in listens on a random path and trades the code for a key", async () => {
    issuer.on("/api/v1/auth/keys", json({ key: "sk-or-v1-test" }));
    const running = service().start({ flow: "openrouter", secret: "openrouter" }, new AbortController().signal);
    const auth = await browserUrl("openrouter");
    expect(auth.origin + auth.pathname).toBe(`${issuer.base}/auth`);
    const callback = new URL(auth.searchParams.get("callback_url")!);
    expect(callback.hostname).toBe("127.0.0.1");
    expect(callback.pathname.length).toBeGreaterThan(16);
    expect((await fetch(`http://127.0.0.1:${callback.port}/?code=guess`)).status).toBe(404);
    await fetch(`${callback}?code=or-code`);
    expect(await running).toEqual({});

    const body = issuer.at("/api/v1/auth/keys")[0]!.json();
    expect(body.code).toBe("or-code");
    expect(body.code_challenge_method).toBe("S256");
    expect(await challengeOf(body.code_verifier)).toBe(auth.searchParams.get("code_challenge")!);
    expect(stored.get("openrouter")).toBe("sk-or-v1-test");
});

test("OpenRouter refusing the code is a denial", async () => {
    issuer.on("/api/v1/auth/keys", json({ error: "bad code" }, 403));
    const refused = failure(service().start({ flow: "openrouter", secret: "openrouter" }, new AbortController().signal));
    const callback = (await browserUrl("openrouter")).searchParams.get("callback_url")!;
    await fetch(`${callback}?code=or-code`);
    expect((await refused).kind).toBe("denied");
    expect(stored.size).toBe(0);
});

const kOld: CodexSession = {
    access: "old", refresh: "refresh-0", idToken: "id-0", accountId: "acct-0", email: "old@example.com", expiresAt: 1,
};

test("a refresh keeps the refresh token the issuer didn't rotate", async () => {
    issuer.on(kToken, json({ access_token: jwt({ exp: 3_000_000 }) }));
    await service().refresh({ secret: kSecret, session: kOld }, new AbortController().signal);
    expect(Object.fromEntries(issuer.at(kToken)[0]!.form()))
        .toEqual({ grant_type: "refresh_token", refresh_token: "refresh-0", client_id: "app_test" });
    expect(session()).toEqual({ ...kOld, access: jwt({ exp: 3_000_000 }), expiresAt: 3_000_000_000 });
});

test("a refused refresh means the sign-in expired", async () => {
    issuer.on(kToken, json({ error: { code: "refresh_token_reused" } }, 401));
    expect((await failure(service().refresh({ secret: kSecret, session: kOld }, new AbortController().signal))).kind)
        .toBe("expired");
    expect(stored.size).toBe(0);
});

test("a sign-in without a flow or secret name is invalid params", async () => {
    for (const params of [{}, { flow: "codexDevice" }, { flow: "elsewhere", secret: "x" }, { flow: "codexDevice", secret: "" }])
        expect(service().start(params, new AbortController().signal)).rejects.toBeInstanceOf(InvalidParams);
    expect(service().refresh({ secret: "x", session: {} }, new AbortController().signal)).rejects.toBeInstanceOf(InvalidParams);
});

test("over JSON-RPC, progress is a notification, the session goes to Qt's secret.store and failures carry their kind", async () => {
    type Message = Record<string, any>;
    const sent: Message[] = [];
    const peer = new Peer((line) => sent.push(JSON.parse(line)), () => {});
    const feed = (message: unknown) => peer.feed(new TextEncoder().encode(JSON.stringify(message) + "\n"));
    new Sidecar(peer, { buildId: "test", hikariApiVersion: 1 }, {}, {
        deps: time.deps(), codex: { issuer: issuer.base, clientId: "app_test", browserPort },
    });
    issuer.on(kUsercode, json({ device_auth_id: "d", user_code: "WXYZ" }))
        .on(kPoll, json({ authorization_code: "a", code_verifier: "v" })).on(kToken, tokens());
    feed({ jsonrpc: "2.0", id: 0, method: Method.initialize, params: { protocolVersion: kProtocolVersion, hikariApiVersion: 1, buildId: "t" } });
    feed({ jsonrpc: "2.0", id: 1, method: Method.signinStart, params: { flow: "codexDevice", secret: kSecret } });

    expect(await waitFor(() => sent.some((m) => m.method === Method.secretStore))).toBe(true);
    const store = sent.find((m) => m.method === Method.secretStore)!;
    expect(store.params.name).toBe(kSecret);
    expect(JSON.parse(store.params.value).accountId).toBe("acct-1");
    expect(sent.find((m) => m.method === Method.signinProgress)!.params).toMatchObject({ flow: "codexDevice", userCode: "WXYZ" });
    feed({ jsonrpc: "2.0", id: store.id, result: null });
    expect(await waitFor(() => sent.some((m) => m.id === 1 && !("method" in m)))).toBe(true);
    expect(sent.find((m) => m.id === 1 && !("method" in m))!.result).toEqual({ email: "owner@example.com", expiresAt: 2_000_000_000 });

    issuer.on(kUsercode, json({}, 404));
    feed({ jsonrpc: "2.0", id: 2, method: Method.signinStart, params: { flow: "codexDevice", secret: kSecret } });
    expect(await waitFor(() => sent.some((m) => m.id === 2))).toBe(true);
    expect(sent.find((m) => m.id === 2)!.error).toEqual({
        code: ErrorCode.signInFailed, message: "codesOff", data: { kind: "codesOff", host: new URL(issuer.base).host, detail: "" },
    });
});
