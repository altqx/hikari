import { expect, test } from "bun:test";

import { egressFetch, keySafeOverPlainHttp, pinnedRoute } from "../../src/providers/egress";
import { Harness, kKey } from "./harness";
import { data } from "./mock";

const h = new Harness().install();

test("Tailscale ranges need a local address in the same range", () => {
    const lanOnly = ["192.168.1.10"];
    const onTailnet = ["192.168.1.10", "100.101.102.103", "fd7a:115c:a1e0::5"];
    const ipv4Tailnet = ["100.101.102.103"];
    const safe = (address: string, local: string[]) => keySafeOverPlainHttp(address, local);

    expect(safe("127.0.0.1", lanOnly)).toBe(true);
    expect(safe("::1", lanOnly)).toBe(true);
    expect(safe("100.64.0.1", onTailnet)).toBe(true);
    expect(safe("100.127.255.254", onTailnet)).toBe(true);
    expect(safe("::ffff:100.100.1.2", onTailnet)).toBe(true);
    expect(safe("100.100.1.2", lanOnly)).toBe(false);
    expect(safe("100.128.0.1", onTailnet)).toBe(false);
    expect(safe("100.63.255.255", onTailnet)).toBe(false);
    expect(safe("fd7a:115c:a1e0::1", onTailnet)).toBe(true);
    expect(safe("fd7a:115c:a1e0:ab12:4843:cd96:6258:b240", onTailnet)).toBe(true);
    expect(safe("fd7a:115c:a1e0::1", lanOnly)).toBe(false);
    expect(safe("fd7a:115c:a1e0::1", ipv4Tailnet)).toBe(false);
    expect(safe("fd7a:115c:a1e1::1", onTailnet)).toBe(false);
    expect(safe("192.168.1.20", onTailnet)).toBe(false);
    expect(safe("10.0.0.5", onTailnet)).toBe(false);
    expect(safe("203.0.113.9", onTailnet)).toBe(false);
});

test("a key travels only over HTTPS, to this machine or to a tailnet", async () => {
    const attempt = async (base: string) => (await h.stream("chat-completions", undefined, { baseUrl: base })).error?.kind;
    expect(await attempt("http://192.168.1.20:8000/v1")).toBe("insecureKey");
    expect(await attempt("http://100.100.1.2:8000/v1")).toBe("insecureKey");
    h.names.set("box.lan", ["192.168.1.20"]);
    expect(await attempt("http://box.lan:8000/v1")).toBe("insecureKey");
    expect(await attempt("http://nowhere.example/v1")).toBe("network");

    const models = await h.models("chat-completions", "http://192.168.1.20/v1");
    expect(models).toMatchObject({ kind: "insecureKey" });
    expect(h.mock.connections).toBe(0);
});

test("a keyless provider sends no key header and may use plain HTTP anywhere", async () => {
    h.mock.replies.push({ chunks: [data({ choices: [{ delta: { content: "ok" }, finish_reason: "stop" }] }), "data: [DONE]\n\n"] });
    const run = await h.stream("chat-completions", undefined, { key: "" });
    expect(run.text()).toBe("ok");
    expect(h.mock.received[0]!.headers.authorization).toBeUndefined();
    expect(h.resolved).toEqual([]);
});

test("a name is resolved once and the checked address is used", async () => {
    h.names.set("peer.example", ["192.168.1.20", "127.0.0.1"]);
    h.mock.replies.push({ chunks: [data({ choices: [{ delta: { content: "ok" }, finish_reason: "stop" }] }), "data: [DONE]\n\n"] });
    const run = await h.stream("chat-completions", undefined, { baseUrl: `http://peer.example:${h.mock.port}/v1` });
    expect(run.text()).toBe("ok");
    expect(h.resolved).toEqual(["peer.example"]);
    expect(h.mock.received).toHaveLength(1);
    expect(h.mock.received[0]!.headers.host).toBe(`peer.example:${h.mock.port}`);
});

test("a MagicDNS name resolving outside the tailnet is refused", async () => {
    h.localAddresses.push("100.101.102.103");
    h.names.set("gpu-box.tail1234.ts.net", ["203.0.113.9"]);
    const run = await h.stream("chat-completions", undefined, { baseUrl: "http://gpu-box.tail1234.ts.net:8000/v1" });
    expect(run.error!.kind).toBe("insecureKey");
    expect(h.resolved).toEqual(["gpu-box.tail1234.ts.net"]);
    expect(h.mock.connections).toBe(0);
});

test("a MagicDNS name resolving into the tailnet is pinned to that address", () => {
    const url = new URL("http://gpu-box.tail1234.ts.net:8000/v1");
    const route = pinnedRoute(url, ["203.0.113.9", "100.100.1.2"], ["100.101.102.103"]);
    expect(route?.url.toString()).toBe("http://100.100.1.2:8000/v1");
    expect(route?.host).toBe("gpu-box.tail1234.ts.net:8000");

    const v6 = pinnedRoute(url, ["fd7a:115c:a1e0::9"], ["fd7a:115c:a1e0::5"]);
    expect(v6?.url.toString()).toBe("http://[fd7a:115c:a1e0::9]:8000/v1");

    expect(pinnedRoute(url, ["203.0.113.9", "100.100.1.2"], ["192.168.1.10"])).toBeUndefined();
});

test("requests stay under the provider's base URL and never follow a redirect", async () => {
    h.mock.replies.push({ status: 302, contentType: "text/plain\r\nLocation: http://127.0.0.1:1/elsewhere", chunks: [] });
    const run = await h.stream("chat-completions");
    expect(run.error).toMatchObject({ kind: "server", httpStatus: 302 });
    expect(h.mock.received).toHaveLength(1);
    expect(h.mock.received[0]!.headers.authorization).toBe(`Bearer ${kKey}`);

    const egress = egressFetch(h.mock.base, true, h.network);
    for (const outside of [`http://127.0.0.1:${h.mock.port}/v2/models`, `http://127.0.0.1:${h.mock.port}/v1x`, "https://example.com/v1/models"])
        await expect(egress(outside)).rejects.toMatchObject({ kind: "rejected" });
    expect(h.mock.received).toHaveLength(1);
});
