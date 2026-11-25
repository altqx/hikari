import { describe, expect, test } from "bun:test";
import { ErrorCode, Method } from "../../src/protocol";
import { RpcError } from "../../src/rpc";
import { checkedPluginTools, PluginTools } from "../../src/tools/plugins";
import { ToolSet, type HikariCalls } from "../../src/tools/toolset";

const kNoHa1: HikariCalls = { call: () => Promise.reject(new Error("no HA1 here")) };

const kTools = [
    { name: "p_0123abcd_count", description: "Words: Counts words", readOnly: true,
        inputSchema: { type: "object", properties: { line: { type: "integer" } }, additionalProperties: false } },
    { name: "p_89abcdef_fix", description: "Words: Fixes words", readOnly: false,
        inputSchema: { type: "object", additionalProperties: false } },
];

function textOf(result: { content: { type: string; text?: string }[] }): string {
    return result.content.map((c) => c.text ?? "").join("");
}

describe("PluginTools", () => {
    test("search_tools finds them, marked as Qt says, and a call asks Qt for the session", async () => {
        const asked: { method: string; params: unknown }[] = [];
        const plugins = new PluginTools(async (method, params) => {
            asked.push({ method, params });
            return { result: { words: 3 } };
        });
        plugins.set(kTools);
        const tools = new ToolSet(kNoHa1, undefined, () => [plugins.source("s1")]);
        const found = JSON.parse(textOf(await tools.call("search_tools", { group: "plugin_tools" })));
        expect(found.tools.map((t: { name: string }) => t.name)).toEqual(["p_0123abcd_count", "p_89abcdef_fix"]);
        const listed = tools.list(true);
        expect(listed.find((t) => t.name === "p_0123abcd_count")?.annotations.readOnlyHint).toBe(true);
        expect(listed.find((t) => t.name === "p_89abcdef_fix")?.annotations.readOnlyHint).toBe(false);

        const result = await tools.call("p_0123abcd_count", { line: 4 });
        expect(textOf(result)).toBe(JSON.stringify({ words: 3 }));
        expect(asked).toEqual([{ method: Method.pluginToolsCall,
            params: { sessionId: "s1", tool: "p_0123abcd_count", arguments: { line: 4 } } }]);
    });

    test("HikariSub's MCP server lists only the read-only tools, and calls them without a session", async () => {
        const asked: unknown[] = [];
        const plugins = new PluginTools(async (_method, params) => {
            asked.push(params);
            return { result: null };
        });
        plugins.set(kTools);
        const source = plugins.readOnlySource();
        expect(source.tools().map((t) => t.name)).toEqual(["p_0123abcd_count"]);
        expect(textOf(await source.call("p_0123abcd_count", {}))).toBe("null");
        expect(asked).toEqual([{ tool: "p_0123abcd_count", arguments: {} }]);
        expect(plugins.readOnly("p_0123abcd_count")).toBe(true);
        expect(plugins.readOnly("p_89abcdef_fix")).toBe(false);
    });

    test("Qt's refusal reaches the model as text", async () => {
        const plugins = new PluginTools(() =>
            Promise.reject(new RpcError(ErrorCode.hikariApi, "The user said no.", { code: "refused" })));
        plugins.set(kTools);
        const tools = new ToolSet(kNoHa1, undefined, () => [plugins.source("s1")]);
        const result = await tools.call("p_89abcdef_fix", {});
        expect(result.isError).toBe(true);
        expect(textOf(result)).toContain("The user said no.");
    });

    test("a list from Qt needs names every provider takes", () => {
        expect(checkedPluginTools({ tools: kTools })).toEqual(kTools);
        expect(checkedPluginTools({ tools: [{ ...kTools[0], name: "has space" }] })).toBeUndefined();
        expect(checkedPluginTools({ tools: [{ ...kTools[0], readOnly: "yes" }] })).toBeUndefined();
        expect(checkedPluginTools({})).toBeUndefined();
    });
});
