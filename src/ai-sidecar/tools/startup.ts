import { parseArgs } from "node:util";
import { kProtocolVersion } from "../src/protocol";

const { values } = parseArgs({
    options: {
        runtime: { type: "string" },
        bundle: { type: "string" },
        single: { type: "string" },
        runs: { type: "string", default: "20" },
    },
});
const { runtime, bundle, single } = values;
if (!runtime || !bundle || !single) {
    console.error("usage: bun tools/startup.ts --runtime <hikari-ai-runtime> --bundle <hikari-ai.js> " +
        "--single <hikari-ai-single> [--runs n]");
    process.exit(64);
}
const runs = Number(values.runs);
const initialize = JSON.stringify({ jsonrpc: "2.0", id: 1, method: "initialize",
    params: { protocolVersion: kProtocolVersion, hikariApiVersion: 1, buildId: "startup" } }) + "\n";

async function msToInitialize(command: string[]): Promise<number> {
    const began = performance.now();
    const child = Bun.spawn(command, { stdin: "pipe", stdout: "pipe", stderr: "inherit", env: { PATH: process.env.PATH ?? "" } });
    child.stdin.write(initialize);
    child.stdin.flush();
    const reader = child.stdout.getReader();
    let text = "";
    while (!text.includes("\n")) {
        const { value, done } = await reader.read();
        if (done)
            break;
        text += new TextDecoder().decode(value);
    }
    const elapsed = performance.now() - began;
    child.stdin.end();
    await child.exited;
    if (JSON.parse(text).id !== 1)
        throw new Error(`${command.join(" ")} didn't answer initialize: ${text}`);
    return elapsed;
}

async function median(command: string[]): Promise<number> {
    await msToInitialize(command);
    const times: number[] = [];
    for (let i = 0; i < runs; ++i)
        times.push(await msToInitialize(command));
    times.sort((a, b) => a - b);
    return times[Math.floor(runs / 2)]!;
}

const whole = await median([single]);
const split = await median([runtime, bundle]);
const limit = whole * 1.1 + 20;
console.log(`single file ${whole.toFixed(1)} ms, runtime and bundle ${split.toFixed(1)} ms, limit ${limit.toFixed(1)} ms`);
if (split > limit) {
    console.error("the split sidecar starts slower than the single file allows");
    process.exit(1);
}
