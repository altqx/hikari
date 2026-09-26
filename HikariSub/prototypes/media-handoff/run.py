"""Bounded native media feasibility probe. No installer, player or device output.

Run with the existing PySide6 interpreter:
  python run.py --source C:/Work/Kainote
Writes only this prototype's ignored _run/ plus observations.json/report.html.
"""
import argparse
import base64
import hashlib
import html
import importlib
import importlib.metadata
import importlib.util
import json
import math
import os
import platform
import re
import struct
import subprocess
import sys
import wave
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def execute(args, timeout=60):
    completed = subprocess.run([str(x) for x in args], capture_output=True,
                               text=True, encoding='utf-8', errors='replace', timeout=timeout)
    return {'returncode': completed.returncode, 'stdout': completed.stdout, 'stderr': completed.stderr}


def inventory(source):
    result = {'python': sys.version, 'executable': sys.executable, 'platform': platform.platform(),
              'architecture': platform.machine(), 'modules': {}, 'scope': 'existing Windows runtime and named repository directories'}
    for module in ['QtCore', 'QtGui', 'QtQml', 'QtQuick', 'QtMultimedia', 'QtMultimediaWidgets']:
        name = 'PySide6.' + module
        try:
            loaded = importlib.import_module(name)
            result['modules'][module] = {'imported': True, 'path': loaded.__file__}
            if module == 'QtCore':
                result['qt_version'] = loaded.qVersion()
        except ImportError as error:
            result['modules'][module] = {'imported': False, 'error': str(error)}
    try:
        import PySide6
        directory = Path(PySide6.__file__).parent
        result['pyside_essentials'] = importlib.metadata.version('PySide6-Essentials')
        stub = directory / 'QtMultimedia.pyi'
        result['multimedia_files'] = [str(p.relative_to(directory)) for p in directory.rglob('*Multimedia*')]
        if stub.exists():
            text = stub.read_text(encoding='utf-8')
            player = text.split('class QMediaPlayer(')[1].split('\nclass ')[0]
            methods = re.findall(r'^    def (\w+)\(', player, re.MULTILINE)
            result['qmediaplayer_stub'] = {'sha256': digest(stub), 'path': str(stub),
                'methods': methods, 'chapter_named_methods': [m for m in methods if 'chapter' in m.lower()],
                'meaning': 'typing declarations only, not loaded Qt Multimedia capability'}
        result['portaudio_runtime_candidates'] = [str(p) for p in directory.rglob('*.dll') if 'portaudio' in p.name.lower()]
    except ImportError:
        pass
    result['portaudio_repository_candidates'] = [str(p) for folder in [source/'x64/Debug', source/'bin/x64/Debug']
        if folder.exists() for p in folder.iterdir() if 'portaudio' in p.name.lower()]
    if os.name == 'nt':
        command = "$o=Get-CimInstance Win32_OperatingSystem;$c=Get-CimInstance Win32_ComputerSystem;" \
            "[pscustomobject]@{os=$o.Caption;version=$o.Version;build=$o.BuildNumber;ramBytes=$c.TotalPhysicalMemory;" \
            "cpu=@(Get-CimInstance Win32_Processor|Select-Object Name,NumberOfCores,NumberOfLogicalProcessors);" \
            "gpu=@(Get-CimInstance Win32_VideoController|Select-Object Name,DriverVersion);" \
            "audioInventory=@(Get-CimInstance Win32_SoundDevice|Select-Object Name,Status)}|ConvertTo-Json -Depth 5"
        captured = execute(['powershell', '-NoProfile', '-Command', command])
        result['machine'] = json.loads(captured['stdout']) if captured['returncode'] == 0 else captured
    result['binary_files'] = []
    for folder in [source/'x64/Debug', source/'Thirdparty/ffmpeg/bin']:
        if folder.exists():
            for p in sorted(folder.iterdir()):
                if p.suffix.lower() == '.dll' or p.name.lower() in ['ffmpeg.exe', 'ffprobe.exe']:
                    result['binary_files'].append({'path': str(p), 'bytes': p.stat().st_size, 'sha256': digest(p)})
    for p in [source/'Thirdparty/ffms2/include/ffms.h', source/'Thirdparty/Build/FFMS2/indexing_additional.cpp',
              source/'bin/x64/Debug/Libass.lib']:
        if p.exists():
            result.setdefault('source_or_archive_inputs', []).append({'path': str(p), 'sha256': digest(p)})
    return result


