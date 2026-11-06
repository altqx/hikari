import { dirname, join, resolve } from "node:path";
import { loadSdksWith } from "./providers/sdks";
import { start } from "./start";

// Bun's bundler bakes __dirname in at build time; hikari-ai-runtime is run as `hikari-ai-runtime <bundle>`.
const bundleDir = dirname(resolve(process.argv[2] ?? ""));
loadSdksWith((name) => require(join(bundleDir, `hikari-ai-${name}.js`)));
start();
