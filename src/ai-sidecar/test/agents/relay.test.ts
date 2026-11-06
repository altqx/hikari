import { expect, test } from "bun:test";

import { AgentRelay } from "../../src/agents/relay";
import { ErrorCode, Method } from "../../src/protocol";
import { Peer } from "../../src/rpc";
import { waitFor } from "../providers/mock";

type Message = Record<string, any>;

function link() {
    const sent: Message[] = [];
    const peer = new Peer((line) => sent.push(JSON.parse(line)), () => {});
    const feed = (message: unknown) => peer.feed(new TextEncoder().encode(JSON.stringify(message) + "\n"));
    const relay = new AgentRelay(peer);
    const find = (method: string, from = 0) => sent.slice(from).find((m) => m.method === method);
    const answer = (message: Message, result: unknown = {}) => feed({ jsonrpc: "2.0", id: message.id, result });
    const notify = (method: string, params: unknown) => feed({ jsonrpc: "2.0", method, params });
    return { sent, feed, relay, find, answer, notify };
}

const b64 = (text: string) => Buffer.from(text).toString("base64");

test("Qt gets the agent's name, arguments and set variables, and stdin goes over in order", async () => {
    const l = link();
    const child = l.relay.start("claude", { args: ["--tools="], env: { CLAUDE_CODE_ENTRYPOINT: "sdk-ts", UNSET: undefined } });
    const start = l.find(Method.agentStart)!;
    expect(start.params).toEqual({ process: child.id, agent: "claude", args: ["--tools="], env: { CLAUDE_CODE_ENTRYPOINT: "sdk-ts" } });
    l.answer(start);
    child.stdin.write("one\n");
    child.stdin.write("two\n");
    expect(await waitFor(() => l.find(Method.agentWrite) !== undefined)).toBe(true);
    const first = l.find(Method.agentWrite)!;
    expect(first.params).toEqual({ process: child.id, data: b64("one\n") });
    expect(l.sent.filter((m) => m.method === Method.agentWrite)).toHaveLength(1);
    l.answer(first);
    expect(await waitFor(() => l.sent.filter((m) => m.method === Method.agentWrite).length === 2)).toBe(true);
    const second = l.sent.filter((m) => m.method === Method.agentWrite)[1]!;
    expect(Buffer.from(second.params.data, "base64").toString()).toBe("two\n");
    l.answer(second);
    child.stdin.end();
    expect(await waitFor(() => l.find(Method.agentCloseInput) !== undefined)).toBe(true);
    expect(l.find(Method.agentCloseInput)!.params).toEqual({ process: child.id });
});

test("output reaches stdout and stderr, and the exit ends both with its code", async () => {
    const l = link();
    const child = l.relay.start("codex", { args: [], env: {} });
    l.answer(l.find(Method.agentStart)!);
    const out: string[] = [];
    const err: string[] = [];
    child.stdout.on("data", (d: Buffer) => out.push(d.toString()));
    child.stderr.on("data", (d: Buffer) => err.push(d.toString()));
    const exit = new Promise<[number | null, string | null]>((resolve) => child.once("exit", (code, signal) => resolve([code, signal])));
    const ended = new Promise((resolve) => child.stdout.on("end", resolve));
    l.notify(Method.agentOutput, { process: child.id, stream: "stdout", data: b64("{\"id\":1}\n") });
    l.notify(Method.agentOutput, { process: child.id, stream: "stderr", data: b64("warning\n") });
    l.notify(Method.agentOutput, { process: child.id + 1, stream: "stdout", data: b64("not ours\n") });
    l.notify(Method.agentExit, { process: child.id, code: 3 });
    expect(await exit).toEqual([3, null]);
    await ended;
    expect(out.join("")).toBe("{\"id\":1}\n");
    expect(err.join("")).toBe("warning\n");
    expect(child.exitCode).toBe(3);
    expect(child.kill("SIGTERM")).toBe(false);
});

test("kill and the SDK's abort ask Qt to kill it, and a killed agent reads as signalled", async () => {
    const l = link();
    const abort = new AbortController();
    const child = l.relay.start("claude", { args: [], env: {}, signal: abort.signal });
    abort.abort();
    expect(child.killed).toBe(true);
    const kill = l.find(Method.agentKill)!;
    expect(kill.params).toEqual({ process: child.id });
    const exit = new Promise<[number | null, string | null]>((resolve) => child.once("exit", (code, signal) => resolve([code, signal])));
    l.notify(Method.agentExit, { process: child.id, code: -1 });
    expect(await exit).toEqual([null, "SIGKILL"]);
    const before = l.sent.length;
    await new Promise<void>((resolve, reject) => child.stdin.write("late\n", (e) => (e ? reject(e) : resolve())));
    expect(l.sent.length).toBe(before);
});

test("an agent Qt refuses to start fails with Qt's reason", async () => {
    const l = link();
    const child = l.relay.start("bash", { args: [], env: {} });
    const failed = new Promise<Error>((resolve) => child.once("error", resolve));
    l.feed({ jsonrpc: "2.0", id: l.find(Method.agentStart)!.id,
        error: { code: ErrorCode.invalidParams, message: "Hikari doesn't know that agent" } });
    expect((await failed).message).toBe("Hikari doesn't know that agent");
    expect(child.kill("SIGTERM")).toBe(false);
});
