import { createHash } from "node:crypto";
import { existsSync, readdirSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { bunNotices, kMitText, section } from "../src/notices";

export const kRoot = join(import.meta.dir, "..");
/** The provider SDKs the bundle loads from hikari-ai-<name>.js beside it, on their first use. */
export const kSdkEntries = ["anthropic", "gemini", "openai"] as const;
export const kNoticesFile = join(kRoot, "NOTICES.txt");
const kNeverShipped = "@anthropic-ai/claude-agent-sdk";

/** Without the two autoload flags a `.env` or `bunfig.toml` in the working directory reaches the binary. */
export const kCompileFlags = ["--compile", "--bytecode", "--no-compile-autoload-dotenv", "--no-compile-autoload-bunfig"];

export interface Metafile {
    inputs: Record<string, unknown>;
}

export function readMetafile(path: string): Metafile {
    return JSON.parse(readFileSync(path, "utf8")) as Metafile;
}

function packageOf(input: string): string | undefined {
    const parts = input.split(/[\\/]/);
    const at = parts.lastIndexOf("node_modules");
    if (at < 0 || at + 1 >= parts.length)
        return undefined;
    const name = parts[at + 1]!;
    return name.startsWith("@") ? `${name}/${parts[at + 2]}` : name;
}

/** The npm packages the bundle holds, by name. */
export function bundledPackages(meta: Metafile): string[] {
    const names = new Set<string>();
    for (const input of Object.keys(meta.inputs)) {
        const name = packageOf(input);
        if (name)
            names.add(name);
    }
    return [...names].sort();
}

/** The inputs that come from a package HikariSub never ships. */
export function forbiddenInputs(meta: Metafile): string[] {
    return Object.keys(meta.inputs).filter((input) => packageOf(input) === kNeverShipped);
}

interface Manifest {
    version?: string;
    license?: string;
    author?: string | { name?: string };
}

/** The package's licence file, or for an MIT package that ships none, the MIT text credited to its author. */
function licenceText(dir: string, manifest: Manifest): string {
    const file = readdirSync(dir).find((name) => /^(licen[cs]e|copying|notice)(\.|$)/i.test(name));
    if (file)
        return readFileSync(join(dir, file), "utf8").trimEnd();
    const author = typeof manifest.author === "string" ? manifest.author : manifest.author?.name;
    if (manifest.license !== "MIT" || !author)
        throw new Error(`${dir} has no licence file`);
    return `Copyright (c) ${author}\n\n${kMitText.trimEnd()}`;
}

export function notices(packages: string[]): string {
    let text = "hikari-ai, the HikariSub AI sidecar: third-party notices\n\n" +
        "hikari-ai is HikariSub's own code (GPL-3.0-or-later), run by its own build of Bun. Its source and build\n" +
        "are in https://github.com/altqx/hikari under src/ai-sidecar, so it can be rebuilt and relinked.\n\n";
    text += bunNotices();
    for (const name of packages) {
        const dir = join(kRoot, "node_modules", name);
        if (!existsSync(dir))
            throw new Error(`${name} is bundled but not installed`);
        const manifest = JSON.parse(readFileSync(join(dir, "package.json"), "utf8")) as Manifest;
        text += section(`${name} ${manifest.version ?? ""} (${manifest.license ?? "see below"})`, licenceText(dir, manifest));
    }
    if (packages.length === 0)
        text += "No npm packages are bundled.\n";
    return text.replace(/\r\n?/g, "\n");
}

/** The files the runtime is built from, besides Bun itself. */
export const kRuntimeInputs = ["src/runtime.ts", "src/notices.ts", "licenses/bun.md", "licenses/mit.txt",
    "licenses/lgpl-2.0.txt", "licenses/lgpl-2.1.txt"];

/** The runtime's id: Bun's version and a digest of its other inputs, so a new runtime has a new id. */
export function runtimeId(bunVersion: string): string {
    const digest = createHash("sha256");
    for (const input of kRuntimeInputs)
        digest.update(input).update("\0").update(readFileSync(join(kRoot, input), "utf8").replace(/\r\n?/g, "\n")).update("\0");
    return `${bunVersion}-${digest.digest("hex").slice(0, 8)}`;
}
