import { afterAll, beforeAll, expect, test } from "bun:test";
import { mkdtempSync, readdirSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { ErrorCode, kProtocolVersion, Method } from "../src/protocol";
import { kRoot } from "../tools/bundle";
import { RelayPlayer } from "./providers/harness";
import { MockProvider, waitFor } from "./providers/mock";
import { removeTempDir } from "./temp_dir";

const dir = mkdtempSync(join(tmpdir(), "hikari-ai-bundle-"));
const exe = process.platform === "win32" ? ".exe" : "";
let mock: MockProvider;

beforeAll(() => {
    mock = new MockProvider();
    const build = Bun.spawnSync([process.execPath, join(kRoot, "tools", "build.ts"),
        "--target", process.platform === "win32" ? "windows-x64" : "linux-x64", "--runtime", process.execPath,
        "--out", dir, "--version", "0.0.0-test", "--build-id", "test", "--api-version", "1"],
        { cwd: kRoot, stdio: ["ignore", "ignore", "inherit"] });
    expect(build.exitCode).toBe(0);
}, 120_000);
afterAll(() => {
    mock.stop();
    removeTempDir(dir);
});

test("the bundle holds no provider SDK and loads each from its own file on first use", async () => {
    expect(readdirSync(dir).filter((name) => name.endsWith(".js")).sort())
        .toEqual(["hikari-ai-anthropic.js", "hikari-ai-gemini.js", "hikari-ai-openai.js", "hikari-ai.js"]);
    expect(await Bun.file(join(dir, "hikari-ai.js")).text()).not.toContain("class Anthropic");

    const child = Bun.spawn([join(dir, `hikari-ai-runtime${exe}`), join(dir, "hikari-ai.js")],
        { stdin: "pipe", stdout: "pipe", stderr: "inherit", env: { PATH: process.env.PATH ?? "" } });
    const sent: Record<string, any>[] = [];
    const feed = (message: unknown) => {
        child.stdin.write(JSON.stringify(message) + "\n");
        child.stdin.flush();
    };
    const player = new RelayPlayer(feed);
    (async () => {
        let text = "";
        for await (const chunk of child.stdout) {
            text += new TextDecoder().decode(chunk);
            for (let end = text.indexOf("\n"); end >= 0; end = text.indexOf("\n")) {
                const message = JSON.parse(text.slice(0, end));
                sent.push(message);
                player.take(message);
                text = text.slice(end + 1);
            }
        }
    })();
    const response = (id: number) => sent.find((m) => m.id === id && !m.method);
    const wires = { openai: "chat-completions", anthropic: "anthropic-messages", gemini: "gemini" } as const;
    feed({ jsonrpc: "2.0", id: 0, method: Method.initialize,
        params: { protocolVersion: kProtocolVersion, hikariApiVersion: 1, buildId: "test" } });
    feed({ jsonrpc: "2.0", id: 1, method: Method.providersSet, params: { providers: Object.entries(wires).map(
        ([id, wire]) => ({ id, preset: id, wire, baseUrl: mock.base })) } });
    for (let i = 0; i < 3; ++i)
        mock.replies.push({ status: 401, contentType: "application/json", chunks: ['{"error":{"message":"no"}}'] });
    Object.keys(wires).forEach((providerId, i) =>
        feed({ jsonrpc: "2.0", id: 2 + i, method: Method.modelsList, params: { providerId } }));
    expect(await waitFor(() => [2, 3, 4].every((id) => response(id)), 20_000)).toBe(true);
    child.stdin.end();
    await child.exited;
    for (const id of [2, 3, 4])
        expect(response(id)!.error).toMatchObject({ code: ErrorCode.providerFailed,
            data: { httpStatus: 401 } });
    expect(mock.received).toHaveLength(3);
}, 60_000);
