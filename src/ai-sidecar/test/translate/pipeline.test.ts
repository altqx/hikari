import { afterEach, beforeEach, expect, test } from "bun:test";

import { ErrorCode, kProtocolVersion, Method, type TranslateResult } from "../../src/protocol";
import { Peer } from "../../src/rpc";
import { Sidecar } from "../../src/sidecar";
import { markersOf } from "../../src/translate/audit";
import { dataOf } from "../../src/translate/honya";
import { chunked } from "../../src/translate/pipeline";
import { RelayPlayer } from "../providers/harness";
import { waitFor } from "../providers/mock";
import { EpisodeProvider, kLines, kProject } from "./episode";

type Message = Record<string, any>;

let provider: EpisodeProvider;
let sent: Message[];
let feed: (message: unknown) => void;
let project: { name: string; data: string }[] | null;
let nextId = 10;

beforeEach(async () => {
    provider = new EpisodeProvider();
    sent = [];
    project = kProject;
    const player = new RelayPlayer((message) => feed(message));
    const peer = new Peer((line) => {
        const message = JSON.parse(line) as Message;
        sent.push(message);
        player.take(message);
        if (message.method === Method.apiCall) {
            expect(message.params).toEqual({ operation: "translation.project", params: { document: 7 } });
            feed({ jsonrpc: "2.0", id: message.id, result: project ? { folder: "Show", files: project } : { folder: null, files: [] } });
        }
    }, () => {});
    feed = (message) => peer.feed(new TextEncoder().encode(JSON.stringify(message) + "\n"));
    new Sidecar(peer, { buildId: "test", hikariApiVersion: 1, version: "0.0.0-dev", packages: {} });
    feed({ jsonrpc: "2.0", id: 0, method: Method.initialize, params: { protocolVersion: kProtocolVersion, hikariApiVersion: 1, buildId: "test" } });
    feed({ jsonrpc: "2.0", id: 1, method: Method.providersSet, params: { providers: [
        { id: "mock", preset: "custom", wire: "chat-completions", baseUrl: provider.base }] } });
    await waitFor(() => sent.some((m) => m.id === 1));
});
afterEach(() => provider.stop());

const models = { translator: { providerId: "mock", model: "translator-m" }, reviewer: { providerId: "mock", model: "reviewer-m" } };

async function run(params: Record<string, unknown> = {}): Promise<Message> {
    const id = nextId++;
    feed({ jsonrpc: "2.0", id, method: Method.translate, params: { stream: 3, document: 7, language: "English", models,
        lines: kLines, chunkTokens: 50, ...params } });
    expect(await waitFor(() => sent.some((m) => m.id === id && !("method" in m)), 10_000)).toBe(true);
    return sent.find((m) => m.id === id && !("method" in m))!;
}

const lineOf = (result: TranslateResult, key: number) => result.lines.find((l) => l.key === key)!;

test("the fixture episode runs end to end: locked terms hold, markers survive, every Line is approved", async () => {
    const result = (await run()).result as TranslateResult;
    expect(result.lines.map((l) => l.key)).toEqual([1, 2, 3, 4, 5, 6]);
    expect(result.lines.every((l) => l.review === "approved")).toBe(true);
    expect(lineOf(result, 2).text).toBe("Today's the Arcane Academy exam, huh.");
    expect(lineOf(result, 5).text).toBe("Yui, let's go!");
    for (const line of kLines)
        expect(markersOf(lineOf(result, line.key).text)).toEqual(markersOf(line.text));
    expect(lineOf(result, 3).text).toBe("⟦1⟧I'm nervous⟦2⟧…\\NWill I be okay?");
    expect(lineOf(result, 6).text).toBe("⟦1⟧This isn't a dream, is it?");
    expect(lineOf(result, 1).text).toBe("⟦1⟧Morning, Sora.");
});

test("the translator sees only the chunk's terms and characters, the previous Lines and the reviewer's feedback", async () => {
    await run();
    const translator = provider.of("You translate");
    expect(translator[0]!.payload.REFERENCE.glossary).toEqual([{ source: "魔導院", target: "Arcane Academy", policy: "hard_locked" }]);
    expect(translator[0]!.payload.REFERENCE.characters.map((c: Message) => c.source)).toEqual(["ユイ", "ソラ"]);
    expect(translator[0]!.payload.REFERENCE.style).toBe("# Style\n\nCasual, short Lines.");
    expect(translator[1]!.payload.FEEDBACK).toEqual([{ id: 2, issue: "\"魔導院\" is locked: write it as \"Arcane Academy\"." }]);
    const second = translator.find((c) => c.payload.LINES[0].id === 4)!;
    expect(second.payload.REFERENCE.glossary).toEqual([]);
    expect(second.payload.PREVIOUS).toEqual(["⟦1⟧Morning, Sora.", "Today's the Arcane Academy exam, huh.",
        "⟦1⟧I'm nervous⟦2⟧…\\NWill I be okay?"]);
    const retried = translator.filter((c) => c.payload.LINES[0].id === 4);
    expect(retried.map((c) => (c.payload.FEEDBACK ?? []).map((f: Message) => f.id))).toEqual([[], [6], [5]]);
    expect(provider.of("You review").length).toBe(3);
    expect(provider.of("You prepare")).toEqual([]);
});

