"""Run the native throwaway probe using existing Windows build dependencies.

python run.py --source C:/Work/Kainote
Only this prototype's _run/ and evidence/ directories are written.
"""
import argparse
import hashlib
import html
import json
import os
import platform
import struct
import subprocess
import zlib
from datetime import datetime, timezone
from pathlib import Path
from fixtures import generate

HERE = Path(__file__).resolve().parent
PIN = '4a05d8127f525943ebf45fdc6497c9e665947f0d'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def command(args, **kw):
    result = subprocess.run([str(x) for x in args] if isinstance(args,list) else args,
                            capture_output=True, text=True, encoding='utf-8', errors='replace', **kw)
    if result.returncode:
        raise RuntimeError(f'Command failed ({result.returncode}): {args}\n{result.stdout}\n{result.stderr}')
    return result.stdout


def build(source, folder):
    libass = source/'Thirdparty/libass'
    head = command(['git', '-C', libass, 'rev-parse', 'HEAD']).strip()
    if head != PIN or command(['git','-C',libass,'status','--porcelain']).strip():
        raise SystemExit('Expected pristine pinned libass source '+PIN)
    vswhere = Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
    vs = Path(command([vswhere,'-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
                       '-property','installationPath']).strip())
    vcvars = vs/'VC/Auxiliary/Build/vcvars64.bat'
    # This command only reads the compiler environment. File writes use direct argv.
    environment = command(f'cmd.exe /d /s /c ""{vcvars}" >nul && set"')
    env = os.environ.copy()
    env.update(line.split('=',1) for line in environment.splitlines() if '=' in line and not line.startswith('='))
    env = {k.upper():v for k,v in env.items()}
    cl = next(Path(p)/'cl.exe' for p in env['PATH'].split(';') if (Path(p)/'cl.exe').exists())
    includes = [libass/'libass',source/'Thirdparty/Build/Libass',source/'Thirdparty/FreeType2/include',
                source/'Thirdparty/harfbuzz/src',source/'Thirdparty/Build/Fribidi',source/'Thirdparty/fribidi/lib']
    inc = ['/I'+str(p) for p in includes]
    original = (libass/'libass/ass_fontselect.c').read_bytes()
    text = original.decode('utf-8').replace('\r\n','\n')
    declaration = '''
/* Hikari throwaway diagnostic v1; not a selected production ABI. */
extern void hikari_font_probe_v1(const char *, const char *, int, int,
    uint32_t, unsigned, unsigned, const char *, GetDataFunc, void *);
'''
    anchor = 'static char *\nfind_font('
    if text.count(anchor) != 1:
        raise SystemExit('Pinned selector declaration anchor changed')
    text = text.replace(anchor,declaration+'\n'+anchor)
    anchor = '''        } else
            result = selected->path;

    }

    return result;
}'''
    hook = '''        } else
            result = selected->path;

        hikari_font_probe_v1(result, selected->path, *index, *uid, code,
            bold, italic, *postscript_name,
            selected->path ? NULL : provider->funcs.get_data, selected->priv);
    }

    return result;
}'''
    if text.count(anchor) != 1:
        raise SystemExit('Pinned selector hook anchor changed')
    text = text.replace(anchor,hook)
    patched = folder/'ass_fontselect_probe.c'
    patched.write_text(text, encoding='utf-8', newline='\n')
    stock_copy = folder/'ass_fontselect_stock.c'
    stock_copy.write_bytes(original)
    font_original = (libass/'libass/ass_font.c').read_bytes()
    font_text = font_original.decode('utf-8').replace('\r\n','\n')
    anchor='static int add_face(ASS_FontSelector *fontsel, ASS_Font *font, uint32_t ch)'
    font_text=font_text.replace(anchor,'''extern void hikari_font_face_v1(int, long, long, const char *);
extern void hikari_font_simulation_v1(int, int, int, int);

'''+anchor)
    anchor='''    if (!face)
        return -1;

    ass_charmap_magic(font->library, face);'''
    if font_text.count(anchor)!=1:
        raise SystemExit('Pinned face-open anchor changed')
    font_text=font_text.replace(anchor,'''    if (!face)
        return -1;

    hikari_font_face_v1(uid, face->face_index, face->num_faces, FT_Get_Postscript_Name(face));
    ass_charmap_magic(font->library, face);''')
    anchor='''        ass_glyph_embolden(face->glyph);
    return true;'''
    if font_text.count(anchor)!=1:
        raise SystemExit('Pinned simulation anchor changed')
    font_text=font_text.replace(anchor,'''        ass_glyph_embolden(face->glyph);
    hikari_font_simulation_v1(font->faces_uid[face_index], index,
        !(style_flags & FT_STYLE_FLAG_BOLD) && font->desc.bold > ass_face_get_weight(face) + 150,
        !(style_flags & FT_STYLE_FLAG_ITALIC) && font->desc.italic > 55);
    return true;''')
    font_patched=folder/'ass_font_probe.c'
    font_patched.write_text(font_text,encoding='utf-8',newline='\n')
    font_stock=folder/'ass_font_stock.c';font_stock.write_bytes(font_original)
    logs = []
    for mode, src in [('stock',stock_copy),('hook',patched)]:
        # Recompile the same exact source in both builds. The hook build adds only
        # the diagnostic call above; the supplied library supplies other objects.
        obj = folder/(mode+'_selector.obj')
        args = [cl,'/nologo','/c','/TC','/std:c17','/MDd','/Od','/Dinline=__inline','/DFRIBIDI_ENTRY=',
                '/DHB_USE_ATEXIT','/D_MBCS',*inc,src,'/Fo'+str(obj),'/Fd'+str(folder/(mode+'.pdb'))]
        logs.append(command(args,env=env,cwd=folder))
        face_obj=folder/(mode+'_font.obj')
        face_src=font_stock if mode=='stock' else font_patched
        args=[cl,'/nologo','/c','/TC','/std:c17','/MDd','/Od','/Dinline=__inline','/DFRIBIDI_ENTRY=',
              '/DHB_USE_ATEXIT','/D_MBCS',*inc,face_src,'/Fo'+str(face_obj),'/Fd'+str(folder/(mode+'_font.pdb'))]
        logs.append(command(args,env=env,cwd=folder))
        args = [cl,'/nologo','/std:c++17','/EHsc','/utf-8','/MDd','/Od',*inc,HERE/'probe.cpp',obj,face_obj,
                '/Fo'+str(folder/(mode+'_probe.obj')),'/Fd'+str(folder/(mode+'_probe.pdb')),
                '/Fe'+str(folder/(mode+'.exe')),'/link','/LIBPATH:'+str(source/'bin/x64/Debug'),
                'Libass.lib','FreeType2.lib','Fribidi.lib','HarfBuzz.lib','Zlib.lib',
                'dwrite.lib','gdi32.lib','user32.lib','ole32.lib','advapi32.lib','usp10.lib',
                '/PDB:'+str(folder/(mode+'_link.pdb'))]
        logs.append(command(args,env=env,cwd=folder))
    (folder/'build.log').write_text('\n'.join(logs),encoding='utf-8')
    inputs = [source/'bin/x64/Debug'/name for name in ['Libass.lib','FreeType2.lib','Fribidi.lib','HarfBuzz.lib','Zlib.lib']]
    return {'libass_source_commit':head,'stock_selector_sha256':digest(original),
            'modified_selector_sha256':digest(patched.read_bytes()),
            'stock_face_source_sha256':digest(font_original),'modified_face_source_sha256':digest(font_patched.read_bytes()),
            'compiler':str(cl),'compiler_file_version_note':'Compiler path is exact installed toolset; build.log retained locally.',
            'build_config_sha256':digest((source/'Thirdparty/Build/Libass/config.h').read_bytes()),
            'libraries':{p.name:{'sha256':digest(p.read_bytes()),'bytes':p.stat().st_size} for p in inputs},
            'limitation':'Prebuilt dependencies are hashed, not rebuilt or reproducibly attested to their source pins.'}


