import { EventEmitter } from "node:events";
import { PassThrough, Writable } from "node:stream";

import {
    Method,
    type AgentExitParams,
    type AgentOutputParams,
    type AgentProcessParams,
    type AgentStartParams,
    type AgentWriteParams,
} from "../protocol";
import type { Peer } from "../rpc";

export interface AgentSpawnOptions {
    args: string[];
    env: Record<string, string | undefined>;
    signal?: AbortSignal;
    session?: string;
}

function asError(error: unknown): Error {
    return error instanceof Error ? error : new Error(String(error));
}

/** An agent CLI Qt runs for the sidecar, shaped like the Agent SDK's `SpawnedProcess`. */
export class RelayedProcess extends EventEmitter {
    readonly stdin: Writable;
    readonly stdout = new PassThrough();
    readonly stderr = new PassThrough();
    killed = false;
    exitCode: number | null = null;
    signalCode: NodeJS.Signals | null = null;
    private ended = false;

    constructor(private readonly peer: Peer, readonly id: number) {
        super();
        this.stdin = new Writable({
            write: (chunk: Buffer, _encoding, done) =>
                this.call(Method.agentWrite, { process: id, data: chunk.toString("base64") } satisfies AgentWriteParams, done),
            final: (done) => this.call(Method.agentCloseInput, { process: id } satisfies AgentProcessParams, done),
        });
    }

    kill(_signal?: NodeJS.Signals): boolean {
        if (this.ended)
            return false;
        this.killed = true;
        this.peer.request(Method.agentKill, { process: this.id } satisfies AgentProcessParams).catch(() => {});
        return true;
    }

    output(stream: AgentOutputParams["stream"], data: Uint8Array): void {
        if (!this.ended)
            (stream === "stderr" ? this.stderr : this.stdout).write(data);
    }

    exited(code: number): void {
        if (!this.end())
            return;
        if (code >= 0)
            this.exitCode = code;
        else
            this.signalCode = "SIGKILL";
        this.emit("exit", this.exitCode, this.signalCode);
    }

    failed(error: Error): void {
        if (this.end() && this.listenerCount("error") > 0)
            this.emit("error", error);
    }

    private end(): boolean {
        if (this.ended)
            return false;
        this.ended = true;
        this.stdout.end();
        this.stderr.end();
        return true;
    }

    private call(method: string, params: unknown, done: (error?: Error | null) => void): void {
        if (this.ended)
            return done();
        this.peer.request(method, params).then(() => done(), (error: unknown) => done(this.ended ? null : asError(error)));
    }
}

/** Asks Qt to start the agents it knows and relays their stdio over the link. */
export class AgentRelay {
    private next = 1;
    private readonly running = new Map<number, RelayedProcess>();

    constructor(private readonly peer: Peer) {
        peer.onNotification(Method.agentOutput, (params) => {
            const p = params as Partial<AgentOutputParams>;
            if (typeof p.process === "number" && typeof p.data === "string")
                this.running.get(p.process)?.output(p.stream === "stderr" ? "stderr" : "stdout", Buffer.from(p.data, "base64"));
        });
        peer.onNotification(Method.agentExit, (params) => {
            const p = params as Partial<AgentExitParams>;
            if (typeof p.process !== "number" || typeof p.code !== "number")
                return;
            this.running.get(p.process)?.exited(p.code);
            this.running.delete(p.process);
        });
    }

    start(agent: string, options: AgentSpawnOptions): RelayedProcess {
        const child = new RelayedProcess(this.peer, this.next++);
        this.running.set(child.id, child);
        const env = Object.fromEntries(Object.entries(options.env).filter((e): e is [string, string] => typeof e[1] === "string"));
        const params: AgentStartParams = { process: child.id, agent, args: options.args, env,
            ...(options.session === undefined ? {} : { session: options.session }) };
        this.peer.request(Method.agentStart, params).catch((error: unknown) => {
            this.running.delete(child.id);
            child.failed(asError(error));
        });
        options.signal?.addEventListener("abort", () => child.kill("SIGKILL"), { once: true });
        return child;
    }
}