def fixtures(folder, ffmpeg, ffprobe):
    raw = folder/'frames.bgr'
    with raw.open('wb') as f:
        for n in range(100):
            frame = bytearray(320*180*3)
            for y in range(180):
                for x in range(320):
                    at = (y*320+x)*3
                    rgb = ((x+n*3)%256, (y*2+n*5)%256, (x//2+y+n*7)%256)
                    frame[at:at+3] = bytes(reversed(rgb))
                    if 8 <= y < 28 and 8 <= x < 168:
                        bit = (x-8)//20
                        level = 240 if (n >> bit) & 1 else 16
                        frame[at:at+3] = bytes([level]*3)
            f.write(frame)
    audio_expected = []
    for track, hz in enumerate([440, 660]):
        samples = bytearray()
        for i in range(48000*5):
            value = round(3000*math.sin(2*math.pi*hz*i/48000))
            if i % 48000 == 0:
                value = 18000
            samples.extend(struct.pack('<h', value))
        with wave.open(str(folder/f'tone{track}.wav'), 'wb') as wav:
            wav.setnchannels(1)
            wav.setsampwidth(2)
            wav.setframerate(48000)
            wav.writeframes(samples)
        audio_expected.append({'source_track': track, 'frequency_hz': hz, 'pcm_4800_4864_sha256': hashlib.sha256(samples[9600:9728]).hexdigest()})
    (folder/'drawing.ass').write_text('''[Script Info]
ScriptType: v4.00+
PlayResX: 320
PlayResY: 180
[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Default,Arial,20,&H4000FFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,0,0,7,0,0,0,1
[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
Dialogue: 0,0:00:00.50,0:00:01.50,Default,,0,0,0,,{\\pos(20,50)\\p1}m 0 0 l 80 0 80 30 0 30
Dialogue: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,{\\pos(160,90)\\p1}m 0 0 l 100 0 100 40 0 40
''', encoding='utf-8')
    (folder/'captions.srt').write_text('1\n00:00:00,500 --> 00:00:01,500\nOriginal synthetic caption one.\n\n2\n00:00:02,000 --> 00:00:03,000\nOriginal synthetic caption two.\n', encoding='utf-8')
    (folder/'chapters.txt').write_text(';FFMETADATA1\ntitle=Hikari synthetic handoff fixture\n[CHAPTER]\nTIMEBASE=1/1000\nSTART=0\nEND=2000\ntitle=Departure\n[CHAPTER]\nTIMEBASE=1/1000\nSTART=2000\nEND=4000\ntitle=Arrival\n', encoding='utf-8')
    result = {'rights': 'Original generated pixels, tones, vector paths and text; CC0-1.0 fixture content. No external media/fonts.',
              'generator': {'frames': 100, 'width': 320, 'height': 180, 'barcode': '8 binary cells at x=8+20*bit, y=8..27, LSB first',
                            'cfr_rate': [25, 1], 'vfr_pts_seconds': '(floor(N/3)*7 + [0,1,3][N%3])/50'},
              'audio_expected': audio_expected, 'files': [], 'ffmpeg_version': execute([ffmpeg, '-version'])['stdout'].splitlines(),
              'commands': []}
    for kind in ['cfr', 'vfr']:
        target = folder/f'{kind}.mkv'
        filters = 'setsar=1,format=yuv420p'
        if kind == 'vfr':
            filters += r',settb=1/1000,setpts=(floor(N/3)*7+if(eq(mod(N\,3)\,1)\,1\,if(eq(mod(N\,3)\,2)\,3\,0)))/(50*TB)'
        command = [ffmpeg,'-hide_banner','-loglevel','error','-nostdin','-y','-f','rawvideo','-pix_fmt','bgr24','-s','320x180','-r','25','-i',raw]
        for name in ['tone0.wav','tone1.wav','drawing.ass','captions.srt','chapters.txt']:
            command += ['-i',folder/name]
        command += ['-map','0:v:0','-map','1:a:0','-map','2:a:0','-map','3:s:0','-map','4:s:0',
                    '-map_metadata','5','-map_chapters','5','-vf',filters,'-fps_mode','passthrough',
                    '-enc_time_base','1:1000','-c:v','libx264','-preset','medium','-crf','18',
                    '-x264-params','keyint=50:min-keyint=50:scenecut=0:bframes=2:b-adapt=0:threads=1',
                    '-color_range','tv','-colorspace','bt709','-color_primaries','bt709','-color_trc','bt709',
                    '-c:a','pcm_s16le','-c:s:0','ass','-c:s:1','srt',
                    '-metadata:s:a:0','title=Original 440 Hz','-metadata:s:a:0','language=eng',
                    '-metadata:s:a:1','title=Original 660 Hz','-metadata:s:a:1','language=jpn',
                    '-metadata:s:s:0','title=Original vector ASS','-metadata:s:s:0','language=eng',
                    '-metadata:s:s:1','title=Original SRT','-metadata:s:s:1','language=jpn',target]
        completed = execute(command)
        result['commands'].append({'argv': [str(x) for x in command], **completed})
        if completed['returncode']:
            raise RuntimeError('Fixture generation failed: '+completed['stderr'])
        metadata = execute([ffprobe,'-v','error','-show_streams','-show_chapters','-show_format','-of','json',target])
        frames = execute([ffprobe,'-v','error','-select_streams','v:0','-show_frames','-show_entries',
                          'frame=pts,pts_time,pict_type,key_frame','-of','json',target])
        frame_list = json.loads(frames['stdout'])['frames']
        expected_pts_ms = [n*40 if kind == 'cfr' else (n//3*7+[0,1,3][n%3])*20 for n in range(100)]
        actual_pts_ms = [round(float(f['pts_time'])*1000) for f in frame_list]
        if actual_pts_ms != expected_pts_ms:
            raise RuntimeError('Generated fixture PTS did not match its requested timeline: '+kind)
        result['files'].append({'name': target.name, 'bytes': target.stat().st_size, 'sha256': digest(target),
                               'metadata': json.loads(metadata['stdout']), 'frames': frame_list,
                               'requested_pts_ms': expected_pts_ms, 'ffprobe_pts_match_generator': True})
    result['input_sha256'] = {p.name: digest(p) for p in folder.iterdir() if p.suffix in ['.bgr','.wav','.ass','.srt','.txt']}
    return result


def report(observed, folder):
    cards = []
    for name, entry in observed.get('probes', {}).items():
        summary = entry.get('summary', {})
        cards.append('<section><h2>'+html.escape(name)+'</h2><pre>'+html.escape(json.dumps(summary, indent=2))+'</pre><details><summary>Raw observed result</summary><pre>'+html.escape(json.dumps(entry, indent=2))+'</pre></details></section>')
    pictures = ''
    try:
        from PySide6.QtGui import QImage
        from PySide6.QtCore import QBuffer, QByteArray, QIODevice
        for raw in sorted(folder.glob('*-frame25.bgra')):
            # QImage conversion is a CPU report image, not a Qt Quick/GPU presentation.
            image = QImage(raw.read_bytes(), 320, 180, 320*4, QImage.Format.Format_ARGB32).copy()
            data = QByteArray()
            buf = QBuffer(data)
            buf.open(QIODevice.OpenModeFlag.WriteOnly)
            image.save(buf, 'PNG')
            pictures += '<figure><img alt="Native FFMS2 decoded frame 25" src="data:image/png;base64,'+base64.b64encode(bytes(data)).decode()+'"><figcaption>'+html.escape(raw.name)+' · owned CPU frame; no subtitles composed</figcaption></figure>'
    except ImportError:
        pass
    body = '''<!doctype html><meta charset="utf-8"><title>Native media feasibility observations</title><style>
body{font:14px/1.5 system-ui;background:#171b20;color:#e8edf2;margin:28px auto;max-width:1100px;padding:0 18px}h1{font-size:23px}h2{font-size:17px}section,aside{padding:16px;background:#20262d;border:1px solid #414b57;border-radius:6px;margin:16px 0}aside{border-color:#f9d784}a{color:#9cdbc9}pre{font:11px/1.5 Consolas,monospace;white-space:pre-wrap;overflow-wrap:anywhere}summary{cursor:pointer}figure{display:inline-block;margin:8px}img{max-width:100%;border:1px solid #414b57}figcaption{font-size:11px;color:#a5b1bd}</style>
<h1>Native media probe · bounded Windows observations</h1><aside><strong>No clock handoff pass.</strong> The installed PySide6 runtime lacks importable Qt Multimedia; no PortAudio binary was found in the scoped runtime/repository search. General-player transport, audible output, device latency, stop/flush acknowledgement, libass/QSG composition and Linux remain unexecuted. The observed slice uses the existing FFMS2 DLL on original synthetic media.</aside>
<p>Clock owner: <strong>none</strong>. Clock estimate: <strong>unavailable</strong>. No playback or fake switch between modes occurs in this report. Frame images are real CPU decode output, not screenshots or simulated native presentation.</p>
<p><a href="README.md">Run instructions, responsibility contracts and remaining proof</a> · <a href="observations.json">Machine-readable observations</a></p>'''
    body += pictures + ''.join(cards) + '<section><h2>Prerequisite observations</h2><pre>'+html.escape(json.dumps(observed['inventory'], indent=2))+'</pre></section>'
    (HERE/'report.html').write_text(body, encoding='utf-8')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, default=Path('C:/Work/Kainote'))
    args = parser.parse_args()
    source = args.source.resolve()
    folder = HERE/'_run'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    folder.mkdir(parents=True, exist_ok=False)
    observed = {'utc': datetime.now(timezone.utc).isoformat(), 'kind': 'functional feasibility only; not release-performance qualification',
                'inventory': inventory(source), 'source_scripts': {p.name:digest(p) for p in [HERE/'run.py',HERE/'ffms_probe.py']},
                'probes': {}, 'run_directory': str(folder), 'unexecuted': ['Qt Multimedia player', 'PortAudio output',
                'active-mode clock handoff', 'libass alpha/color composition', 'QSG presentation', 'Linux',
                'device routing/latency/underruns', 'production generation queues', 'release performance gates']}
    ffmpeg, ffprobe = source/'Thirdparty/ffmpeg/bin/ffmpeg.exe', source/'Thirdparty/ffmpeg/bin/ffprobe.exe'
    dll = source/'x64/Debug/FFMS2.dll'
    if os.name != 'nt' or not all(p.exists() for p in [ffmpeg,ffprobe,dll]):
        observed['blocked'] = 'This bounded runner requires existing Windows FFMS2 and repository FFmpeg binaries; no fallback/provisioning.'
    else:
        observed['fixtures'] = fixtures(folder, ffmpeg, ffprobe)
        for kind, mode in [('cfr','decode'),('vfr','decode'),('cfr','cancel'),('cfr','subtitles')]:
            name = kind+'-'+mode
            result_file = folder/(name+'.json')
            try:
                child = execute([sys.executable,HERE/'ffms_probe.py',dll,folder/(kind+'.mkv'),result_file,mode], timeout=45)
            except subprocess.TimeoutExpired:
                child = {'returncode': None, 'timed_out': True, 'stdout': '', 'stderr': 'Native child exceeded 45 s experiment bound; killed by runner.'}
            entry = {'process': child, 'native': json.loads(result_file.read_text()) if result_file.exists() else None}
            native = entry['native'] or {}
            if mode == 'decode' and native.get('completed'):
                sequential, random = native['sequential'], native['random_access']
                audio_checks = []
                for number, audio in enumerate(native['audio']):
                    expected = observed['fixtures']['audio_expected'][number]['pcm_4800_4864_sha256']
                    audio_checks.append({'track':audio['track'], 'matches_generated_s16_pcm':audio.get('pcm_sha256')==expected})
                entry['summary'] = {'decoded_frames':len(sequential), 'barcode_matches':sum(r['requested_index']==r['pixel_barcode_id'] for r in sequential),
                    'random_seeks':len(random), 'random_barcode_and_hash_matches':sum(r['requested_index']==r['pixel_barcode_id'] and r['matches_sequential'] for r in random),
                    'owned_copy_survives_decoder_destruction':native['copied_frame_survives_decoder_destruction'],
                    'out_of_range_returned_frame':native['out_of_range']['returned_frame'], 'audio':audio_checks,
                    'tracks':native['tracks'], 'chapters':native['chapters'],
                    'pts_step_microseconds':sorted(set((r['document_time_us_rational'][0]/r['document_time_us_rational'][1])-(sequential[i-1]['document_time_us_rational'][0]/sequential[i-1]['document_time_us_rational'][1]) for i,r in enumerate(sequential) if i))}
            else:
                entry['summary'] = {'process_returncode':child['returncode'], 'completed':native.get('completed', False),
                                    'cancel_observed':native.get('cancel_observed'), 'native_result':native}
            observed['probes'][name] = entry
    (HERE/'observations.json').write_text(json.dumps(observed, indent=2), encoding='utf-8')
    report(observed, folder)
    print(json.dumps({'run_directory':str(folder),'summaries':{n:p['summary'] for n,p in observed['probes'].items()},
                      'report':str(HERE/'report.html')}, indent=2))


if __name__ == '__main__':
    main()
