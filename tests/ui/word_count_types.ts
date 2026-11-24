import { existsSync } from "node:fs";

const [tsc, project, typeRoots] = Bun.argv.slice(2);
if (!existsSync(tsc)) {
    console.log(`The AI sidecar's TypeScript isn't installed at ${tsc}; skipped`);
    process.exit(77);
}
const checked = Bun.spawnSync([process.execPath, tsc, "-p", project, "--typeRoots", typeRoots], {
    stdout: "inherit",
    stderr: "inherit",
});
process.exit(checked.exitCode ?? 1);
