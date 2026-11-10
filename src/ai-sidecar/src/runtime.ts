import { resolve } from "node:path";
import { bunNotices } from "./notices";

const bundle = process.argv[2];
if (bundle === "--licenses") {
    process.stdout.write("hikari-ai-runtime, the Bun that runs HikariSub's AI sidecar: third-party notices\n\n" +
        bunNotices().replace(/\r\n?/g, "\n"));
    process.exit(0);
}
if (!bundle) {
    process.stderr.write("usage: hikari-ai-runtime <bundle> | --licenses\n");
    process.exit(64);
}
require(resolve(bundle));
