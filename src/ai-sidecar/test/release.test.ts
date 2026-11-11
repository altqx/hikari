import { describe, expect, test } from "bun:test";
import { join } from "node:path";
import lock from "../bun.lock";
import { bundledPackages, forbiddenInputs, kRoot, notices } from "../tools/bundle";

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

    test("the notices end lines with LF even where a package's licence uses CRLF", () => {
        const text = notices(["@protobufjs/base64"]);
        expect(text).toContain("@protobufjs/base64");
        expect(text).not.toContain("\r");
    });

    test("only the Claude Agent SDK has optional packages, which `bun ci --omit=optional` leaves out", () => {
        const optional = Object.entries(lock.packages).filter(([, entry]) => {
            const meta = entry[2] as { optionalDependencies?: object; os?: unknown } | undefined;
            return meta?.optionalDependencies !== undefined || meta?.os !== undefined;
        }).map(([name]) => name);
        expect(optional.length).toBeGreaterThan(0);
        expect(optional.filter((name) => !name.startsWith("@anthropic-ai/claude-agent-sdk"))).toEqual([]);
    });

    test("NOTICES.txt matches what the bundle holds", () => {
        const check = Bun.spawnSync([process.execPath, join(kRoot, "tools", "notices.ts"), "--check"],
            { cwd: kRoot, stderr: "pipe" });
        expect(check.stderr.toString()).toBe("");
        expect(check.exitCode).toBe(0);
    }, 30_000);
});
