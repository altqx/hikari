declare module "*.md" {
    const text: string;
    export default text;
}

declare module "*.lock" {
    const lock: { packages: Record<string, [string, string, unknown, string]> };
    export default lock;
}
