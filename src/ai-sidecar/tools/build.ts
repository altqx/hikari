import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { parseArgs } from "node:util";
import { kProtocolVersion, kRequiredApiVersion } from "../src/protocol";
import { bundledPackages, forbiddenInputs, kCompileFlags, kNoticesFile, kRoot, notices, readMetafile,
    kSdkEntries, runtimeId } from "./bundle";

const { values } = parseArgs({
    options: {
        target: { type: "string" },
        runtime: { type: "string" },
        out: { type: "string" },
        version: { type: "string" },
        "build-id": { type: "string" },
        "api-version": { type: "string" },
        single: { type: "boolean" },
    },
});
const { target, runtime, out, version } = values;
if (!target || !runtime || !out || !version || !values["build-id"] || values["api-version"] === undefined) {
    console.error("usage: bun tools/build.ts --target <linux-x64|windows-x64> --runtime <bun> --out <dir> " +
        "--version <ai version> --build-id <id> --api-version <n> [--single]");
    process.exit(64);
}

const exe = target.startsWith("windows") ? ".exe" : "";
const bunVersion = Bun.version;
const packages: Record<string, string> =
    JSON.parse(readFileSync(join(kRoot, "package.json"), "utf8")).dependencies ?? {};
const defines = [`--define=HIKARI_BUILD_ID=${JSON.stringify(values["build-id"])}`,
    `--define=HIKARI_API_VERSION=${Number(values["api-version"])}`,
    `--define=HIKARI_AI_VERSION=${JSON.stringify(version)}`,
    `--define=HIKARI_PACKAGES=${JSON.stringify(packages)}`];
const compile = [...kCompileFlags, `--target=bun-${target}-baseline`, `--compile-executable-path=${runtime}`];

function bun(args: string[]): void {
    const run = Bun.spawnSync([process.execPath, "build", ...args], { cwd: kRoot, stdio: ["ignore", "inherit", "inherit"] });
    if (run.exitCode !== 0)
        process.exit(run.exitCode ?? 1);
}

mkdirSync(out, { recursive: true });
bun([join("src", "runtime.ts"), ...compile, `--outfile=${join(out, `hikari-ai-runtime${exe}`)}`]);

const metafile = join(out, "hikari-ai.meta.json");
const sdkMetafile = join(out, "hikari-ai-sdks.meta.json");
const bundle = ["--target=bun", "--format=cjs", "--bytecode", `--outdir=${out}`];
bun([join("src", "main.ts"), ...bundle, ...defines, `--metafile=${metafile}`, "--entry-naming=hikari-ai.[ext]"]);
bun([...kSdkEntries.map((name) => join("src", "sdk", `${name}.ts`)), ...bundle, `--metafile=${sdkMetafile}`,
    "--entry-naming=hikari-ai-[name].[ext]"]);
const meta = { inputs: { ...readMetafile(metafile).inputs, ...readMetafile(sdkMetafile).inputs } };
const forbidden = forbiddenInputs(meta);
if (forbidden.length > 0) {
    console.error(`hikari-ai must never bundle the Claude Agent SDK, but it holds:\n  ${forbidden.join("\n  ")}`);
    process.exit(1);
}
if (notices(bundledPackages(meta)) !== readFileSync(kNoticesFile, "utf8")) {
    console.error("NOTICES.txt doesn't match the bundle; run `bun tools/notices.ts` and commit it");
    process.exit(1);
}

if (values.single)
    bun([join("src", "single.ts"), ...compile, ...defines, `--outfile=${join(out, `hikari-ai-single${exe}`)}`]);

writeFileSync(join(out, "build.json"), JSON.stringify({
    version,
    protocol: kProtocolVersion,
    requiredApiVersion: kRequiredApiVersion,
    hikariApiVersion: Number(values["api-version"]),
    runtime: runtimeId(bunVersion),
    bun: bunVersion,
    packages,
}, null, 2) + "\n");
