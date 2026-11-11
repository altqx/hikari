import { describe, expect, test } from "bun:test";
import { existsSync, mkdtempSync, readFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { gzipSync } from "node:zlib";

import { IntegrityError, kClaudeAgentSdk, lockedPackage, sha512, tarballUrl, untar, vendor } from "../../src/agents/vendor";

function tarEntry(name: string, data: string, type = "0"): Uint8Array {
    const body = new TextEncoder().encode(data);
    const header = new Uint8Array(512);
    header.set(new TextEncoder().encode(name).subarray(0, 100), 0);
    header.set(new TextEncoder().encode(body.length.toString(8).padStart(11, "0")), 124);
    header[156] = type.charCodeAt(0);
    const padded = new Uint8Array(Math.ceil(body.length / 512) * 512);
    padded.set(body);
    return new Uint8Array([...header, ...padded]);
}

function tgz(...entries: Uint8Array[]): Uint8Array {
    return gzipSync(new Uint8Array([...entries.flatMap((e) => [...e]), ...new Uint8Array(1024)]));
}

function serving(data: Uint8Array) {
    const urls: string[] = [];
    const fetchFn = (async (url: string) => {
        urls.push(url);
        return new Response(data);
    }) as unknown as typeof fetch;
    return { urls, fetchFn };
}

describe("the vendored Claude Agent SDK", () => {
    test("the version and sha512 come from bun.lock", () => {
        const pkg = lockedPackage(kClaudeAgentSdk);
        expect(pkg.version).toMatch(/^\d+\.\d+\.\d+$/);
        expect(pkg.integrity).toStartWith("sha512-");
        expect(tarballUrl(pkg)).toBe(
            `https://registry.npmjs.org/@anthropic-ai/claude-agent-sdk/-/claude-agent-sdk-${pkg.version}.tgz`);
    });

    test("a matching tarball is unpacked once and reused", async () => {
        const archive = tgz(tarEntry("package/sdk.mjs", "export const ok = 1;"), tarEntry("package/lib/a.js", "a"));
        const pkg = { name: kClaudeAgentSdk, version: "1.2.3", integrity: sha512(archive) };
        const root = mkdtempSync(join(tmpdir(), "vendor-"));
        const { urls, fetchFn } = serving(archive);
        const dir = await vendor(pkg, fetchFn, root);
        expect(dir).toBe(join(root, "1.2.3"));
        expect(readFileSync(join(dir, "sdk.mjs"), "utf8")).toBe("export const ok = 1;");
        expect(readFileSync(join(dir, "lib", "a.js"), "utf8")).toBe("a");
        expect(await vendor(pkg, fetchFn, root)).toBe(dir);
        expect(urls).toHaveLength(1);
    });

    test("a mismatch is refused and nothing is written", async () => {
        const archive = tgz(tarEntry("package/sdk.mjs", "evil"));
        const pkg = { name: kClaudeAgentSdk, version: "1.2.3", integrity: sha512(tgz(tarEntry("package/sdk.mjs", "ok"))) };
        const root = mkdtempSync(join(tmpdir(), "vendor-"));
        await expect(vendor(pkg, serving(archive).fetchFn, root)).rejects.toBeInstanceOf(IntegrityError);
        expect(existsSync(join(root, "1.2.3"))).toBe(false);
        expect(existsSync(join(root, "1.2.3.part"))).toBe(false);
    });

    test("links and paths outside the package are refused", () => {
        expect(() => untar(tgz(tarEntry("package/../../evil.js", "x")))).toThrow("outside the package");
        expect(() => untar(tgz(tarEntry("package/link", "", "2")))).toThrow("only regular files");
    });
});
