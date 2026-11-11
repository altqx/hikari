import { describe, expect, test } from "bun:test";
import { AgentSessions, type Complete } from "../../src/agent/session";
import type { SessionUpdate, SessionUpdateParams } from "../../src/protocol";
import type { CompleteParams, Finished, Usage } from "../../src/providers/types";
import { tool } from "../../src/tools/catalogue";
import { type CallToolResult, type HikariCalls, ToolSet, type ToolSource } from "../../src/tools/toolset";
import { whileOn } from "../../src/tools/web";

const kUsage: Usage = { inputTokens: 1, outputTokens: 1, cachedInputTokens: 0, cacheWriteTokens: 0, cacheWriteHourTokens: 0 };

const ha1: HikariCalls = { call: async () => ({}) as never };

class FakeWeb implements ToolSource {
    readonly group = "web";
    readonly description = "Web search and page reading.";
    result: CallToolResult = { content: [{ type: "text", text: "{\"results\":[]}" }] };
    tools() {
        return [tool("web_search", "Searches the web.", { type: "object", properties: {} }, "read", "web")];
    }
    async call(): Promise<CallToolResult> {
        return this.result;
    }
}

function harness(answers: Partial<Finished>[], webSearch?: boolean) {
    const requests: CompleteParams[] = [];
    const web = new FakeWeb();
    const complete: Complete = async (params, _signal, emit) => {
        requests.push(structuredClone(params));
        const answer = answers[Math.min(requests.length - 1, answers.length - 1)]!;
        if (answer.text)
            emit(params.stream, { kind: "text", text: answer.text });
        return { reason: "end", text: "", toolCalls: [], usage: kUsage, ...answer };
    };
    const updates: SessionUpdate[] = [];
    const sessions = new AgentSessions((_id, on) => new ToolSet(ha1, undefined, [whileOn(web, on)]), complete,
        (_method, params: SessionUpdateParams) => updates.push(params.update));
    sessions.open({ sessionId: "t1", model: { providerId: "p", model: "m" }, ...(webSearch === undefined ? {} : { webSearch }) });
    const prompt = (extra: { webSearch?: boolean } = {}) =>
        sessions.prompt({ sessionId: "t1", prompt: [{ type: "text", text: "Find it" }], ...extra });
    const of = <K extends SessionUpdate["sessionUpdate"]>(kind: K) =>
        updates.filter((u): u is Extract<SessionUpdate, { sessionUpdate: K }> => u.sessionUpdate === kind);
    const offered = (i: number) => requests[i]!.request.tools!.map((t) => t.name);
    return { requests, web, updates, prompt, of, offered };
}

describe("web search in agent sessions", () => {
    test("a session searches only while Qt says so, and a prompt can switch it", async () => {
        const h = harness([{ text: "Hi." }], true);
        await h.prompt();
        expect(h.requests[0]!.request.webSearch).toBe(true);
        await h.prompt({ webSearch: false });
        expect(h.requests[1]!.request.webSearch).toBeUndefined();
        await h.prompt();
        expect(h.requests[2]!.request.webSearch).toBeUndefined();
    });

    test("without webSearch the session doesn't search", async () => {
        const h = harness([{ text: "Hi." }]);
        await h.prompt();
        expect(h.requests[0]!.request.webSearch).toBeUndefined();
    });

    test("the search service's tools leave search_tools and the offered tools while search is off", async () => {
        const found = { type: "toolCall" as const, id: "c1", name: "search_tools", argumentsJson: "{\"group\":\"web\"}" };
        const h = harness([{ toolCalls: [found], reason: "toolUse" }, { text: "Found." }], true);
        await h.prompt();
        expect(h.offered(1)).toContain("web_search");
        await h.prompt({ webSearch: false });
        expect(h.offered(2)).not.toContain("web_search");
        expect(JSON.stringify(h.requests[2]!.request.tools)).not.toContain("\"web\"");
    });

    test("an answer's citations, suggestions and searches reach Qt before its usage, with its text length", async () => {
        const citations = [{ index: 1, url: "https://a.example/x", title: "A", start: 0, end: 3 }];
        const searches = [{ kind: "search" as const, query: "ass karaoke", urls: ["https://a.example/x"] }];
        const h = harness([{ text: "Yes😀.", citations, searchSuggestions: "<div>s</div>", searches }], true);
        await h.prompt();
        expect(h.of("sources")).toEqual([{ sessionUpdate: "sources", textLength: 6, citations,
            searchSuggestions: "<div>s</div>", searches }]);
        const kinds = h.updates.map((u) => u.sessionUpdate);
        expect(kinds.indexOf("sources")).toBeLessThan(kinds.indexOf("usage"));
        expect(kinds.lastIndexOf("agent_message_chunk")).toBeLessThan(kinds.indexOf("sources"));
    });

    test("an answer that neither cited nor searched sends no sources", async () => {
        const h = harness([{ text: "Plain." }], true);
        await h.prompt();
        expect(h.of("sources")).toEqual([]);
    });

    test("a used-up search plan tells Qt, and the turn goes on", async () => {
        const call = { type: "toolCall" as const, id: "c1", name: "web_search", argumentsJson: "{}" };
        const found = { type: "toolCall" as const, id: "c0", name: "search_tools", argumentsJson: "{\"group\":\"web\"}" };
        const h = harness([{ toolCalls: [found], reason: "toolUse" }, { toolCalls: [call], reason: "toolUse" },
            { text: "I couldn't search." }], true);
        h.web.result = { content: [{ type: "text", text: "no searches left" }], isError: true,
            searchFailure: { service: "brave", reason: "usedUp", httpStatus: 402 } };
        expect(await h.prompt()).toEqual({ stopReason: "end_turn" });
        expect(h.of("search_failed")).toEqual([{ sessionUpdate: "search_failed", service: "brave", reason: "usedUp",
            httpStatus: 402 }]);
    });
});

describe("whileOn", () => {
    test("passes failures through with why the service failed", async () => {
        const web = new FakeWeb();
        web.result = { content: [], isError: true, searchFailure: { service: "exa", reason: "key", httpStatus: 401 } };
        expect((await whileOn(web, () => true).call("web_search", {})).searchFailure?.reason).toBe("key");
        expect(whileOn(web, () => false).tools()).toEqual([]);
    });
});
