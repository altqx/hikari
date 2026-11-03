import { describe, expect, test } from "bun:test";
import { join } from "node:path";
import { bundledPackages, forbiddenInputs, kRoot } from "../tools/bundle";

describe("the release checks", () => {
    test("an input from the Claude Agent SDK is caught, wherever it is nested", () => {
        const meta = { inputs: {
            "src/main.ts": {},
            "node_modules/@anthropic-ai/sdk/index.mjs": {},
            "node_modules/@anthropic-ai/claude-agent-sdk/sdk.mjs": {},
            "node_modules/x/node_modules/@anthropic-ai/claude-agent-sdk/cli.js": {},
            "node_modules\\@anthropic-ai\\claude-agent-sdk\\win.js": {},
        } };
        expect(forbiddenInputs(meta)).toEqual([
            "node_modules/@anthropic-ai/claude-agent-sdk/sdk.mjs",
            "node_modules/x/node_modules/@anthropic-ai/claude-agent-sdk/cli.js",
            "node_modules\\@anthropic-ai\\claude-agent-sdk\\win.js",
        ]);
        expect(bundledPackages(meta)).toEqual(["@anthropic-ai/claude-agent-sdk", "@anthropic-ai/sdk"]);
    });

    test("NOTICES.txt matches what the bundle holds", () => {
        const check = Bun.spawnSync([process.execPath, join(kRoot, "tools", "notices.ts"), "--check"],
            { cwd: kRoot, stderr: "pipe" });
        expect(check.stderr.toString()).toBe("");
        expect(check.exitCode).toBe(0);
    }, 30_000);
});
