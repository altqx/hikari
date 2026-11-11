import { writeProtocol } from "./stdio";
import notices from "../NOTICES.txt" with { type: "text" };
import { ErrorCode } from "./protocol";
import { Peer, RpcError } from "./rpc";
import { Sidecar } from "./sidecar";

declare const HIKARI_BUILD_ID: string;
declare const HIKARI_API_VERSION: number;
declare const HIKARI_AI_VERSION: string;
declare const HIKARI_PACKAGES: Record<string, string>;

function option(name: string): string | undefined {
    const at = process.argv.indexOf(name);
    return at < 0 ? undefined : process.argv[at + 1];
}

export function start(): void {
    if (process.argv.includes("--licenses")) {
        writeProtocol(notices);
        process.exit(0);
    }

    const peer = new Peer(writeProtocol, (reason) => {
        process.stderr.write(`hikari-ai: ${reason}\n`);
        process.exit(2);
    });
    new Sidecar(peer, {
        buildId: typeof HIKARI_BUILD_ID === "string" ? HIKARI_BUILD_ID : "dev",
        hikariApiVersion: typeof HIKARI_API_VERSION === "number" ? HIKARI_API_VERSION : 0,
        version: typeof HIKARI_AI_VERSION === "string" ? HIKARI_AI_VERSION : "0.0.0-dev",
        packages: typeof HIKARI_PACKAGES === "object" ? HIKARI_PACKAGES : {},
    }, undefined, { vendor: option("--vendor") });

    process.stdin.on("data", (chunk: Buffer) => peer.feed(chunk));
    process.stdin.on("end", () => {
        peer.close(new RpcError(ErrorCode.internalError, "stdin closed"));
        process.exit(0);
    });
}
