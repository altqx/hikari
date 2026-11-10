import bun from "../licenses/bun.md" with { type: "text" };
import lgpl20 from "../licenses/lgpl-2.0.txt" with { type: "text" };
import lgpl21 from "../licenses/lgpl-2.1.txt" with { type: "text" };
import mit from "../licenses/mit.txt" with { type: "text" };

export function section(title: string, body: string): string {
    return `${"=".repeat(78)}\n${title}\n${"=".repeat(78)}\n\n${body.trimEnd()}\n\n`;
}

export const kMitText = mit;

/** Bun's own notices, which the runtime carries whichever bundle it runs. */
export function bunNotices(): string {
    return section("Bun (MIT)", bun + "\n\n" + mit) +
        section("JavaScriptCore and WebCore, statically linked into Bun (LGPL-2.0)", lgpl20) +
        section("tinycc, statically linked into Bun (LGPL-2.1)", lgpl21);
}