def png_from_ppm(path):
    data = path.read_bytes().split(b'\n',3)[3]
    def chunk(tag, data):
        return struct.pack('>I',len(data))+tag+data+struct.pack('>I',zlib.crc32(tag+data)&0xffffffff)
    raw = b''.join(b'\0'+data[y*2400:(y+1)*2400] for y in range(200))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',800,200,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--source',type=Path,default=Path('C:/Work/Kainote'))
    args=parser.parse_args()
    if os.name!='nt':
        raise SystemExit('Windows-only experiment. Linux/fontconfig has not been executed.')
    root=HERE/'_run';root.mkdir(exist_ok=True)
    # A fresh run directory avoids accidentally replaying stale captures.
    run=root/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ');run.mkdir()
    fixtures=generate(run/'fixtures')
    provenance=build(args.source.resolve(),run)
    records={}
    for mode in ['stock','hook']:
        folder=run/mode;folder.mkdir()
        text=command([run/(mode+'.exe'),folder,run/'fixtures'])
        (folder/'trace.jsonl').write_text(text,encoding='utf-8')
        events=[json.loads(line) for line in text.splitlines() if line.startswith('{')]
        for e in events:
            if e.get('file') and e['event'] in ['selected_v1','metadata_candidate']:
                data=(folder/e['file']).read_bytes()
                e['sha256']=digest(data)
                e['matching_fixture_names']=[n for n,b in fixtures.items() if b==data]
                e['byte_count']=len(data)
            if e['event']=='render':
                e['ppm_sha256']=digest((folder/e['file']).read_bytes())
        records[mode]=events
    evidence=HERE/'evidence';evidence.mkdir(exist_ok=True)
    report={'question':'Can actual renderer selection be tied to exact font bytes and collection faces?',
            'utc':datetime.now(timezone.utc).isoformat(),'platform':platform.platform(),
            'python':platform.python_version(),'provenance':provenance,
            'fixtures':{n:{'sha256':digest(b),'bytes':len(b)} for n,b in fixtures.items()},
            'records':records}
    stock_renders={e['case']:e for e in records['stock'] if e['event']=='render'}
    hook_renders={e['case']:e for e in records['hook'] if e['event']=='render'}
    comparison={c:stock_renders[c]['ppm_sha256']==hook_renders[c]['ppm_sha256'] for c in stock_renders}
    reimports={c.removeprefix('reimport_'):e['ppm_sha256']==hook_renders[c.removeprefix('reimport_')]['ppm_sha256']
               for c,e in hook_renders.items() if c.startswith('reimport_')}
    report['observations']={'stock_hook_same_pixels':comparison,'reimport_same_pixels':reimports,
        'warning':'Equal pixels for these frames do not establish complete font collection or cross-platform parity.',
        'source_label_guard':'Local archive passes pointer 1 for CONFIG_SOURCEVERSION; the shared callback guards that exact diagnostic instead of dereferencing it. Archive provenance remains its recorded binary hash.'}
    (evidence/'observed.json').write_text(json.dumps(report,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    # A self-contained report contains genuine rendered pixels, not screenshots.
    import base64
    cards=[]
    ids=list(dict.fromkeys(e['case'] for e in records['hook'] if e['event']=='render'))
    for case in ids:
        ev=[e for e in records['hook'] if e['case']==case]
        selected=[e for e in ev if e['event']=='selected_v1']
        logs=[e['text'] for e in ev if e['event']=='log' and any(x in e['text'] for x in ['fontselect','Glyph','font provider'])]
        rendered=next(e for e in ev if e['event']=='render')
        stock=next((e for e in records['stock'] if e['event']=='render' and e['case']==case),None)
        exact=bool(selected) and all(e.get('sha256') for e in selected)
        label='Selected bytes captured' if exact else 'No new selection event; inspect cache or failure'
        if any('Using default' in line for line in logs): label+=' · requested match unavailable, known default fallback'
        if any('Glyph ' in line and 'not found, selecting' in line for line in logs) and len(selected)>1: label+=' · glyph fallback'
        if any('failed to find any fallback' in line for line in logs): label+=' · missing glyph'
        if rendered['images']==0: label='No visible output · unresolved request/glyph'
        comparison=('Stock pixels agree' if stock['ppm_sha256']==rendered['ppm_sha256'] else 'Stock pixels differ') if stock else 'Reimport or lifetime observation'
        if case.startswith('reimport_'):
            comparison='Reimport pixels agree' if reimports[case.removeprefix('reimport_')] else 'REIMPORT MISMATCH · collected files did not reproduce fallback policy'
        png=base64.b64encode(png_from_ppm(run/'hook'/rendered['file'])).decode('ascii')
        identity_events=[e for e in ev if e['event'] in ['selected_v1','opened_face_v1','glyph_simulation_v1','metadata_candidate']]
        details=json.dumps(identity_events,indent=2,ensure_ascii=False)
        cards.append(f'<article><h2>{html.escape(case)}</h2><p class="status">{html.escape(label)}</p><p>{comparison} · {rendered["images"]} image nodes</p><img alt="Actual libass pixels for {case}" src="data:image/png;base64,{png}"><details><summary>Selection logs and identities</summary><pre>{html.escape(chr(10).join(logs))}</pre><pre>{html.escape(details)}</pre></details></article>')
    stock_equal=sum(report['observations']['stock_hook_same_pixels'].values())
    summary=f'{stock_equal}/{len(stock_renders)} stock/hook frames agree. {sum(reimports.values())}/{len(reimports)} captured-font-set reimports agree.'
    failed=[c for c,agree in reimports.items() if not agree]
    if failed: summary+=' Reimport mismatches: '+', '.join(failed)+'.'
    page='''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>Font identity native probe</title>
<style>body{font:16px system-ui;background:#151820;color:#e1e5ed;margin:2rem auto;max-width:1000px;padding:0 1rem}h1{font-size:1.8rem}h2{font-size:1.15rem}article{border:1px solid #454c5e;padding:1rem;margin:1rem 0;border-radius:8px}img{width:100%;max-width:800px}pre{white-space:pre-wrap;overflow-wrap:anywhere;font-size:.8rem}.status{color:#b6d6ff}summary,button{cursor:pointer}button{padding:.4rem;background:#263750;color:#fff;border:1px solid #7185a4;border-radius:4px;margin:.2rem}</style>
<h1>Font identity: native feasibility experiment</h1><p>Throwaway probe for issue 47. These are executed Windows observations, not a production collector or a complete font certification.</p>
<p>The stock public log names a selected face. The isolated diagnostic-hook build captures bytes from the actual selected provider stream and records its face index. A separate GDI/DirectWrite lookup supplies candidates; candidate agreement is checked by bytes, never assumed from a name.</p>
<p><strong>Observed:</strong> OBSERVATION_SUMMARY Equal pixels do not mean every request was fulfilled: missing-request and missing-glyph cases can reproduce the same deficiency.</p>
<p><strong>Decision to review:</strong> the two same-name attachment files give identical public selection lines but different hashes and pixels. Independent GDI/DirectWrite matching identifies the system Arial file while libass uses a same-name attachment. A narrow hook can capture actual selected bytes and the opened FreeType face here; files alone do not recreate the system fallback resolver.</p>
<p><strong>Still unresolved:</strong> Linux/fontconfig, variable-instance coordinates, complete per-glyph tracing, cancellation, OS font-install notifications, and multithread stress. The hook observes bold/italic simulation at glyph-load time, not every cached render. Synthetic boxes test identity; they do not establish correct Arabic/CJK shaping. User review of diagnostics and maintenance cost remains open.</p>
<p><strong>Build caveat:</strong> the existing local archive has an invalid source-version log argument (pointer 1). Both probe builds guard that single log; archive hashes are recorded without claiming reproducible binary provenance. No production source was changed.</p>
<button onclick="document.querySelectorAll('details').forEach(x=>x.open=true)">Show all evidence</button><button onclick="document.querySelectorAll('details').forEach(x=>x.open=false)">Collapse evidence</button>
'''.replace('OBSERVATION_SUMMARY',html.escape(summary))+''.join(cards)+'</html>'
    (evidence/'report.html').write_text(page,encoding='utf-8')
    print(json.dumps({'report':str(evidence/'report.html'),'evidence':str(evidence/'observed.json'),'local_run':str(run)},indent=2))


if __name__=='__main__':
    main()
