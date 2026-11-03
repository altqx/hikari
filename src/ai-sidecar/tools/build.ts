import { mkdirSync, readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { parseArgs } from "node:util";
import { bundledPackages, forbiddenInputs, kCompileFlags, kNoticesFile, kRoot, notices, readMetafile } from "./bundle";

const { values } = parseArgs({
    options: {
        target: { type: "string" },
        runtime: { type: "string" },
        out: { type: "string" },
        "build-id": { type: "string" },
        "api-version": { type: "string" },
    },
});
const { target, runtime, out } = values;
if (!target || !runtime || !out || !values["build-id"] || values["api-version"] === undefined) {
    console.error("usage: bun tools/build.ts --target <linux-x64|windows-x64> --runtime <bun> --out <file> " +
        "--build-id <id> --api-version <n>");
    process.exit(64);
}

mkdirSync(dirname(out), { recursive: true });
const metafile = `${out}.meta.json`;
const build = Bun.spawnSync([process.execPath, "build", join("src", "main.ts"), ...kCompileFlags,
    `--target=bun-${target}-baseline`,
    `--compile-executable-path=${runtime}`, `--define=HIKARI_BUILD_ID=${JSON.stringify(values["build-id"])}`,
    `--define=HIKARI_API_VERSION=${Number(values["api-version"])}`, `--metafile=${metafile}`, `--outfile=${out}`],
    { cwd: kRoot, stdio: ["ignore", "inherit", "inherit"] });
if (build.exitCode !== 0)
    process.exit(build.exitCode ?? 1);

const meta = readMetafile(metafile);
const forbidden = forbiddenInputs(meta);
if (forbidden.length > 0) {
    console.error(`hikari-ai must never bundle the Claude Agent SDK, but it holds:\n  ${forbidden.join("\n  ")}`);
    process.exit(1);
}
if (notices(bundledPackages(meta)) !== readFileSync(kNoticesFile, "utf8")) {
    console.error("NOTICES.txt doesn't match the bundle; run `bun tools/notices.ts` and commit it");
    process.exit(1);
}
