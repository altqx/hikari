import json, subprocess, sys, time, os, select
exe, text = sys.argv[1], sys.argv[2]
cfg = json.loads(sys.argv[3]) if len(sys.argv) > 3 else {}
p = subprocess.Popen([exe, "--stdio"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
def send(m):
    b = json.dumps(m).encode(); p.stdin.write(b"Content-Length: %d\r\n\r\n" % len(b) + b); p.stdin.flush()
def recv(timeout):
    r, _, _ = select.select([p.stdout], [], [], timeout)
    if not r: return None
    hdr = b""
    while not hdr.endswith(b"\r\n\r\n"): hdr += p.stdout.read(1)
    n = int([l for l in hdr.split(b"\r\n") if l.lower().startswith(b"content-length")][0].split(b":")[1])
    return json.loads(p.stdout.read(n))
t0 = time.time()
send({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":os.getpid(),"rootUri":None,"capabilities":{"workspace":{"configuration":True},"general":{"positionEncodings":["utf-16"]}},"initializationOptions":cfg}})
uri = "untitled:line"
while True:
    m = recv(10)
    if m is None: print("timeout"); break
    if m.get("id") == 1 and "method" not in m and "error" in m: print(m); break
    if m.get("id") == 1 and "method" not in m:
        print("init", round(time.time()-t0,3), "s; encoding", m["result"]["capabilities"].get("positionEncoding"))
        send({"jsonrpc":"2.0","method":"initialized","params":{}})
        send({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":uri,"languageId":"plaintext","version":1,"text":text}}})
        t1 = time.time()
    elif m.get("method") == "workspace/configuration":
        send({"jsonrpc":"2.0","id":m["id"],"result":[cfg if cfg else {"harper-ls":{}}] * len(m["params"]["items"])})
    elif m.get("method") == "textDocument/publishDiagnostics":
        print("diag after", round(time.time()-t1,3), "s")
        for d in m["params"]["diagnostics"]:
            r = d["range"]; print(" ", d.get("code"), r["start"]["character"], r["end"]["character"], repr(text[r["start"]["character"]:r["end"]["character"]]), "-", d["message"][:70])
        send({"jsonrpc":"2.0","id":2,"method":"textDocument/codeAction","params":{"textDocument":{"uri":uri},"range":m["params"]["diagnostics"][0]["range"] if m["params"]["diagnostics"] else {"start":{"line":0,"character":0},"end":{"line":0,"character":0}},"context":{"diagnostics":m["params"]["diagnostics"][:1]}}})
    elif m.get("id") == 2 and "method" not in m:
        print("actions:", [a.get("title") for a in (m.get("result") or [])][:8]); break
    elif "method" in m and "id" in m:
        send({"jsonrpc":"2.0","id":m["id"],"result":None})
print(open(f"/proc/{p.pid}/status").read().split("VmRSS:")[1].split("\n")[0].strip()); p.kill()
print("rss check done")
