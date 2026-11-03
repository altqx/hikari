import type { Socket, TCPSocketListener } from "bun";

export interface Reply {
    status?: number;
    contentType?: string;
    chunks: string[];
    end?: "close" | "reset" | "hang";
}

export interface Received {
    method: string;
    path: string;
    headers: Record<string, string>;
    body: string;
    json(): Record<string, unknown>;
}

interface State {
    buffer: string;
}

/** A provider on 127.0.0.1 that answers each request with the next reply, one write per chunk. */
export class MockProvider {
    readonly replies: Reply[] = [];
    readonly received: Received[] = [];
    connections = 0;
    disconnects = 0;
    private readonly server: TCPSocketListener<State>;
    private readonly open = new Set<Socket<State>>();

    constructor() {
        this.server = Bun.listen<State>({
            hostname: "127.0.0.1",
            port: 0,
            socket: {
                open: (socket) => {
                    ++this.connections;
                    socket.data = { buffer: "" };
                    this.open.add(socket);
                },
                data: (socket, data) => this.read(socket, data.toString("latin1")),
                close: (socket) => {
                    ++this.disconnects;
                    this.open.delete(socket);
                },
                error: () => {},
            },
        });
    }

    get port(): number {
        return this.server.port;
    }

    get base(): string {
        return `http://127.0.0.1:${this.port}/v1`;
    }

    stop(): void {
        for (const socket of this.open)
            socket.terminate();
        this.server.stop(true);
    }

    private read(socket: Socket<State>, text: string): void {
        socket.data.buffer += text;
        const data = socket.data.buffer;
        const headerEnd = data.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;
        const lines = data.slice(0, headerEnd).split("\r\n");
        const [method = "", path = ""] = lines[0]!.split(" ");
        const headers: Record<string, string> = {};
        for (const line of lines.slice(1)) {
            const colon = line.indexOf(":");
            if (colon > 0)
                headers[line.slice(0, colon).trim().toLowerCase()] = line.slice(colon + 1).trim();
        }
        const length = Number(headers["content-length"] ?? 0);
        const bodyBytes = Buffer.from(data.slice(headerEnd + 4), "latin1");
        if (bodyBytes.length < length)
            return;
        const body = bodyBytes.subarray(0, length).toString("utf8");
        socket.data.buffer = "";
        this.received.push({ method, path, headers, body, json: () => JSON.parse(body) as Record<string, unknown> });
        const reply = this.replies.shift() ?? { status: 500, chunks: [] };
        socket.write(`HTTP/1.1 ${reply.status ?? 200} X\r\nContent-Type: ${reply.contentType ?? "text/event-stream"}\r\n` +
            "Connection: close\r\n\r\n");
        const queue = [...reply.chunks];
        const step = () => {
            if (!this.open.has(socket))
                return;
            const chunk = queue.shift();
            if (chunk !== undefined) {
                socket.write(chunk);
                setTimeout(step, 5);
                return;
            }
            if ((reply.end ?? "close") === "close")
                socket.end();
            else if (reply.end === "reset")
                socket.terminate();
        };
        step();
    }
}

export function sse(event: string, data: unknown): string {
    return `${event ? `event: ${event}\n` : ""}data: ${JSON.stringify(data)}\n\n`;
}

export const data = (o: unknown) => sse("", o);

/** Splits each chunk in two at an awkward byte, so lines arrive in pieces. */
export function ragged(chunks: string[]): string[] {
    return chunks.flatMap((c) => {
        const at = Math.floor(c.length / 3);
        return [c.slice(0, at), c.slice(at)];
    });
}

export async function waitFor(done: () => boolean, ms = 5000): Promise<boolean> {
    const start = Date.now();
    while (!done() && Date.now() - start < ms)
        await Bun.sleep(10);
    return done();
}
