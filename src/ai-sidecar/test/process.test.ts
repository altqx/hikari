import { describe, expect, test } from "bun:test";
import { join } from "node:path";
import { kProtocolVersion } from "../src/protocol";

const initialize = JSON.stringify({ jsonrpc: "2.0", id: 1, method: "initialize",
    params: { protocolVersion: kProtocolVersion, hikariApiVersion: 0, buildId: "dev" } }) + "\n";

function start(entry: string) {
    return Bun.spawn([process.execPath, join(import.meta.dir, entry)], {
        stdin: "pipe", stdout: "pipe", stderr: "pipe", env: { PATH: process.env.PATH ?? "" },
    });
}

async function firstLine(stream: ReadableStream<Uint8Array>): Promise<string> {
    const reader = stream.getReader();
    let text = "";
    while (!text.includes("\n")) {
        const { value, done } = await reader.read();
        if (done)
            break;
        text += new TextDecoder().decode(value);
    }
    reader.releaseLock();
    return text;
}

describe("the sidecar process", () => {
    test("a dependency's console output lands on stderr, and stdout carries only the protocol", async () => {
        const child = start("fixtures/noisy-main.ts");
        child.stdin.write(initialize);
        child.stdin.end();
        const [stdout, stderr, code] = await Promise.all([new Response(child.stdout).text(),
            new Response(child.stderr).text(), child.exited]);
        expect(code).toBe(0);
        expect(stdout.trimEnd().split("\n").map((line) => JSON.parse(line).id)).toEqual([1]);
        expect(stderr).toContain("noisy-dependency: console.log");
        expect(stderr).toContain("noisy-dependency: console.info");
        expect(stderr).toContain("noisy-dependency: process.stdout.write");
    });

    test("stdin closing ends the process", async () => {
        const child = start("../src/main.ts");
        child.stdin.write(initialize);
        child.stdin.flush();
        expect(JSON.parse(await firstLine(child.stdout)).id).toBe(1);
        expect(child.exitCode).toBeNull();
        child.stdin.end();
        expect(await child.exited).toBe(0);
    });

    test("a malformed line ends the session with an error", async () => {
        const child = start("../src/main.ts");
        child.stdin.write("not json\n");
        child.stdin.flush();
        expect(await child.exited).toBe(2);
        expect(await new Response(child.stderr).text()).toContain("isn't JSON");
    });
});
