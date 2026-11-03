import { lookup } from "node:dns/promises";
import { isIP } from "node:net";
import { networkInterfaces } from "node:os";

import { errorDetail, kindOfStatus, ProviderError } from "./errors";

/** What an SDK gets in place of an absent key; the egress fetch drops any header that carries it. */
export const kNoKey = "hikari-no-key";

export interface Network {
    resolve(host: string): Promise<string[]>;
    localAddresses(): string[];
}

export const kSystemNetwork: Network = {
    async resolve(host) {
        try {
            return (await lookup(host, { all: true, verbatim: true })).map((a) => a.address);
        } catch {
            return [];
        }
    },
    localAddresses() {
        return Object.values(networkInterfaces()).flatMap((list) => (list ?? []).map((a) => a.address));
    },
};

type Bytes = number[];

function ipv4Bytes(text: string): Bytes | undefined {
    const parts = text.split(".");
    if (parts.length !== 4 || !parts.every((p) => /^\d{1,3}$/.test(p) && Number(p) < 256))
        return undefined;
    return parts.map(Number);
}

function ipv6Bytes(text: string): Bytes | undefined {
    let tail: Bytes = [];
    let head = text;
    const lastColon = text.lastIndexOf(":");
    if (text.includes(".", lastColon)) {
        const v4 = ipv4Bytes(text.slice(lastColon + 1));
        if (!v4)
            return undefined;
        tail = v4;
        head = text.slice(0, lastColon + 1) + "0:0";
    }
    const halves = head.split("::");
    if (halves.length > 2)
        return undefined;
    const groups = (half: string) => (half === "" ? [] : half.split(":"));
    const left = groups(halves[0]!);
    const right = halves.length === 2 ? groups(halves[1]!) : [];
    const missing = 8 - left.length - right.length;
    if ((halves.length === 2 && missing < 1) || (halves.length === 1 && missing !== 0))
        return undefined;
    const all = [...left, ...Array<string>(halves.length === 2 ? missing : 0).fill("0"), ...right];
    if (!all.every((g) => /^[0-9a-f]{1,4}$/i.test(g)))
        return undefined;
    const bytes = all.flatMap((g) => [parseInt(g, 16) >> 8, parseInt(g, 16) & 0xff]);
    if (tail.length)
        bytes.splice(12, 4, ...tail);
    return bytes;
}

/** An address as bytes, with an IPv4-mapped IPv6 address unmapped to IPv4. */
function addressBytes(address: string): Bytes | undefined {
    const bare = address.replace(/^\[|\]$/g, "").replace(/%.*$/, "");
    if (isIP(bare) === 4)
        return ipv4Bytes(bare);
    const v6 = isIP(bare) === 6 ? ipv6Bytes(bare) : undefined;
    if (v6 && v6.slice(0, 10).every((b) => b === 0) && v6[10] === 0xff && v6[11] === 0xff)
        return v6.slice(12);
    return v6;
}

function inRange(bytes: Bytes, range: { prefix: Bytes; bits: number }): boolean {
    if (bytes.length !== range.prefix.length)
        return false;
    for (let bit = 0; bit < range.bits; ++bit) {
        const mask = 0x80 >> (bit % 8);
        if ((bytes[bit >> 3]! & mask) !== (range.prefix[bit >> 3]! & mask))
            return false;
    }
    return true;
}

const kTailnets = [
    { prefix: ipv4Bytes("100.64.0.0")!, bits: 10 },
    { prefix: ipv6Bytes("fd7a:115c:a1e0::")!, bits: 48 },
];

/** Loopback, or a tailnet address while this machine has an address in the same tailnet range. */
export function keySafeOverPlainHttp(address: string, localAddresses: string[]): boolean {
    const a = addressBytes(address);
    if (!a)
        return false;
    if ((a.length === 4 && a[0] === 127) || (a.length === 16 && a.slice(0, 15).every((b) => b === 0) && a[15] === 1))
        return true;
    return kTailnets.some((range) => inRange(a, range) &&
        localAddresses.some((local) => {
            const l = addressBytes(local);
            return l !== undefined && inRange(l, range);
        }));
}

export interface Route {
    url: URL;
    /** The Host header for a URL pinned to an address; empty when the URL is used as is. */
    host: string;
}

/** The URL with its host replaced by the first resolved address a key may travel to over plain HTTP. */
export function pinnedRoute(url: URL, resolved: string[], localAddresses: string[]): Route | undefined {
    const safe = resolved.find((address) => keySafeOverPlainHttp(address, localAddresses));
    if (safe === undefined)
        return undefined;
    const pinned = new URL(url);
    pinned.hostname = isIP(safe) === 6 ? `[${safe}]` : safe;
    return { url: pinned, host: url.host };
}

async function route(url: URL, hasKey: boolean, network: Network): Promise<Route> {
    if (url.protocol !== "https:" && url.protocol !== "http:")
        throw new ProviderError("rejected", "invalid endpoint");
    const host = url.hostname.replace(/^\[|\]$/g, "");
    if (!hasKey || url.protocol === "https:" || host.toLowerCase() === "localhost")
        return { url, host: "" };
    if (isIP(host)) {
        if (!keySafeOverPlainHttp(host, network.localAddresses()))
            throw new ProviderError("insecureKey");
        return { url, host: "" };
    }
    const resolved = await network.resolve(host);
    if (resolved.length === 0)
        throw new ProviderError("network", "host not found");
    const pinned = pinnedRoute(url, resolved, network.localAddresses());
    if (!pinned)
        throw new ProviderError("insecureKey");
    return pinned;
}

function withinBase(url: URL, base: URL): boolean {
    if (url.origin !== base.origin)
        return false;
    const prefix = base.pathname.replace(/\/+$/, "");
    return url.pathname === prefix || url.pathname.startsWith(`${prefix}/`) || prefix === "";
}

/**
 * The only way out for an SDK client: requests stay under the provider's base URL, never follow a redirect,
 * and a key travels over plain HTTP only to this machine or a tailnet, at the address that was checked.
 * A response that isn't 2xx fails with the provider's own message.
 */
export function egressFetch(baseUrl: string, hasKey: boolean, network: Network = kSystemNetwork): typeof fetch {
    let base: URL;
    try {
        base = new URL(baseUrl);
    } catch {
        base = new URL("invalid:");
    }
    const egress = async (input: string | URL | Request, init?: RequestInit): Promise<Response> => {
        const url = new URL(input instanceof Request ? input.url : input.toString());
        if (base.protocol === "invalid:" || !withinBase(url, base))
            throw new ProviderError("rejected", "invalid endpoint");
        const headers = new Headers(input instanceof Request ? input.headers : undefined);
        new Headers(init?.headers).forEach((value, name) => headers.set(name, value));
        for (const [name, value] of [...headers])
            if (value.includes(kNoKey))
                headers.delete(name);
        const target = await route(url, hasKey, network);
        if (target.host)
            headers.set("host", target.host);
        let response: Response;
        try {
            response = await fetch(target.url, { ...init, headers, redirect: "manual" });
        } catch (error) {
            if (init?.signal?.aborted)
                throw init.signal.reason ?? error;
            throw new ProviderError("network", error instanceof Error ? error.message : String(error));
        }
        if (response.status >= 300) {
            const body = await response.text().catch(() => "");
            throw new ProviderError(kindOfStatus(response.status), errorDetail(body), response.status);
        }
        return response;
    };
    return egress as typeof fetch;
}
