import type { HostRequest, Json, ProgramCalls, Response } from "../../../types/hikari";

type Method = keyof ProgramCalls;

const waiting = new Map<number, (response: Response) => void>();
let nextId = 1;

function send(message: object): void {
    process.stdout.write(JSON.stringify(message) + "\n");
}

function call<M extends Method>(method: M, params: ProgramCalls[M]["params"]): Promise<ProgramCalls[M]["result"]> {
    const id = nextId++;
    send({ jsonrpc: "2.0", id, method, params });
    return new Promise((resolve, reject) => {
        waiting.set(id, (response) => {
            if ("error" in response)
                reject(new Error(response.error.message));
            else
                resolve(response.result as ProgramCalls[M]["result"]);
        });
    });
}

async function countWords(): Promise<number> {
    const { lines } = await call("lines.list", { fields: ["text"] });
    const words = lines.reduce((sum, line) => sum + (line.text ?? "").split(/\s+/).filter(Boolean).length, 0);
    await call("hikari/notify", { text: `${words} words in ${lines.length} lines` });
    return words;
}

async function answer(request: HostRequest): Promise<Json> {
    switch (request.method) {
    case "initialize":
        return {};
    case "command":
        return request.params?.command === "count" ? countWords() : null;
    default:
        return null;
    }
}

for await (const line of console) {
    if (!line.trim())
        continue;
    const message = JSON.parse(line);
    if ("method" in message && message.method === "exit")
        process.exit(0);
    if ("method" in message) {
        const request = message as HostRequest;
        answer(request).then(
            (result) => send({ jsonrpc: "2.0", id: request.id, result }),
            (error: Error) => send({ jsonrpc: "2.0", id: request.id, error: { code: -32000, message: error.message } }),
        );
    } else {
        waiting.get(message.id)?.(message as Response);
        waiting.delete(message.id);
    }
}
