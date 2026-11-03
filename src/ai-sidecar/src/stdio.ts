const toStdout = process.stdout.write.bind(process.stdout);
const toStderr = process.stderr.write.bind(process.stderr);

export function writeProtocol(line: string): void {
    toStdout(line);
}

process.stdout.write = toStderr as typeof process.stdout.write;

const viaStderr = new console.Console({ stdout: process.stderr, stderr: process.stderr });
for (const name of ["log", "info", "debug", "warn", "error", "trace", "dir", "dirxml", "table", "group",
    "groupCollapsed", "groupEnd", "count", "countReset", "time", "timeEnd", "timeLog", "assert"] as const) {
    const method = viaStderr[name] as (...args: unknown[]) => void;
    (console as unknown as Record<string, unknown>)[name] = method.bind(viaStderr);
}
