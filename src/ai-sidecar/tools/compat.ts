import { readFileSync } from "node:fs";
import { parseArgs } from "node:util";
import { kProtocolVersion } from "../src/protocol";

const { values } = parseArgs({
    options: {
        runtime: { type: "string" },
        bundle: { type: "string" },
        app: { type: "string", multiple: true },
    },
});
const { runtime, bundle } = values;
const apps = values.app ?? [];
if (!runtime || !bundle || apps.length === 0) {
    console.error("usage: bun tools/compat.ts --runtime <hikari-ai-runtime> --bundle <hikari-ai.js> " +
        "--app <name>=<api.json>...");
    process.exit(64);
}

type Answer = { result?: { protocolVersion: number; requiredApiVersion: number }; error?: { message: string } };

async function initialize(reference: { version: number; nodes?: unknown }): Promise<Answer> {
    const { nodes: _, ...hikariApi } = reference;
    const child = Bun.spawn([runtime!, bundle!], { stdin: "pipe", stdout: "pipe", stderr: "inherit",
        env: { PATH: process.env.PATH ?? "" } });
    child.stdin.write(JSON.stringify({ jsonrpc: "2.0", id: 1, method: "initialize", params: {
        protocolVersion: kProtocolVersion, hikariApiVersion: reference.version, buildId: "compat", hikariApi } }) + "\n");
    child.stdin.flush();
    const reader = child.stdout.getReader();
    let text = "";
    while (!text.includes("\n")) {
        const { value, done } = await reader.read();
        if (done)
            break;
        text += new TextDecoder().decode(value);
    }
    child.stdin.end();
    await child.exited;
    return JSON.parse(text.slice(0, text.indexOf("\n")));
}

let broken = 0;
for (const app of apps) {
    const [name, path] = app.split("=", 2);
    const reference = JSON.parse(readFileSync(path!, "utf8"));
    const answer = await initialize(reference);
    if (answer.error) {
        console.error(`::error::${name} (HA1 ${reference.version}): initialize failed: ${answer.error.message}`);
        ++broken;
    } else if (answer.result!.requiredApiVersion > reference.version) {
        console.log(`${name} (HA1 ${reference.version}): not offered, it needs HA1 ${answer.result!.requiredApiVersion}`);
    } else {
        console.log(`${name} (HA1 ${reference.version}): runs`);
    }
}
process.exit(broken === 0 ? 0 : 1);
