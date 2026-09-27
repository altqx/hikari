"""Bounded local font-replay diagnosis; preserves the parent evidence files."""
import argparse
import hashlib
import json
import os
import platform
from pathlib import Path
import subprocess
from datetime import datetime, timezone

HERE = Path(__file__).resolve().parent
PARENT = HERE.parent


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def command(args, **kwargs):
    r = subprocess.run(args, capture_output=True, text=True, encoding='utf-8',
                       errors='replace', **kwargs)
    if r.returncode:
        raise RuntimeError(f'command failed ({r.returncode}): {r.stdout[-6000:]}\n{r.stderr[-2000:]}')
    return r.stdout


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, default=Path('C:/Work/Kainote'))
    parser.add_argument('--original-run', type=Path,
                        default=PARENT/'_run/20260926T215219440218Z')
    parser.add_argument('--expect-original-agreement', action='store_true',
                        help='Return 1 when the original full replay differs: the red-capable diagnosis signal.')
    args = parser.parse_args()
    old = args.original_run.resolve()
    original_evidence = PARENT/'evidence/observed.json'
    original_hash = sha(original_evidence)
    evidence = json.loads(original_evidence.read_text(encoding='utf-8'))
    parent_report_hash = sha(PARENT/'evidence/report.html')
    libass = args.source/'Thirdparty/libass'
    source_head = command(['git','-C',str(libass),'rev-parse','HEAD']).strip()
    if source_head!=evidence['provenance']['libass_source_commit'] or command(
            ['git','-C',str(libass),'status','--porcelain']).strip():
        raise RuntimeError('Pinned libass source changed')
    for filename, key in [('ass_fontselect_probe.c', 'modified_selector_sha256'),
                          ('ass_font_probe.c', 'modified_face_source_sha256')]:
        if sha(old/filename) != evidence['provenance'][key]:
            raise RuntimeError('Original generated hook source differs: '+filename)
    fonts = [e for e in evidence['records']['hook']
             if e['case']=='system_multilingual' and e['event']=='selected_v1']
    for font in fonts:
        if sha(old/'hook'/font['file']) != font['sha256']:
            raise RuntimeError('Captured font bytes differ')
    library_inputs = {name:args.source/'bin/x64/Debug'/name
                      for name in evidence['provenance']['libraries']}
    for name,path in library_inputs.items():
        if sha(path)!=evidence['provenance']['libraries'][name]['sha256']:
            raise RuntimeError('Original archive changed: '+name)
    run = HERE/'_run'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    run.mkdir(parents=True)
    vswhere = Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
    vs = Path(command([str(vswhere), '-latest', '-products', '*', '-requires',
                       'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property',
                       'installationPath']).strip())
    vcvars = vs/'VC/Auxiliary/Build/vcvars64.bat'
    lines = command(f'cmd.exe /d /s /c ""{vcvars}" >nul && set"')
    env = {k.upper():v for k,v in os.environ.items()}
    env.update((k.upper(),v) for k,v in (line.split('=',1) for line in lines.splitlines()
               if '=' in line and not line.startswith('=')))
    cl = next(Path(p)/'cl.exe' for p in env['PATH'].split(';') if (Path(p)/'cl.exe').exists())
    includes = [args.source/'Thirdparty/libass/libass']
    executable = run/'diagnostic.exe'
    argv = [str(cl), '/nologo', '/std:c++17', '/EHsc', '/utf-8', '/MDd', '/Od',
            *('/I'+str(p) for p in includes), str(HERE/'probe.cpp'),
            str(old/'hook_selector.obj'), str(old/'hook_font.obj'),
            '/Fo'+str(run/'diagnostic.obj'), '/Fd'+str(run/'compile.pdb'),
            '/Fe'+str(executable), '/link', '/LIBPATH:'+str(args.source/'bin/x64/Debug'),
            'Libass.lib', 'FreeType2.lib', 'Fribidi.lib', 'HarfBuzz.lib', 'Zlib.lib',
            'dwrite.lib', 'gdi32.lib', 'user32.lib', 'ole32.lib', 'advapi32.lib', 'usp10.lib',
            '/PDB:'+str(run/'link.pdb')]
    (run/'build.log').write_text(command(argv, env=env, cwd=run), encoding='utf-8')
    trace = command([str(executable), str(run/'output'),
                     *(str(old/'hook'/font['file']) for font in fonts)], timeout=30)
    (run/'trace.jsonl').write_text(trace, encoding='utf-8')
    records = [json.loads(line) for line in trace.splitlines() if line.startswith('{')]
    for record in records:
        if record.get('file'):
            record['sha256'] = sha(run/'output'/record['file'])
    renders = {e['case']:e for e in records if e['event']=='render'}
    comparisons = {case:renders[case+'_directwrite']['sha256']==renders[case+'_none']['sha256']
                   for case in ('full','ascii','cjk','emoji','arabic_combining')}
    pairs = [
                      ('full_none_reverse','full_none'),
                      ('full_directwrite_captured','full_directwrite'),
                      ('cjk_none_default_yu','cjk_directwrite'),
                      ('emoji_none_default_segoe','emoji_directwrite'),
                      ('cjk_none_explicit_yu','cjk_directwrite'),
                      ('emoji_none_explicit_segoe','emoji_directwrite'),
                      ('full_none_default_yu','full_directwrite'),
                      ('full_none_default_segoe','full_directwrite'),
                      ('full_none_authored_families','full_directwrite')]
    variations = {case:renders[case]['sha256']==renders[original]['sha256']
                  for case,original in pairs}
    repeat = command([str(executable), str(run/'repeat-output'),
                      *(str(old/'hook'/font['file']) for font in fonts)], timeout=30)
    (run/'repeat-trace.jsonl').write_text(repeat,encoding='utf-8')
    repeated = {record['case']:sha(run/'repeat-output'/record['file'])
                for record in (json.loads(line) for line in repeat.splitlines() if line.startswith('{'))
                if record['event']=='render'}
    original_frames = {e['case']:e['ppm_sha256'] for e in evidence['records']['hook'] if e['event']=='render'}
    captured = {
        'utc': datetime.now(timezone.utc).isoformat(), 'run_directory':str(run),
        'platform':platform.platform(), 'python':platform.python_version(),
        'libass_source_commit':source_head,
        'original_evidence_sha256':original_hash, 'original_evidence_unchanged':sha(original_evidence)==original_hash,
        'original_report_sha256':parent_report_hash,
        'original_report_unchanged':sha(PARENT/'evidence/report.html')==parent_report_hash,
        'command':argv, 'compiler_sha256':sha(cl), 'executable_sha256':sha(executable),
        'source_sha256': {str(p):sha(p) for p in (HERE/'run.py', HERE/'probe.cpp', PARENT/'probe.cpp')},
        'reused_objects_sha256': {p.name:sha(p) for p in (old/'hook_selector.obj',old/'hook_font.obj')},
        'libraries_sha256': {name:sha(path) for name,path in library_inputs.items()},
        'input_fonts':fonts, 'records':records, 'provider_pair_same_pixels':comparisons,
        'variation_same_pixels':variations,
        'variation_reference_cases':dict(pairs),
        'repeat_frame_sha256':repeated,
        'repeat_all_frames_agree':all(repeated[case]==frame['sha256'] for case,frame in renders.items()),
        'original_baseline_frames_agree':{
            'full_directwrite':renders['full_directwrite']['sha256']==original_frames['system_multilingual'],
            'full_none':renders['full_none']['sha256']==original_frames['reimport_system_multilingual']},
        'limits':'Windows existing Debug archives and original hook objects; no dependency rebuild, production patch, Linux or full-document qualification.'}
    (run/'observations.json').write_text(json.dumps(captured,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    (HERE/'observations.json').write_text(json.dumps(captured,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    print(json.dumps({'run':str(run),'provider_pair_same_pixels':comparisons,
                      'variation_same_pixels':variations}))
    return 1 if args.expect_original_agreement and not comparisons['full'] else 0


if __name__=='__main__':
    raise SystemExit(main())
