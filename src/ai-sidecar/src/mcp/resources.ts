import { ErrorCode } from "../protocol";
import { RpcError } from "../rpc";
import type { DocumentsListResult, Line } from "../tools/api.gen";
import type { HikariCalls } from "../tools/toolset";
import { McpError } from "./errors";

export interface Resource {
    uri: string;
    name: string;
    title?: string;
    description?: string;
    mimeType: string;
}

export interface ResourceTemplate {
    uriTemplate: string;
    name: string;
    title?: string;
    description?: string;
    mimeType: string;
}

export interface ResourceContents {
    uri: string;
    mimeType: string;
    text: string;
}

type Part = "lines" | "styles" | "info";

const kJson = "application/json";
const kRoot = "hikari://documents";
const kPageSize = 5000;
const kParts: { part: Part; title: string; description: string }[] = [
    { part: "lines", title: "Lines", description: "Every Line, with its id, row and fields." },
    { part: "styles", title: "Styles", description: "The Document's styles." },
    { part: "info", title: "Script Info", description: "The Document's Script Info properties, in order." },
];

function isObject(value: unknown): value is Record<string, unknown> {
    return typeof value === "object" && value !== null && !Array.isArray(value);
}

function notFound(uri: string): RpcError {
    return new RpcError(McpError.invalidParams, "Resource not found", { uri });
}

/** The open Documents' state as MCP resources, read through HA1. */
export class Resources {
    constructor(private readonly api: HikariCalls) {}

    async list(): Promise<{ resources: Resource[] }> {
        const listed = await this.api.call("documents.list");
        const resources: Resource[] = [{ uri: kRoot, name: "documents", title: "Open Documents",
            description: "The Documents open in HikariSub.", mimeType: kJson }];
        for (const document of listed.documents) {
            if (document.document === undefined)
                continue;
            const base = `${kRoot}/${document.document}`;
            const title = document.title ?? `Document ${document.document}`;
            resources.push({ uri: base, name: `document-${document.document}`, title,
                description: "The Document's file, format, revision and counts.", mimeType: kJson });
            for (const { part, title: partTitle, description } of kParts)
                resources.push({ uri: `${base}/${part}`, name: `document-${document.document}-${part}`,
                    title: `${title}: ${partTitle}`, description, mimeType: kJson });
        }
        return { resources };
    }

    templates(): { resourceTemplates: ResourceTemplate[] } {
        return {
            resourceTemplates: [
                { uriTemplate: `${kRoot}/{document}`, name: "document", title: "Document",
                    description: "A Document's file, format, revision and counts.", mimeType: kJson },
                ...kParts.map(({ part, title, description }) => ({
                    uriTemplate: `${kRoot}/{document}/${part}`, name: `document-${part}`, title, description,
                    mimeType: kJson })),
            ],
        };
    }

    async read(uri: string): Promise<{ contents: ResourceContents[] }> {
        const match = /^hikari:\/\/documents(?:\/(\d{1,15})(?:\/(lines|styles|info))?)?$/.exec(uri);
        if (!match)
            throw notFound(uri);
        const document = match[1] === undefined ? undefined : Number(match[1]);
        const part = match[2] as Part | undefined;
        try {
            const value = await this.value(document, part);
            return { contents: [{ uri, mimeType: kJson, text: JSON.stringify(value) }] };
        } catch (error) {
            if (error instanceof RpcError && error.code === ErrorCode.hikariApi && isObject(error.data)
                && (error.data.code === "not_found" || error.data.code === "not_granted"))
                throw notFound(uri);
            throw error;
        }
    }

    private value(document: number | undefined, part: Part | undefined): Promise<unknown> {
        if (document === undefined)
            return this.api.call("documents.list") as Promise<DocumentsListResult>;
        switch (part) {
        case undefined: return this.api.call("documents.get", { document });
        case "lines": return this.lines(document);
        case "styles": return this.api.call("styles.list", { document });
        case "info": return this.api.call("info.list", { document });
        }
    }

    /** Every Line at one revision; HA1 answers at most 5000 at a time. */
    private async lines(document: number): Promise<{ document: number; revision: number; lines: Line[] }> {
        for (let attempt = 0; attempt < 3; ++attempt) {
            const first = await this.api.call("lines.list", { document, limit: kPageSize });
            const lines = [...first.lines];
            let changed = false;
            while (!changed && first.total !== undefined && lines.length < first.total) {
                const page = await this.api.call("lines.list", { document, offset: lines.length, limit: kPageSize });
                changed = page.revision !== first.revision || page.lines.length === 0;
                lines.push(...page.lines);
            }
            if (!changed)
                return { document, revision: first.revision, lines };
        }
        throw new RpcError(McpError.internalError, "The Document kept changing while it was read; read it again.");
    }
}
