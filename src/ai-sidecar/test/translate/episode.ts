import type { Server } from "bun";

import { withData } from "../../src/translate/honya";

/** A short episode as Qt sends it: override blocks are ⟦n⟧ markers, the speaker is the Actor. */
export const kLines = [
    { key: 1, start: 1000, end: 3000, speaker: "ユイ", text: "⟦1⟧おはよう、ソラ。" },
    { key: 2, start: 3200, end: 5000, speaker: "ソラ", text: "今日は魔導院の試験だね。" },
    { key: 3, start: 5200, end: 7000, speaker: "ユイ", text: "⟦1⟧緊張する⟦2⟧…\\N大丈夫かな。" },
    { key: 4, start: 14000, end: 16000, speaker: "先生", text: "星詠みの者は前へ。" },
    { key: 5, start: 16200, end: 18000, speaker: "ソラ", text: "ユイ、行こう！" },
    { key: 6, start: 25000, end: 27000, speaker: "ユイ", text: "⟦1⟧夢じゃないよね？" },
];

/** What the mock translator writes for each source, first try and after feedback. */
export const kFirst: Record<string, string> = {
    "⟦1⟧おはよう、ソラ。": "⟦1⟧Morning, Sora.",
    "今日は魔導院の試験だね。": "Today's the Magic School exam, huh.",
    "⟦1⟧緊張する⟦2⟧…\\N大丈夫かな。": "⟦1⟧I'm nervous⟦2⟧…\\NWill I be okay?",
    "星詠みの者は前へ。": "Stargazers, step forward.",
    "ユイ、行こう！": "Yui-san, let's go!",
    "⟦1⟧夢じゃないよね？": "This isn't a dream, right?",
};
export const kFixed: Record<string, string> = {
    ...kFirst,
    "今日は魔導院の試験だね。": "Today's the Arcane Academy exam, huh.",
    "ユイ、行こう！": "Yui, let's go!",
    "⟦1⟧夢じゃないよね？": "⟦1⟧This isn't a dream, right?",
};

export const kProject = [
    { name: "GLOSSARY.md", data: withData("# Glossary", { terms: [
        { jp_term: "魔導院", translated_term: "Arcane Academy", policy: "hard_locked", protected: true, first_seen_chapter: 1 },
    ], honya_only: "kept" }) },
    { name: "CHARACTERS.md", data: withData("# Characters", { characters: [
        { id: "yui", jp_name: "ユイ", translated_name: "Yui", relationships: [] },
        { id: "sora", jp_name: "ソラ", translated_name: "Sora", relationships: [] },
    ] }) },
    { name: "STYLE.md", data: "# Style\n\nCasual, short Lines.\n" },
];

type Payload = Record<string, any>;
export interface Call {
    system: string;
    payload: Payload;
}

/**
 * An OpenAI-compatible server that plays every role from the request's system prompt: the translator breaks the
 * locked term and drops a marker until a Line gets feedback, and the reviewer rejects "Yui-san".
 */
export class EpisodeProvider {
    readonly calls: Call[] = [];
    /** Lines the reviewer never approves. */
    stubborn = new Set<number>();
    /** Lines the translator was given feedback on; it gets them right from then on. */
    private readonly told: number[] = [];
    private readonly server: Server<undefined>;

    constructor() {
        this.server = Bun.serve({ hostname: "127.0.0.1", port: 0, fetch: (request) => this.answer(request) });
    }

    get base(): string {
        return `http://127.0.0.1:${this.server.port}/v1`;
    }

    stop(): void {
        this.server.stop(true);
    }

    of(role: string): Call[] {
        return this.calls.filter((c) => c.system.startsWith(role));
    }

    private async answer(request: Request): Promise<Response> {
        const body = await request.json() as Payload;
        const text = (m: Payload) => typeof m.content === "string" ? m.content
            : (m.content as Payload[]).map((p) => p.text ?? "").join("");
        const system = text(body.messages.find((m: Payload) => m.role === "system") ?? { content: "" });
        const payload = JSON.parse(text(body.messages.findLast((m: Payload) => m.role === "user"))) as Payload;
        this.calls.push({ system, payload });
        const reply = JSON.stringify(this.play(system, payload));
        const chunk = (o: unknown) => `data: ${JSON.stringify(o)}\n\n`;
        return new Response(chunk({ choices: [{ delta: { content: "```json\n" + reply + "\n```" }, finish_reason: "stop" }] }) +
            chunk({ choices: [], usage: { prompt_tokens: 100, completion_tokens: 20 } }) + "data: [DONE]\n\n",
        { headers: { "content-type": "text/event-stream" } });
    }

    private play(system: string, payload: Payload): unknown {
        if (system.startsWith("You translate")) {
            const told = new Set((payload.FEEDBACK ?? []).map((f: Payload) => f.id));
            this.told.push(...told as Set<number>);
            return {
                lines: payload.LINES.map((l: Payload) => ({ id: l.id,
                    text: (this.told.includes(l.id) ? kFixed : kFirst)[l.text] ?? l.text })),
                new_terms: [{ source: "星詠み", target: "Stargazer", category: "title" },
                    { source: "魔導院", target: "Magic School" }],
                new_characters: payload.LINES.some((l: Payload) => l.speaker === "先生")
                    ? [{ source: "先生", target: "the teacher" }] : [],
            };
        }
        if (system.startsWith("You review")) {
            const bad = payload.LINES.filter((l: Payload) => this.stubborn.has(l.id) || l.translation.includes("Yui-san"));
            return bad.length === 0 ? { status: "approve", feedback: [] }
                : { status: "reject", feedback: bad.map((l: Payload) => ({ id: l.id, issue: "Drop the honorific: Sora calls her Yui." })) };
        }
        if (system.startsWith("You prepare"))
            return { characters: [{ source: "ユイ", target: "Yui" }], terms: [{ source: "魔導院", target: "Arcane Academy" },
                { source: "竜", target: "Dragon" }], examples: [] };
        if (system.startsWith("You check"))
            return { fixes: [
                { id: 6, text: "⟦1⟧This isn't a dream, is it?", reason: "register" },
                { id: 1, text: "Morning, Sora.", reason: "drops a marker, so it must be refused" },
            ] };
        return {};
    }
}
