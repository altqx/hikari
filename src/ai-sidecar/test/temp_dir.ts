import { rmSync } from "node:fs";

const kRetried = new Set(["EBUSY", "EMFILE", "ENFILE", "ENOTEMPTY", "EPERM"]);

// Bun 1.4.4's rmSync parses maxRetries and retryDelay but ignores them, and on Windows a
// child we spawned or a virus scanner can briefly hold a handle on a file inside the dir.
export function removeTempDir(dir: string, { attempts = 20, delayMs = 250, remove = rmSync } = {}) {
    for (let attempt = 1; ; ++attempt) {
        try {
            remove(dir, { recursive: true, force: true });
            return;
        } catch (error) {
            if (attempt >= attempts || !kRetried.has((error as NodeJS.ErrnoException).code ?? ""))
                throw error;
            Bun.sleepSync(delayMs);
        }
    }
}
