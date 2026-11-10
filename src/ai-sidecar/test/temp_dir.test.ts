import { expect, test } from "bun:test";
import { existsSync, mkdtempSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { removeTempDir } from "./temp_dir";

const failing = (code: string, times: number) => {
    let calls = 0;
    const remove = () => {
        if (++calls <= times)
            throw Object.assign(new Error(code), { code });
    };
    return { remove, calls: () => calls };
};

test("a busy temp dir is removed once the handle goes away", () => {
    const busy = failing("EBUSY", 3);
    removeTempDir("unused", { delayMs: 0, remove: busy.remove });
    expect(busy.calls()).toBe(4);
});

test("a dir that stays busy fails after the last attempt", () => {
    const busy = failing("EBUSY", Infinity);
    expect(() => removeTempDir("unused", { attempts: 3, delayMs: 0, remove: busy.remove })).toThrow("EBUSY");
    expect(busy.calls()).toBe(3);
});

test("other errors are not retried", () => {
    const denied = failing("EACCES", Infinity);
    expect(() => removeTempDir("unused", { delayMs: 0, remove: denied.remove })).toThrow("EACCES");
    expect(denied.calls()).toBe(1);
});

test("a real temp dir and its files are removed", () => {
    const dir = mkdtempSync(join(tmpdir(), "hikari-ai-temp-dir-"));
    writeFileSync(join(dir, "file"), "x");
    removeTempDir(dir);
    expect(existsSync(dir)).toBe(false);
});
