"""Generate the diagnostic view from recorded native PPM output; no native rerun."""
import base64
import hashlib
import html
import json
from pathlib import Path
import struct
import zlib

HERE = Path(__file__).resolve().parent


def png(path):
    data = path.read_bytes().split(b'\n', 3)[3]
    def chunk(tag, payload):
        return struct.pack('>I',len(payload))+tag+payload+struct.pack('>I',zlib.crc32(tag+payload)&0xffffffff)
    raw = b''.join(b'\0'+data[y*2400:(y+1)*2400] for y in range(200))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',800,200,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')


def main():
    observations = HERE/'observations.json'
    d = json.loads(observations.read_text(encoding='utf-8'))
    run = Path(d['run_directory'])
    renders = {r['case']:r for r in d['records'] if r['event']=='render'}
    cards = []
    for case,description in [
        ('full_directwrite','Original system DirectWrite frame'),
        ('full_none','Captured bytes, provider NONE — still incomplete'),
        ('full_directwrite_captured','Captured bytes + same-host DirectWrite — original pixels'),
        ('cjk_none','Single CJK character, NONE/Arial — missing fallback'),
        ('cjk_none_default_yu','Only default family changed — original isolated glyph'),
        ('emoji_none','Single emoji, NONE/Arial — missing fallback'),
        ('emoji_none_default_segoe','Only default family changed — original isolated glyph'),
        ('full_none_authored_families','Synthetic explicit font tags — different full pixels')]:
        r = renders[case]
        path = run/'output'/r['file']
        if hashlib.sha256(path.read_bytes()).hexdigest()!=r['sha256']:
            raise RuntimeError('Recorded PPM hash mismatch: '+case)
        events = [e for e in d['records'] if e['case']==case and e['event'] in ('selected_v1','opened_face_v1')]
        source = base64.b64encode(png(path)).decode()
        cards.append(f'<article><h2>{html.escape(description)}</h2><code>{case}</code><img alt="Recorded libass output for {case}" src="data:image/png;base64,{source}"><p class="hash">PPM SHA-256 {r["sha256"]}</p><details><summary>Captured identity evidence</summary><pre>{html.escape(json.dumps(events,ensure_ascii=False,indent=2))}</pre></details></article>')
    comparisons = []
    for case,agree in d['provider_pair_same_pixels'].items():
        comparisons.append(f'<tr><td>{case}</td><td>DirectWrite versus NONE</td><td>{"same" if agree else "different"}</td></tr>')
    for case,agree in d['variation_same_pixels'].items():
        comparisons.append(f'<tr><td>{case}</td><td>{d["variation_reference_cases"][case]}</td><td>{"same" if agree else "different"}</td></tr>')
    page='''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Font fallback replay diagnosis</title><style>
body{margin:0;background:#171a20;color:#e8edf4;font:16px/1.55 system-ui,sans-serif}main{max-width:1050px;padding:32px;margin:auto}h1{line-height:1.2}a{color:#9bbdff}article{background:#232830;border:1px solid #495365;padding:18px;margin:20px 0;border-radius:8px}img{display:block;width:min(800px,100%);height:auto;margin-top:14px}code,.hash{overflow-wrap:anywhere}pre{overflow:auto;max-height:460px;font-size:12px}table{border-collapse:collapse;width:100%;font-size:13px}td,th{border-bottom:1px solid #495365;padding:8px;text-align:left}.callout{border-left:5px solid #eeb75e;padding:14px 20px;background:#332d24}h2{font-size:20px}</style><main><h1>Captured fonts are usable.<br>Provider NONE lacks the fallback policy.</h1>
<p class="callout"><strong>The original 17/18 result remains incomplete.</strong> This diagnosis explains the multilingual failure; it does not certify portable collection or replace the original evidence.</p>
<p>The original mismatch reproduces with a single CJK character or emoji. Configuring the appropriate captured fallback family restores each isolated glyph. Same-host DirectWrite restores the full frame, while reversed attachment order does not. Explicit font tags select the expected identities but still change full pixels.</p>
<p><a href="README.md">Diagnosis, pinned source explanation and reproduction</a> · <a href="observations.json">Exact native observations</a> · <a href="../evidence/report.html">Preserved original report</a></p>'''
    page+=f'<p>Capture: {d["utc"]}. Nineteen cases repeated twice; all repeat frame hashes agree: {d["repeat_all_frames_agree"]}. Original baseline hashes agree: {all(d["original_baseline_frames_agree"].values())}.</p>'
    page+='<table><tr><th>Case</th><th>Reference</th><th>Pixels</th></tr>'+''.join(comparisons)+'</table>'+''.join(cards)
    page+='<h2>Proof limits</h2><p>Real libass ASS_Image composites, not screenshots. Windows existing Debug archives and previously generated hook objects; no production resolver, Linux, whole-document, variable-instance, concurrency or performance proof. Same-host DirectWrite reproduction still depends on the installed provider environment. No user ASS names were changed; the tagged text is a separate original fixture.</p>'
    page+='<details><summary>Full captured observations</summary><pre>'+html.escape(json.dumps(d,ensure_ascii=False,indent=2))+'</pre></details>'
    page+='<p class="hash">Observations JSON SHA-256: '+hashlib.sha256(observations.read_bytes()).hexdigest()+'</p></main></html>'
    (HERE/'report.html').write_text(page,encoding='utf-8')
    print(HERE/'report.html')


if __name__=='__main__':
    main()
