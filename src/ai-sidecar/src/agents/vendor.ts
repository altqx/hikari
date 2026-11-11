import { createHash } from "node:crypto";
import { existsSync } from "node:fs";
import { mkdir, readFile, rename, rm, writeFile } from "node:fs/promises";
import { dirname, join, normalize, sep } from "node:path";
import { pathToFileURL } from "node:url";
import { gunzipSync } from "node:zlib";

import lock from "../../bun.lock";

export interface LockedPackage {
    name: string;
    version: string;
    integrity: string;
}

export class IntegrityError extends Error {
    constructor(readonly pkg: LockedPackage, readonly actual: string) {
        super(`${pkg.name}@${pkg.version} doesn't match bun.lock (got ${actual})`);
    }
}

type LockEntry = [string, string, unknown, string];

export function lockedPackage(name: string, packages: Record<string, LockEntry> = lock.packages): LockedPackage {
    const entry = packages[name];
    if (!entry)
        throw new Error(`${name} isn't in bun.lock`);
    const [spec, , , integrity] = entry;
    return { name, version: spec.slice(spec.lastIndexOf("@") + 1), integrity };
}

export function tarballUrl(pkg: LockedPackage, registry = "https://registry.npmjs.org"): string {
    const base = pkg.name.slice(pkg.name.lastIndexOf("/") + 1);
    return `${registry}/${pkg.name}/-/${base}-${pkg.version}.tgz`;
}

export function sha512(data: Uint8Array): string {
    return `sha512-${createHash("sha512").update(data).digest("base64")}`;
}

function field(header: Uint8Array, offset: number, length: number): string {
    const bytes = header.subarray(offset, offset + length);
    const end = bytes.indexOf(0);
    return new TextDecoder().decode(end < 0 ? bytes : bytes.subarray(0, end));
}

/** The regular files of an npm tarball, with `package/` stripped. Links and paths leaving the package are refused. */
export function untar(tgz: Uint8Array): Map<string, Uint8Array> {
    const tar = gunzipSync(tgz);
    const files = new Map<string, Uint8Array>();
    let longName: string | undefined;
    for (let at = 0; at + 512 <= tar.length;) {
        const header = tar.subarray(at, at + 512);
        if (header.every((b) => b === 0))
            break;
        const size = parseInt(field(header, 124, 12).trim() || "0", 8);
        const type = String.fromCharCode(header[156] || 48);
        const body = tar.subarray(at + 512, at + 512 + size);
        at += 512 + Math.ceil(size / 512) * 512;
        if (type === "L") {
            longName = new TextDecoder().decode(body).replace(/\0+$/, "");
            continue;
        }
        if (type === "x" || type === "g" || type === "5")
            continue;
        const prefix = field(header, 345, 155);
        const name = longName ?? (prefix ? `${prefix}/${field(header, 0, 100)}` : field(header, 0, 100));
        longName = undefined;
        if (type !== "0")
            throw new Error(`${name}: only regular files are unpacked`);
        const path = normalize(name.slice(name.indexOf("/") + 1));
        if (!path || path.startsWith("..") || path.startsWith(sep) || /^[a-zA-Z]:/.test(path) || path.includes(`..${sep}`))
            throw new Error(`${name}: outside the package`);
        files.set(path, body.slice());
    }
    return files;
}

/** Fetches `pkg` once into `<root>/<version>/`, checked against its bun.lock sha512, and returns that folder. */
export async function vendor(pkg: LockedPackage, fetchFn: typeof fetch, root: string): Promise<string> {
    const dir = join(root, pkg.version);
    const stamp = join(dir, ".integrity");
    if (existsSync(stamp) && (await readFile(stamp, "utf8")) === pkg.integrity)
        return dir;
    const response = await fetchFn(tarballUrl(pkg));
    const tgz = new Uint8Array(await response.arrayBuffer());
    const actual = sha512(tgz);
    if (actual !== pkg.integrity)
        throw new IntegrityError(pkg, actual);
    const staging = `${dir}.part`;
    await rm(staging, { recursive: true, force: true });
    for (const [path, data] of untar(tgz)) {
        await mkdir(dirname(join(staging, path)), { recursive: true });
        await writeFile(join(staging, path), data);
    }
    await writeFile(join(staging, ".integrity"), pkg.integrity);
    await rm(dir, { recursive: true, force: true });
    await rename(staging, dir);
    return dir;
}

export type ClaudeAgentSdk = typeof import("@anthropic-ai/claude-agent-sdk");

export const kClaudeAgentSdk = "@anthropic-ai/claude-agent-sdk";

export async function loadClaudeAgentSdk(fetchFn: typeof fetch, vendorRoot: string): Promise<ClaudeAgentSdk> {
    const dir = await vendor(lockedPackage(kClaudeAgentSdk), fetchFn, join(vendorRoot, "claude-agent-sdk"));
    return await import(pathToFileURL(join(dir, "sdk.mjs")).href) as ClaudeAgentSdk;
}
