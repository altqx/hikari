import { describe, expect, test } from "bun:test";

import { ErrorCode, kProtocolVersion, Method } from "../../src/protocol";
import { ProviderError } from "../../src/providers/errors";
import { kKeyPlaceholder, type Relay, type RelayRequest, type RelayResponse } from "../../src/providers/relay";
import type { SearchService } from "../../src/providers/types";
import { Peer } from "../../src/rpc";
import { Sidecar } from "../../src/sidecar";
import { kCatalogue } from "../../src/tools/catalogue";
import { ToolSet, type HikariCalls } from "../../src/tools/toolset";
import { WebSearch } from "../../src/tools/web";
import { waitFor } from "../providers/mock";

interface Answer {
    status?: number;
    body: unknown;
}

/** Qt's relay for search targets: answers each path from `answers` and keeps what the sidecar sent. */
class SearchRelay implements Relay {
    sent: RelayRequest[] = [];
    constructor(public answers: Record<string, Answer> = {}) {}

    async send(request: RelayRequest): Promise<RelayResponse> {
        this.sent.push(request);
        const answer = this.answers[new URL(request.url).pathname] ?? { status: 404, body: { error: "no such path" } };
        const bytes = new TextEncoder().encode(typeof answer.body === "string" ? answer.body : JSON.stringify(answer.body));
        return {
            status: answer.status ?? 200,
            headers: [["content-type", "application/json"]],
            body: new ReadableStream({ start: (c) => { c.enqueue(bytes); c.close(); } }),
        };
    }

    header(i: number, name: string): string | undefined {
        return this.sent[i]!.headers.find(([n]) => n.toLowerCase() === name.toLowerCase())?.[1];
    }

    json(i: number): any {
        return JSON.parse(new TextDecoder().decode(this.sent[i]!.body));
    }
}

const kNoHa1: HikariCalls = { call: () => Promise.reject(new Error("no HA1 here")) };

function textOf(result: { content: { type: string; text?: string }[] }): string {
    return result.content.map((c) => c.text ?? "").join("");
}

function web(service: SearchService | undefined, answers: Record<string, Answer> = {}) {
    const relay = new SearchRelay(answers);
    let chosen = service;
    const search = new WebSearch(relay, () => chosen);
    return { relay, search, choose: (s: SearchService | undefined) => { chosen = s; } };
}

const kSearchAnswers: Record<SearchService, [string, Answer]> = {
    brave: ["/res/v1/web/search", { body: { web: { results: [
        { url: "https://example.org/a", title: "<strong>Aimer</strong> &amp; co", description: "Sings <b>it</b>", extra_snippets: ["More"] },
        { url: "", title: "no url", description: "dropped" },
    ] } } }],
    tavily: ["/search", { body: { results: [{ url: "https://example.org/a", title: "Aimer & co", content: "Sings it … More" }] } }],
    exa: ["/search", { body: { results: [{ url: "https://example.org/a", title: "Aimer & co", highlights: ["Sings it", "More"] }] } }],
    firecrawl: ["/v2/search", { body: { success: true, data: { web: [{ url: "https://example.org/a", title: "Aimer & co", description: "Sings it … More" }] } } }],
};

const kServices = Object.keys(kSearchAnswers) as SearchService[];

