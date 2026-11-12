import { describe, expect, test } from "bun:test";
import { readFileSync } from "node:fs";
import { generate, kTypesFile, readReference } from "../../tools/gen-api";
import { kCatalogue as kOwn, kGroups } from "../../src/tools/catalogue";

const reference = readReference();
const access = new Map(reference.operations.map((op) => [op.name, op.access]));

function walk(value: unknown, visit: (key: string, value: unknown) => void): void {
    if (Array.isArray(value))
        return value.forEach((item) => walk(item, visit));
    if (typeof value !== "object" || value === null)
        return;
    for (const [key, child] of Object.entries(value)) {
        visit(key, child);
        walk(child, visit);
    }
}

describe("the generated types", () => {
    test("match HA1's reference; run `bun tools/gen-api.ts` after changing api.json", () => {
        expect(readFileSync(kTypesFile, "utf8")).toBe(generate(reference));
    });
});

describe("the tool catalogue", () => {
    test("places every HA1 operation, so a new one is a decision", () => {
        expect(kOwn.unplacedOperations()).toEqual([]);
    });

    test("names fit every provider's rule and are unique", () => {
        const names = kOwn.entries.map((e) => e.tool.name);
        for (const name of names)
            expect(name).toMatch(/^[a-zA-Z0-9_-]{1,64}$/);
        expect(new Set(names).size).toBe(names.length);
    });

    test("offers about twenty core tools, searching and commands among them", () => {
        const core = kOwn.entries.filter((e) => e.group === "core").map((e) => e.tool.name);
        expect(core.length).toBeGreaterThanOrEqual(15);
        expect(core.length).toBeLessThanOrEqual(25);
        expect(core).toEqual(expect.arrayContaining(["search_tools", "search_commands", "run_command", "documents_list",
            "lines_find", "document_apply"]));
        for (const entry of kOwn.entries)
            expect(Object.keys(kGroups)).toContain(entry.group);
    });

    test("takes each read and write mark from HA1", () => {
        for (const entry of kOwn.entries.filter((e) => e.operation)) {
            expect(entry.tool.annotations.readOnlyHint).toBe(access.get(entry.operation!) === "read");
            expect(entry.tool._meta["hikari/group"]).toBe(entry.group);
        }
        const run = kOwn.entries.find((e) => e.tool.name === "run_command")!;
        expect(run.operation).toBe("commands.run");
        expect(run.tool.annotations.readOnlyHint).toBe(false);
        expect(kOwn.entries.find((e) => e.tool.name === "search_tools")!.tool.annotations.readOnlyHint).toBe(true);
    });

    test("gives self-contained object schemas without const, which Gemini refuses", () => {
        for (const { tool } of kOwn.entries) {
            expect(tool.inputSchema.type).toBe("object");
            expect(tool.inputSchema.properties).toBeDefined();
            walk(tool.inputSchema, (key) => {
                expect(key).not.toBe("$ref");
                expect(key).not.toBe("const");
            });
        }
        const apply = kOwn.entries.find((e) => e.tool.name === "document_apply")!.tool.inputSchema as any;
        expect(apply.properties.document).toMatchObject({ type: "integer", minimum: 1, description: "Defaults to the active Document." });
        expect(apply.properties.edits.items.oneOf[0].properties.op).toEqual({ enum: ["set_line"] });
    });

    test("offers the frame tools and the solvers in typeset, with full instead of maxPixels on video_frame", () => {
        const typeset = kOwn.entries.filter((e) => e.group === "typeset").map((e) => e.tool.name).sort();
        expect(typeset).toEqual(["typeset_apply", "typeset_check", "typeset_clip", "typeset_drawing", "typeset_hydra",
            "typeset_perspective", "typeset_shift", "video_colours", "video_frame", "video_ink_bounds", "video_keyframes"]);
        expect(kOwn.entries.find((e) => e.tool.name === "typeset_apply")!.tool.annotations.readOnlyHint).toBe(false);
        expect(kOwn.entries.find((e) => e.tool.name === "typeset_perspective")!.tool.annotations.readOnlyHint).toBe(true);
        const frame = kOwn.entries.find((e) => e.tool.name === "video_frame")!.tool.inputSchema as any;
        expect(Object.keys(frame.properties).sort()).toEqual(["crop", "document", "frame", "full", "line", "maxEdge", "subtitles"]);
        expect(frame.properties.full.type).toBe("boolean");
        expect(frame.additionalProperties).toBe(false);
    });

    test("names tools, not HA1 operations, in every description", () => {
        const operations = reference.operations.map((op) => op.name);
        for (const { tool } of kOwn.entries) {
            const texts = [tool.description];
            walk(tool.inputSchema, (key, value) => {
                if (key === "description" && typeof value === "string")
                    texts.push(value);
            });
            for (const text of texts)
                for (const name of operations)
                    expect(text).not.toContain(name);
        }
        const run = kOwn.entries.find((e) => e.tool.name === "run_command")!.tool.inputSchema as any;
        expect(run.properties.command.description).toBe("A command id from search_commands.");
    });
});
