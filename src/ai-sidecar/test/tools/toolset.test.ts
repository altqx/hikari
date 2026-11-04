import { describe, expect, test } from "bun:test";
import { inflateSync } from "node:zlib";
import { ErrorCode } from "../../src/protocol";
import { RpcError } from "../../src/rpc";
import type { OperationName, Operations } from "../../src/tools/api.gen";
import { ToolSet, type HikariCalls } from "../../src/tools/toolset";

type Answer = unknown | RpcError;

/** HA1 as the sidecar sees it: each operation answers from `answers`, and every call is kept. */
class FakeHa1 implements HikariCalls {
    calls: { operation: string; params: unknown }[] = [];
    constructor(public answers: Partial<Record<OperationName, Answer>> = {}) {}

    call<O extends OperationName>(operation: O, params?: Operations[O]["params"]): Promise<Operations[O]["result"]> {
        this.calls.push({ operation, params });
        const answer = this.answers[operation];
        if (answer instanceof RpcError)
            return Promise.reject(answer);
        return Promise.resolve(answer as Operations[O]["result"]);
    }
}

function ha1Error(code: string, message: string, data?: unknown): RpcError {
    return new RpcError(ErrorCode.hikariApi, message, data === undefined ? { code } : { code, data });
}

function textOf(result: { content: { type: string; text?: string }[] }): string {
    return result.content.map((c) => c.text ?? "").join("");
}