describe("WebSearch", () => {
    test.each(kServices)("%s searches through the search target with the key placeholder", async (service) => {
        const [path, answer] = kSearchAnswers[service];
        const { relay, search } = web(service, { [path]: answer });
        const result = await search.call("web_search", { query: "who sings this", count: 3 });
        expect(result.isError).toBeUndefined();
        expect(JSON.parse(textOf(result))).toEqual({
            results: [{ url: "https://example.org/a", title: "Aimer & co", snippet: "Sings it … More" }],
        });
        expect(relay.sent).toHaveLength(1);
        expect(relay.sent[0]!.target).toEqual({ kind: "search", id: service });
        expect(relay.sent[0]!.headers.some(([, v]) => v.includes(kKeyPlaceholder))).toBe(true);
    });

    test("each service gets the query, the count and its key header in its own shape", async () => {
        const sent: Record<string, { relay: SearchRelay }> = {};
        for (const service of kServices) {
            const [path, answer] = kSearchAnswers[service];
            const w = web(service, { [path]: answer });
            await w.search.call("web_search", { query: "aimer" });
            sent[service] = w;
        }
        const brave = sent.brave!.relay;
        const url = new URL(brave.sent[0]!.url);
        expect(url.host).toBe("api.search.brave.com");
        expect(brave.sent[0]!.method).toBe("GET");
        expect(Object.fromEntries(url.searchParams)).toEqual({ q: "aimer", count: "5", extra_snippets: "true" });
        expect(brave.header(0, "x-subscription-token")).toBe(kKeyPlaceholder);

        const tavily = sent.tavily!.relay;
        expect(tavily.sent[0]!.url).toBe("https://api.tavily.com/search");
        expect(tavily.header(0, "authorization")).toBe(`Bearer ${kKeyPlaceholder}`);
        expect(tavily.json(0)).toEqual({ query: "aimer", max_results: 5, search_depth: "basic" });

        const exa = sent.exa!.relay;
        expect(exa.sent[0]!.url).toBe("https://api.exa.ai/search");
        expect(exa.header(0, "x-api-key")).toBe(kKeyPlaceholder);
        expect(exa.json(0)).toEqual({ query: "aimer", numResults: 5, contents: { highlights: { maxCharacters: 600 } } });

        const firecrawl = sent.firecrawl!.relay;
        expect(firecrawl.sent[0]!.url).toBe("https://api.firecrawl.dev/v2/search");
        expect(firecrawl.header(0, "authorization")).toBe(`Bearer ${kKeyPlaceholder}`);
        expect(firecrawl.json(0)).toEqual({ query: "aimer", limit: 5 });
    });

    test("reads a page with each service that can, and Brave offers no web_fetch", async () => {
        const pages: Record<Exclude<SearchService, "brave">, [string, Answer]> = {
            tavily: ["/extract", { body: { results: [{ url: "https://example.org/a", raw_content: "# Page" }], failed_results: [] } }],
            exa: ["/contents", { body: { results: [{ url: "https://example.org/a", title: "A", text: "# Page" }], statuses: [{ id: "https://example.org/a", status: "success" }] } }],
            firecrawl: ["/v2/scrape", { body: { success: true, data: { markdown: "# Page", metadata: { title: "A" } } } }],
        };
        for (const [service, [path, answer]] of Object.entries(pages) as [SearchService, [string, Answer]][]) {
            const { relay, search } = web(service, { [path]: answer });
            expect(search.tools().map((t) => t.name)).toEqual(["web_search", "web_fetch"]);
            const page = JSON.parse(textOf(await search.call("web_fetch", { url: "https://example.org/a" })));
            expect(page.text).toBe("# Page");
            expect(page.url).toBe("https://example.org/a");
            expect(JSON.stringify(relay.json(0))).toContain("https://example.org/a");
        }
        const { relay, search } = web("brave");
        expect(search.tools().map((t) => t.name)).toEqual(["web_search"]);
        expect((await search.call("web_fetch", { url: "https://example.org/a" })).isError).toBe(true);
        expect(relay.sent).toHaveLength(0);
    });

    test("a page that can't be read is an error the model can read", async () => {
        const { search } = web("tavily", { "/extract": { body: { results: [], failed_results: [{ url: "https://example.org/a", error: "blocked by robots" }] } } });
        const result = await search.call("web_fetch", { url: "https://example.org/a" });
        expect(result.isError).toBe(true);
        expect(textOf(result)).toContain("blocked by robots");
    });

    test("refuses bad arguments without sending anything", async () => {
        const { relay, search } = web("exa");
        for (const args of [{}, { query: " " }, { query: "a", count: 0 }, { query: "a", count: 11 }, { query: "a", count: 2.5 }])
            expect((await search.call("web_search", args)).isError).toBe(true);
        for (const url of ["example.org", "file:///etc/passwd", "javascript:alert(1)"])
            expect((await search.call("web_fetch", { url })).isError).toBe(true);
        expect(relay.sent).toHaveLength(0);
    });

    test("without a service it offers nothing and says web search is off", async () => {
        const { relay, search } = web(undefined);
        expect(search.tools()).toEqual([]);
        expect(textOf(await search.call("web_search", { query: "a" }))).toBe("Web search is off in Settings › AI.");
        expect(relay.sent).toHaveLength(0);
    });

    test.each([
        ["exa", 402, "rateLimited"], ["firecrawl", 402, "rateLimited"], ["tavily", 432, "rateLimited"],
        ["tavily", 433, "rateLimited"], ["brave", 429, "rateLimited"], ["brave", 422, "unauthorized"], ["tavily", 401, "unauthorized"],
    ] as const)("%s answering %d is %s", async (service, status, kind) => {
        const [path] = kSearchAnswers[service];
        const { search } = web(service, { [path]: { status, body: { error: { detail: "invalid subscription token" } } } });
        const result = await search.call("web_search", { query: "a" });
        expect(result.isError).toBe(true);
        expect(textOf(result)).toMatch(kind === "rateLimited" ? /no searches left/ : /refused HikariSub's key/);
        const error = await search.test(service).catch((e: unknown) => e);
        expect(error).toBeInstanceOf(ProviderError);
        expect((error as ProviderError).kind).toBe(kind);
    });

    test("test() runs one search with the named service", async () => {
        const [path, answer] = kSearchAnswers.firecrawl;
        const { relay, search } = web("brave", { [path]: answer });
        expect(await search.test("firecrawl")).toBeNull();
        expect(relay.sent[0]!.target).toEqual({ kind: "search", id: "firecrawl" });
        expect(relay.json(0).limit).toBe(1);
    });
});

describe("ToolSet with web search", () => {
    test("search_tools finds the web group only while a service is chosen", async () => {
        const { search, choose } = web(undefined, { [kSearchAnswers.exa[0]]: kSearchAnswers.exa[1] });
        const tools = new ToolSet(kNoHa1, kCatalogue, [search]);
        const enumOf = () => (tools.list().find((t) => t.name === "search_tools")!.inputSchema as any).properties.group.enum;
        expect(enumOf()).not.toContain("web");
        expect(tools.list(true).map((t) => t.name)).not.toContain("web_search");
        expect((await tools.call("search_tools", { group: "web" })).isError).toBe(true);
        expect(tools.has("web_search")).toBe(false);

        choose("exa");
        expect(enumOf()).toContain("web");
        expect(tools.list().map((t) => t.name)).not.toContain("web_search");
        const found = JSON.parse(textOf(await tools.call("search_tools", { query: "search the web" })));
        expect(found.tools.map((t: { name: string }) => t.name)).toContain("web_search");
        expect(tools.list().map((t) => t.name)).toContain("web_search");
        const answer = await tools.call("web_search", { query: "aimer" });
        expect(answer.isError).toBeUndefined();
        expect(JSON.parse(textOf(answer)).results[0].url).toBe("https://example.org/a");

        choose(undefined);
        expect(tools.list().map((t) => t.name)).not.toContain("web_search");
    });
});

describe("the sidecar", () => {
    function sidecar(relay: SearchRelay) {
        const sent: Record<string, any>[] = [];
        const peer = new Peer((line) => sent.push(JSON.parse(line)), () => {});
        let id = 0;
        const ask = async (method: string, params: unknown) => {
            const mine = ++id;
            peer.feed(new TextEncoder().encode(JSON.stringify({ jsonrpc: "2.0", id: mine, method, params }) + "\n"));
            expect(await waitFor(() => sent.some((m) => m.id === mine))).toBe(true);
            return sent.find((m) => m.id === mine)!;
        };
        new Sidecar(peer, { buildId: "test", hikariApiVersion: 1, version: "0.0.0", packages: {} }, { relay });
        return { ask };
    }

    test("_hikari/search.test searches once and answers the service's refusal as a provider error", async () => {
        const relay = new SearchRelay({ [kSearchAnswers.tavily[0]]: kSearchAnswers.tavily[1], "/v2/search": { status: 402, body: { error: "Payment required" } } });
        const { ask } = sidecar(relay);
        await ask(Method.initialize, { protocolVersion: kProtocolVersion, hikariApiVersion: 1, buildId: "test" });
        expect((await ask(Method.searchTest, { service: "tavily" })).result).toBeNull();
        const refused = await ask(Method.searchTest, { service: "firecrawl" });
        expect(refused.error.code).toBe(ErrorCode.providerFailed);
        expect(refused.error.data).toEqual({ kind: "rateLimited", httpStatus: 402, detail: "Payment required" });
        for (const params of [{}, { service: "google" }, null])
            expect((await ask(Method.searchTest, params)).error.code).toBe(ErrorCode.invalidParams);
        expect(relay.sent.map((r) => r.target.id)).toEqual(["tavily", "firecrawl"]);
    });

    test("providers.set chooses the service the web tools search with", async () => {
        const relay = new SearchRelay({ [kSearchAnswers.exa[0]]: kSearchAnswers.exa[1] });
        const { ask } = sidecar(relay);
        await ask(Method.initialize, { protocolVersion: kProtocolVersion, hikariApiVersion: 1, buildId: "test" });
        const names = async () => (await ask(Method.toolsList, { all: true })).result.tools.map((t: { name: string }) => t.name);
        expect(await names()).not.toContain("web_search");
        expect((await ask(Method.providersSet, { providers: [], searchService: "exa" })).result).toBeNull();
        expect(await names()).toContain("web_search");
        const call = await ask(Method.toolsCall, { name: "web_search", arguments: { query: "aimer" } });
        expect(call.result.isError).toBeUndefined();
        expect(relay.sent[0]!.target).toEqual({ kind: "search", id: "exa" });
        expect((await ask(Method.providersSet, { providers: [], searchService: "bing" })).error.code).toBe(ErrorCode.invalidParams);
        await ask(Method.providersSet, { providers: [] });
        expect(await names()).not.toContain("web_search");
    });
});