test("only approved chunks add to the glossary and characters, and a locked term is never rewritten", async () => {
    const result = (await run()).result as TranslateResult;
    expect(result.files.map((f) => f.name)).toEqual(["GLOSSARY.md", "CHARACTERS.md"]);
    const glossary = dataOf(result.files[0]!.data);
    expect(glossary.honya_only).toBe("kept");
    expect(glossary.terms).toEqual([
        { jp_term: "魔導院", translated_term: "Arcane Academy", policy: "hard_locked", protected: true, first_seen_chapter: 1 },
        { jp_term: "星詠み", translated_term: "Stargazer", category: "title", gloss: null, policy: "preferred" },
    ]);
    expect(result.files[0]!.data).toContain("| 星詠み | Stargazer | title | preferred |");
    const characters = dataOf(result.files[1]!.data).characters as Message[];
    expect(characters.map((c) => [c.id, c.jp_name, c.translated_name])).toEqual([
        ["sora", "ソラ", "Sora"], ["the-teacher", "先生", "the teacher"], ["yui", "ユイ", "Yui"]]);
});

test("Lines the reviewer never approves keep the best attempt, need review and add nothing to the project", async () => {
    provider.stubborn.add(4);
    const result = (await run({ retries: 1 })).result as TranslateResult;
    expect(lineOf(result, 4)).toEqual({ key: 4, text: "Stargazers, step forward.", review: "needsReview",
        issues: ["Drop the honorific: Sora calls her Yui."] });
    expect(lineOf(result, 5).review).toBe("needsReview");
    expect(lineOf(result, 1).review).toBe("approved");
    expect(result.files).toEqual([]);
    expect(provider.of("You translate").filter((c) => c.payload.LINES[0].id === 4)).toHaveLength(2);
});

test("a coherence fix that drops a marker is refused", async () => {
    const result = (await run()).result as TranslateResult;
    expect(provider.of("You check")).toHaveLength(1);
    expect(lineOf(result, 1).text).toBe("⟦1⟧Morning, Sora.");
});

test("without a project folder a pre-pass seeds the run's reference from the Lines, inventing nothing", async () => {
    project = null;
    const result = (await run()).result as TranslateResult;
    expect(provider.of("You prepare")).toHaveLength(1);
    const first = provider.of("You translate")[0]!;
    expect(first.payload.REFERENCE.glossary).toEqual([{ source: "魔導院", target: "Arcane Academy", policy: "preferred" }]);
    expect(first.payload.REFERENCE.characters).toEqual([{ source: "ユイ", target: "Yui" }]);
    expect(dataOf(result.files.find((f) => f.name === "GLOSSARY.md")!.data).terms)
        .not.toContainEqual(expect.objectContaining({ jp_term: "竜" }));
});

test("progress and usage come as notifications on the run's stream", async () => {
    await run();
    const progress = sent.filter((m) => m.method === Method.translateProgress).map((m) => m.params);
    expect(progress.filter((p) => p.stage === "translate")).toEqual([
        { stream: 3, stage: "translate", done: 3, total: 6 }, { stream: 3, stage: "translate", done: 6, total: 6 }]);
    expect(progress.filter((p) => p.role === "translator")).toHaveLength(5);
    expect(progress.find((p) => p.role)!.usage).toMatchObject({ inputTokens: 100, outputTokens: 20 });
});

test("the Project's source language names the pair in every prompt", async () => {
    await run({ sourceLanguage: "Japanese" });
    for (const call of provider.calls)
        expect(call.system.split("\n")[0]).toContain("from Japanese into English");
    expect(provider.of("You check").length).toBeGreaterThan(0);
});

test("without a source language the prompts name only the target, so the models detect the source", async () => {
    await run({ sourceLanguage: " " });
    for (const call of provider.calls) {
        expect(call.system.split("\n")[0]).toContain("into English");
        expect(call.system.split("\n")[0]).not.toContain(" from ");
    }
});

test("bad params are refused", async () => {
    expect((await run({ lines: [] })).error.code).toBe(ErrorCode.invalidParams);
    expect((await run({ models: { translator: models.translator } })).error.code).toBe(ErrorCode.invalidParams);
    expect((await run({ lines: [kLines[0], kLines[0]] })).error.code).toBe(ErrorCode.invalidParams);
    expect((await run({ sourceLanguage: 3 })).error.code).toBe(ErrorCode.invalidParams);
    expect(provider.calls).toEqual([]);
});

test("chunks never split a Line, and a long gap starts a new chunk past half the budget", () => {
    expect(chunked(kLines, 50).map((c) => c.map((l) => l.key))).toEqual([[1, 2, 3], [4, 5, 6]]);
    expect(chunked(kLines, 1000).map((c) => c.length)).toEqual([6]);
    expect(chunked(kLines, 1).map((c) => c.length)).toEqual([1, 1, 1, 1, 1, 1]);
});