describe("ToolSet", () => {
    test("offers the core tools until search_tools finds more", async () => {
        const tools = new ToolSet(new FakeHa1());
        const offered = tools.list().map((t) => t.name);
        expect(offered).toContain("document_apply");
        expect(offered).not.toContain("video_frame");
        expect(tools.list(true).map((t) => t.name)).toContain("video_frame");

        const found = await tools.call("search_tools", { query: "video frame image" });
        expect(found.isError).toBeUndefined();
        const names = JSON.parse(textOf(found)).tools.map((t: { name: string }) => t.name);
        expect(names[0]).toBe("video_frame");
        expect(tools.list().map((t) => t.name)).toContain("video_frame");

        const app = JSON.parse(textOf(await tools.call("search_tools", { group: "app" }))).tools;
        expect(app.map((t: { name: string }) => t.name).sort()).toEqual(["panels_close", "panels_focus", "panels_list",
            "panels_open", "settings_get", "settings_list", "settings_set"]);
        expect(app[0].inputSchema.type).toBe("object");
    });

    test("search_tools refuses what it can't search, in words the model can act on", async () => {
        const tools = new ToolSet(new FakeHa1());
        expect(textOf(await tools.call("search_tools", {}))).toBe("search_tools needs a query, a group or both.");
        const core = await tools.call("search_tools", { group: "core" });
        expect(core.isError).toBe(true);
        expect(textOf(core)).toContain("typeset");
        expect((await tools.call("search_tools", { query: "zzz" })).isError).toBe(true);
    });

    test("runs a tool as its HA1 operation and answers HA1's result as JSON", async () => {
        const ha1 = new FakeHa1({ "lines.find": { lines: [{ id: 3, row: 3, text: "General Kenobi" }], total: 1 } });
        const tools = new ToolSet(ha1);
        const result = await tools.call("lines_find", { text: "Kenobi", fields: ["text"] });
        expect(ha1.calls).toEqual([{ operation: "lines.find", params: { text: "Kenobi", fields: ["text"] } }]);
        expect(result).toEqual({ content: [{ type: "text", text: '{"lines":[{"id":3,"row":3,"text":"General Kenobi"}],"total":1}' }] });

        await tools.call("documents_list", undefined);
        expect(ha1.calls[1]).toEqual({ operation: "documents.list", params: {} });
        await tools.call("run_command", { command: "GLOBAL_SAVE_SUBS" });
        expect(ha1.calls[2]).toEqual({ operation: "commands.run", params: { command: "GLOBAL_SAVE_SUBS" } });
    });

    test("words HA1's refusals for the model, with what to do next", async () => {
        const ha1 = new FakeHa1({
            "document.apply": ha1Error("stale_revision", "The Document changed since that revision; read it again.",
                { current: 5 }),
            "lines.get": ha1Error("not_granted",
                "Only the active Document and the attached ones are available without the documents.all grant.",
                { document: 2 }),
            "files.read": ha1Error("not_granted", "No file \"f9\" was granted to this caller; see files.list.", { file: "f9" }),
            "selection.set": ha1Error("invalid_params", "Parameter /selected: expected array.", { pointer: "/selected" }),
            "styles.list": new RpcError(ErrorCode.requestCancelled, "cancelled"),
            "info.list": new RpcError(ErrorCode.internalError, "stdin closed"),
        });
        const tools = new ToolSet(ha1);
        const stale = await tools.call("document_apply", { revision: 1, name: "Fix", edits: [] });
        expect(stale.isError).toBe(true);
        expect(textOf(stale)).toBe("The Document changed since that revision; read it again. It is now at revision 5. " +
            "Read the Lines again and prepare the edits against the current revision, which documents_get gives.");
        expect(textOf(await tools.call("lines_get", { document: 2, ids: [1] }))).toBe("Only the active Document and " +
            "the attached ones are available without the documents.all grant. Work on the active Document, or ask the " +
            "user to attach this one with @.");
        expect(textOf(await tools.call("files_read", { file: "f9" }))).toBe("No file \"f9\" was granted to this caller; " +
            "see files_list.");
        expect(textOf(await tools.call("selection_set", { selected: 1 }))).toBe("Parameter /selected: expected array. " +
            "Check the tool's parameters and call it again.");
        expect(textOf(await tools.call("styles_list", {}))).toBe("styles_list was cancelled.");
        expect(textOf(await tools.call("info_list", {}))).toBe("HikariSub couldn't run info_list: stdin closed");
    });

    test("refuses an unknown tool and arguments that aren't an object without calling HA1", async () => {
        const ha1 = new FakeHa1();
        const tools = new ToolSet(ha1);
        const unknown = await tools.call("delete_everything", {});
        expect(unknown).toEqual({ content: [{ type: "text", text: "No tool is named \"delete_everything\". search_tools finds more tools." }], isError: true });
        expect(textOf(await tools.call("lines_get", [1, 2]))).toBe("lines_get takes a JSON object of arguments.");
        expect(ha1.calls).toEqual([]);
    });

    test("search_commands matches every word in a command's id or name", async () => {
        const command = (id: string, name: string, enabled = true) => ({ command: id, name, window: "global", enabled, keys: [] });
        const ha1 = new FakeHa1({
            "commands.list": {
                commands: [command("GLOBAL_SAVE_SUBS", "Save subtitles"), command("GLOBAL_SAVE_SUBS_AS", "Save subtitles as"),
                    command("GLOBAL_OPEN_SUBS", "Open subtitles"), command("GLOBAL_UNDO", "Undo", false)],
            },
        });
        const tools = new ToolSet(ha1);
        const saved = JSON.parse(textOf(await tools.call("search_commands", { query: "save subs", limit: 1 })));
        expect(saved).toEqual({ commands: [command("GLOBAL_SAVE_SUBS", "Save subtitles")], total: 2 });
        expect(ha1.calls[0]).toEqual({ operation: "commands.list", params: {} });
        await tools.call("search_commands", { query: "undo", window: "global" });
        expect(ha1.calls[1]).toEqual({ operation: "commands.list", params: { window: "global" } });
        expect((await tools.call("search_commands", { query: " " })).isError).toBe(true);
        expect((await tools.call("search_commands", { query: "save", limit: 0 })).isError).toBe(true);
    });

    test("video_frame answers a PNG of HA1's BGRA pixels", async () => {
        const bgra = Uint8Array.of(10, 20, 30, 255, 40, 50, 60, 255, 70, 80, 90, 255, 1, 2, 3, 0);
        const ha1 = new FakeHa1({ "video.frame": { frame: 7, width: 2, height: 2, format: "bgra", data: Buffer.from(bgra).toString("base64") } });
        const result = await new ToolSet(ha1).call("video_frame", { maxEdge: 64 });
        expect(result.content[0]).toEqual({ type: "text", text: '{"frame":7,"width":2,"height":2}' });
        const image = result.content[1] as { type: string; data: string; mimeType: string };
        expect(image.type).toBe("image");
        expect(image.mimeType).toBe("image/png");
        const png = Buffer.from(image.data, "base64");
        expect([...png.subarray(0, 8)]).toEqual([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);
        expect(png.readUInt32BE(16)).toBe(2);
        expect(png.readUInt32BE(20)).toBe(2);
        const idat = png.indexOf("IDAT");
        const pixels = inflateSync(png.subarray(idat + 4, idat + 4 + png.readUInt32BE(idat - 4)));
        expect([...pixels]).toEqual([0, 30, 20, 10, 60, 50, 40, 0, 90, 80, 70, 3, 2, 1]);
    });
});
