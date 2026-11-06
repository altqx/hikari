import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { bundledPackages, kNoticesFile, kRoot, notices, readMetafile } from "./bundle";

const check = process.argv.includes("--check");
const dir = mkdtempSync(join(tmpdir(), "hikari-ai-notices-"));
try {
    const build = Bun.spawnSync([process.execPath, "build", join("src", "single.ts"), "--target=bun",
        `--metafile=${join(dir, "meta.json")}`, `--outdir=${join(dir, "out")}`],
        { cwd: kRoot, stdio: ["ignore", "ignore", "inherit"] });
    if (build.exitCode !== 0)
        process.exit(build.exitCode ?? 1);
    const text = notices(bundledPackages(readMetafile(join(dir, "meta.json"))));
    if (!check)
        writeFileSync(kNoticesFile, text);
    else if (text !== readFileSync(kNoticesFile, "utf8")) {
        console.error("NOTICES.txt is out of date; run `bun tools/notices.ts` and commit it");
        process.exit(1);
    }
} finally {
    rmSync(dir, { recursive: true, force: true });
}
