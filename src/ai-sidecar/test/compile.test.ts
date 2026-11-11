import { afterAll, describe, expect, test } from "bun:test";
import { mkdirSync, mkdtempSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { kCompileFlags, kRoot } from "../tools/bundle";
import { removeTempDir } from "./temp_dir";

const dir = mkdtempSync(join(tmpdir(), "hikari-ai-compile-"));
afterAll(() => removeTempDir(dir));

const probe = join(dir, "probe.ts");
writeFileSync(probe, `process.stdout.write(JSON.stringify({
    dotenv: process.env.HIKARI_DOTENV_PROBE ?? null,
    bunfig: (globalThis as { hikariPreloaded?: boolean }).hikariPreloaded ?? null,
}));\n`);
const work = join(dir, "work");
mkdirSync(work);
writeFileSync(join(work, ".env"), "HIKARI_DOTENV_PROBE=leaked\n");
writeFileSync(join(work, "preload.ts"), "(globalThis as { hikariPreloaded?: boolean }).hikariPreloaded = true;\n");
writeFileSync(join(work, "bunfig.toml"), 'preload = ["./preload.ts"]\n');

function compileAndRun(flags: string[]): unknown {
    const out = join(dir, `probe-${flags.length}${process.platform === "win32" ? ".exe" : ""}`);
    const build = Bun.spawnSync([process.execPath, "build", probe, ...flags, `--outfile=${out}`],
        { stdio: ["ignore", "ignore", "inherit"] });
    expect(build.exitCode).toBe(0);
    const run = Bun.spawnSync([out], { cwd: work, env: { PATH: process.env.PATH ?? "" } });
    return JSON.parse(run.stdout.toString());
}

describe("the compiled sidecar", () => {
    test("reads no .env or bunfig.toml from its working directory", () => {
        expect(compileAndRun(kCompileFlags)).toEqual({ dotenv: null, bunfig: null });
    }, 60_000);

    test("would read the .env without the autoload flags, so the check above means something", () => {
        expect(compileAndRun(["--compile"])).toMatchObject({ dotenv: "leaked" });
    }, 60_000);

    test("the runtime reads neither when it runs a bundle", () => {
        const runtime = join(dir, `runtime${process.platform === "win32" ? ".exe" : ""}`);
        const build = Bun.spawnSync([process.execPath, "build", join(kRoot, "src", "runtime.ts"), ...kCompileFlags,
            `--outfile=${runtime}`], { stdio: ["ignore", "ignore", "inherit"] });
        expect(build.exitCode).toBe(0);
        const bundle = join(dir, "bundle", "probe.js");
        const bundled = Bun.spawnSync([process.execPath, "build", probe, "--target=bun", "--format=cjs", "--bytecode",
            `--outdir=${join(dir, "bundle")}`], { stdio: ["ignore", "ignore", "inherit"] });
        expect(bundled.exitCode).toBe(0);
        const run = Bun.spawnSync([runtime, bundle], { cwd: work, env: { PATH: process.env.PATH ?? "" } });
        expect(JSON.parse(run.stdout.toString())).toEqual({ dotenv: null, bunfig: null });
    }, 60_000);
});
