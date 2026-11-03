import { existsSync, readdirSync, readFileSync } from "node:fs";
import { join } from "node:path";

export const kRoot = join(import.meta.dir, "..");
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
    return `Copyright (c) ${author}\n\n${readFileSync(join(kRoot, "licenses", "mit.txt"), "utf8").trimEnd()}`;
}

function section(title: string, body: string): string {
    return `${"=".repeat(78)}\n${title}\n${"=".repeat(78)}\n\n${body.trimEnd()}\n\n`;
}

export function notices(packages: string[]): string {
    const licence = (name: string) => readFileSync(join(kRoot, "licenses", name), "utf8");
    let text = "hikari-ai, the HikariSub AI sidecar: third-party notices\n\n" +
        "hikari-ai is HikariSub's own code (GPL-3.0-or-later), built into one executable with Bun. Its source and\n" +
        "build are in https://github.com/altqx/hikari under src/ai-sidecar, so it can be rebuilt and relinked.\n\n";
    text += section("Bun (MIT)", licence("bun.md") + "\n\n" + licence("mit.txt"));
    text += section("JavaScriptCore and WebCore, statically linked into Bun (LGPL-2.0)", licence("lgpl-2.0.txt"));
    text += section("tinycc, statically linked into Bun (LGPL-2.1)", licence("lgpl-2.1.txt"));
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
