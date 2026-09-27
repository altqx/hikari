"""Original, Windows-only throwaway LuaJIT process/QML feasibility experiment.

Uses an existing Debug LuaJIT archive and installed MSVC/PySide. No installs.
Writes only this directory's ignored _run/ plus evidence/ and report.html.
"""
import argparse
import hashlib
import html
import json
import math
import os
import platform
import struct
import subprocess
import sys
import time
import uuid
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent
LIMIT = 65536
HELLO, START, DIALOG, REPLY, RESULT, CANCEL, PROGRESS, STOP, ERROR = range(1, 10)
REJECT = 11


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def loaded_modules(pid):
    """Read only this probe's own GUI/helper process module paths and bytes."""
    import ctypes as c
    from ctypes import wintypes as w
    kernel, psapi = c.WinDLL('kernel32', use_last_error=True), c.WinDLL('psapi', use_last_error=True)
    kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.OpenProcess.restype = w.HANDLE
    kernel.CloseHandle.argtypes = [w.HANDLE]
    psapi.EnumProcessModulesEx.argtypes = [w.HANDLE, c.POINTER(w.HMODULE), w.DWORD, c.POINTER(w.DWORD), w.DWORD]
    psapi.GetModuleFileNameExW.argtypes = [w.HANDLE, w.HMODULE, w.LPWSTR, w.DWORD]
    handle = kernel.OpenProcess(0x0410, False, pid)
    if not handle:
        return {'error': 'OpenProcess failed', 'winerror': c.get_last_error()}
    try:
        modules = (w.HMODULE * 1024)()
        needed = w.DWORD()
        if not psapi.EnumProcessModulesEx(handle, modules, c.sizeof(modules), c.byref(needed), 3):
            return {'error': 'EnumProcessModulesEx failed', 'winerror': c.get_last_error()}
        result = {}
        for module in modules[:min(1024, needed.value // c.sizeof(w.HMODULE))]:
            name = c.create_unicode_buffer(32768)
            if psapi.GetModuleFileNameExW(handle, module, name, len(name)):
                try:
                    result[name.value] = sha(name.value)
                except OSError as exc:
                    result[name.value] = 'unreadable: ' + str(exc)
        return {'sha256': result, 'snapshot_only': True, 'truncated': needed.value > c.sizeof(modules)}
    finally:
        kernel.CloseHandle(handle)


def command(args, **kw):
    r = subprocess.run([str(a) for a in args] if isinstance(args, list) else args,
                       capture_output=True, text=True, encoding='utf-8', errors='replace', **kw)
    if r.returncode:
        raise RuntimeError(f'{r.returncode}: {args}\n{r.stdout}\n{r.stderr}')
    return r.stdout + r.stderr


def build(source, out):
    vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
    vs = Path(command([vswhere, '-latest', '-products', '*', '-requires',
                       'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath']).strip())
    vcvars = vs / 'VC/Auxiliary/Build/vcvars64.bat'
    # Read a compiler environment; compilation uses argv and only owned outputs.
    env_text = command(f'cmd.exe /d /s /c ""{vcvars}" >nul && set"')
    env = os.environ.copy()
    env.update(line.split('=', 1) for line in env_text.splitlines() if '=' in line and not line.startswith('='))
    env = {k.upper(): v for k, v in env.items()}
    cl = next(Path(p) / 'cl.exe' for p in env['PATH'].split(';') if (Path(p) / 'cl.exe').exists())
    lib = source / 'bin/x64/Debug/LuaJit.lib'
    exe = out / 'lua-helper.exe'
    args = [cl, '/nologo', '/TC', '/std:c17', '/utf-8', '/MDd', '/Od', '/W3',
            '/D_CRT_SECURE_NO_WARNINGS', '/I' + str(source / 'Thirdparty/LuaJIT/src'), HERE / 'helper.c',
            '/Fo' + str(out / 'helper.obj'), '/Fd' + str(out / 'compile.pdb'), '/Fe' + str(exe),
            '/link', lib, '/PDB:' + str(out / 'helper.pdb')]
    built = subprocess.run([str(a) for a in args], env=env, cwd=out, capture_output=True,
                           text=True, encoding='utf-8', errors='replace')
    log = built.stdout + built.stderr
    (out / 'build.log').write_text(log, encoding='utf-8')
    if built.returncode:
        raise RuntimeError(f'Build failed ({built.returncode}); see {out / "build.log"}\n{log[-2500:]}')
    paths = [HERE / f for f in ['run.py', 'helper.c', 'fixtures.lua', 'Dialog.qml']]
    paths += [source / 'Thirdparty/LuaJIT/src' / f for f in ['lua.h', 'lauxlib.h', 'lualib.h', 'luaconf.h', 'luajit.h', 'luajit_relver.txt']]
    return exe, {
        'command': [str(a) for a in args], 'compiler': str(cl), 'compiler_sha256': sha(cl),
        'archive': str(lib), 'archive_bytes': lib.stat().st_size, 'archive_sha256': sha(lib),
        'helper_sha256': sha(exe), 'helper_bytes': exe.stat().st_size,
        'source_head': command(['git', '-C', source, 'rev-parse', 'HEAD']).strip(),
        'luajit_source_tree': command(['git', '-C', source, 'rev-parse', 'HEAD:Thirdparty/LuaJIT']).strip(),
        'input_sha256': {str(p): sha(p) for p in paths},
        'limitation': 'Existing Debug archive was not rebuilt or attested to these headers/source; runtime identity is observed separately. MSVC2026 prototype, not accepted MSVC2022 build-workflow proof.'}


def encode(value, depth=0):
    if depth > 12:
        raise ValueError('depth')
    if value is None:
        return b'\0'
    if isinstance(value, bool):
        return b'\2' if value else b'\1'
    if isinstance(value, (int, float)):
        if not math.isfinite(value):
            raise ValueError('nonfinite')
        return b'\3' + struct.pack('<d', value)
    if isinstance(value, str):
        b = value.encode('utf-8')
        return b'\4' + struct.pack('<I', len(b)) + b
    if isinstance(value, list):
        value = {i + 1: v for i, v in enumerate(value)}
    if isinstance(value, dict) and len(value) <= 128:
        return b'\5' + struct.pack('<I', len(value)) + b''.join(encode(k, depth + 1) + encode(v, depth + 1) for k, v in value.items())
    raise ValueError('unsupported value')


def decode(data):
    pos = 0

    def read(n):
        nonlocal pos
        if pos + n > len(data):
            raise ValueError('truncated value')
        b = data[pos:pos + n]
        pos += n
        return b

    def item(depth=0):
        if depth > 12:
            raise ValueError('depth')
        t = read(1)[0]
        if t == 0:
            return None
        if t in (1, 2):
            return t == 2
        if t == 3:
            return struct.unpack('<d', read(8))[0]
        if t == 4:
            return read(struct.unpack('<I', read(4))[0]).decode('utf-8')
        if t == 5:
            n = struct.unpack('<I', read(4))[0]
            if n > 128:
                raise ValueError('fields')
            result = {}
            for _ in range(n):
                key, value = item(depth + 1), item(depth + 1)
                result[key] = value
            return result
        raise ValueError('tag')

    value = item()
    if pos != len(data):
        raise ValueError('trailing')
    return value


def frame(kind, gen, run, request, body, version=1):
    b = struct.pack('<HHIII', version, kind, gen, run, request) + encode(body)
    if len(b) > LIMIT:
        raise ValueError('frame too large')
    return struct.pack('<I', len(b)) + b


def sequence(table):
    return [table[k] for k in sorted(table)]


def execute(exe, source, out, visible=False):
    if not visible:
        os.environ['QT_QPA_PLATFORM'] = 'offscreen'
        os.environ['QT_QUICK_BACKEND'] = 'software'
    from PySide6.QtCore import QObject, QProcess, QTimer, Slot, QMetaObject, QUrl, qVersion
    from PySide6.QtGui import QGuiApplication
    from PySide6.QtNetwork import QLocalServer
    from PySide6.QtQml import QQmlApplicationEngine
    import PySide6

    cases = [
        ('dialog_one', 'apply'), ('dialog_two', 'apply'), ('custom_cancel', 'custom_cancel'),
        ('default_ok', 'default_ok'), ('default_close', 'default_close'),
        ('cancel_dialog', 'cancel_dialog'), ('unchanged_edgeblur', 'edgeblur'),
        ('lua_error', 'error'), ('cooperative_cancel', 'cooperative'), ('owned_force_stop', 'stuck'),
        ('restart_after_force_stop', 'apply'), ('abrupt_helper_loss', 'loss'),
        ('restart_after_loss', 'apply'), ('version_mismatch', 'wire_version'),
        ('truncated_frame', 'wire_truncated'), ('oversized_frame', 'wire_oversized')]

    class Probe(QObject):
        def __init__(self):
            super().__init__()
            self.gen = self.run = self.ticks = 0
            self.index = -1
            self.current = self.process = self.socket = self.server = None
            self.rx = b''
            self.events, self.results, self.sessions, self.rejections = [], [], [], []
            self.pending = None
            self.done = False
            self.heartbeat_times = []
            self.initial = time.perf_counter()

        def record_event(self, event, **data):
            self.events.append({'ms': round((time.perf_counter() - self.initial) * 1000, 3), 'event': event, **data})

        @Slot(int)
        def heartbeat(self, count):
            self.ticks = count
            self.heartbeat_times.append(time.perf_counter())

        def advance(self):
            self.index += 1
            if self.index == len(cases):
                self.done = True
                QTimer.singleShot(100, app.quit)
                return
            name, mode = cases[self.index]
            self.run += 1
            self.current = {'name': name, 'mode': mode, 'run': self.run, 'start_ticks': self.ticks,
                            'start': time.perf_counter(), 'messages': [], 'expects_exit': mode in ('stuck', 'loss') or mode.startswith('wire_')}
            self.record_event('case_start', name=name, run=self.run)
            root.setProperty('statusText', name)
            if self.process is None:
                self.start_session()
            else:
                self.perform()
            current_run = self.run
            QTimer.singleShot(10000, lambda: self.timeout(current_run))

        def start_session(self):
            self.gen += 1
            gen = self.gen
            server = QLocalServer(self)
            name = 'hikari-lua-probe-' + uuid.uuid4().hex
            if not server.listen(name):
                raise RuntimeError(server.errorString())
            self.server = server
            self.rx = b''
            record = {'generation': gen, 'pipe': server.fullServerName(), 'stdout': '', 'stderr': ''}
            self.sessions.append(record)
            process = QProcess(self)
            self.process = process
            record['started_ms'] = round((time.perf_counter() - self.initial) * 1000, 3)

            def accepted():
                sock = server.nextPendingConnection()
                self.socket = sock
                sock.readyRead.connect(lambda: self.read(sock, gen))
                self.record_event('pipe_connected', generation=gen)
                if sock.bytesAvailable():
                    self.read(sock, gen)

            server.newConnection.connect(accepted)
            process.readyReadStandardOutput.connect(lambda: self.capture(process, record, 'stdout'))
            process.readyReadStandardError.connect(lambda: self.capture(process, record, 'stderr'))
            process.finished.connect(lambda code, status: self.exited(process, record, code, status))
            process.errorOccurred.connect(lambda error: self.record_event('process_error', generation=gen, error=str(error)))
            process.start(str(exe), [server.fullServerName(), str(gen), str(HERE / 'fixtures.lua'),
                                    str(source / 'Automation/automation/Autoload/macro-1-edgeblur.lua')])

        def capture(self, proc, record, kind):
            chunk = bytes(proc.readAllStandardOutput() if kind == 'stdout' else proc.readAllStandardError())
            record[kind] += chunk.decode('utf-8', 'replace')
            if len(record[kind]) > 131072:
                record[kind] = record[kind][:131072]
                record[kind + '_truncated'] = True

        def read(self, sock, gen):
            if gen != self.gen:
                self.record_event('old_connection_ignored', generation=gen)
                sock.readAll()
                return
            self.rx += bytes(sock.readAll())
            while len(self.rx) >= 4:
                n = struct.unpack('<I', self.rx[:4])[0]
                if not 17 <= n <= LIMIT:
                    self.record_event('host_protocol_failure', length=n)
                    self.process.kill()
                    return
                if len(self.rx) < n + 4:
                    return
                payload, self.rx = self.rx[4:4 + n], self.rx[4 + n:]
                version, kind, generation, run, request = struct.unpack('<HHIII', payload[:16])
                msg = {'version': version, 'kind': kind, 'generation': generation, 'run': run,
                       'request': request, 'body': decode(payload[16:])}
                self.handle(msg)

        def handle(self, msg, replay=False):
            self.record_event('received' if not replay else 'controlled_replay', **msg)
            if msg['version'] != 1 or msg['generation'] != self.gen:
                self.rejections.append({'where': 'GUI', 'reason': 'version/generation', 'message': msg, 'controlled_replay': replay})
                return
            kind, body = msg['kind'], msg['body']
            if kind == HELLO:
                self.sessions[-1]['runtime'] = body
                self.sessions[-1]['process_id'] = int(self.process.processId())
                self.sessions[-1]['loaded_modules_at_hello'] = loaded_modules(int(self.process.processId()))
                self.perform()
                return
            if kind == REJECT:
                self.rejections.append({'where': 'helper', 'reason': body, 'generation': self.gen})
                return
            if self.current:
                self.current['messages'].append(msg)
            if kind == DIALOG:
                if msg['run'] != self.run:
                    self.rejections.append({'where': 'GUI', 'reason': 'run', 'message': msg})
                    return
                controls = sequence(body['controls'])
                for c in controls:
                    if c['class'] not in ('label', 'edit', 'intedit', 'checkbox', 'dropdown'):
                        raise RuntimeError('unsupported synthetic control')
                    if 'items' in c:
                        c['items'] = sequence(c['items'])
                labels = sequence(body['buttons'])
                buttons = [{'label': s, 'value': s} for s in labels] if labels else [
                    {'label': 'OK', 'value': ''}, {'label': 'Cancel', 'value': False}]
                self.pending = msg
                self.current['dialog_tick'] = self.ticks
                self.current['qml_control_classes'] = [c['class'] for c in controls]
                root.setProperty('controls', controls)
                root.setProperty('buttons', buttons)
                root.setProperty('caseMode', self.current['mode'])
                root.setProperty('dialogVisible', True)
                expected = self.run
                QTimer.singleShot(300, lambda: self.auto_reply(expected))
            elif kind in (RESULT, ERROR) and self.current and not self.current['expects_exit']:
                if msg['run'] != self.run:
                    self.rejections.append({'where': 'GUI', 'reason': 'stale run', 'message': msg})
                    return
                self.current['terminal'] = 'result' if kind == RESULT else 'lua_error'
                self.current['value'] = body
                self.finish_case()

        def send(self, kind, body=None, request=0, generation=None, run=None, version=1):
            generation = self.gen if generation is None else generation
            run = self.run if run is None else run
            self.record_event('sent', kind=kind, generation=generation, run=run, request=request, version=version)
            self.socket.write(frame(kind, generation, run, request, {} if body is None else body, version))
            self.socket.flush()

        def perform(self):
            if not self.current:
                return
            mode = self.current['mode']
            if self.current['name'] == 'restart_after_force_stop':
                old = {'version': 1, 'kind': RESULT, 'generation': self.gen - 1, 'run': self.run - 1,
                       'request': 0, 'body': {'fixture': 'late result from old generation'}}
                self.handle(old, replay=True)
                self.send(REPLY, {'button': 'late', 'values': {}}, request=1, generation=self.gen - 1, run=self.run - 1)
            if mode == 'wire_version':
                self.send(START, {'mode': 'apply'}, version=99)
            elif mode == 'wire_truncated':
                self.socket.write(struct.pack('<I', 100) + b'1234567890')
                self.socket.flush()
                self.socket.disconnectFromServer()
            elif mode == 'wire_oversized':
                self.socket.write(struct.pack('<I', LIMIT + 1))
                self.socket.flush()
            else:
                self.send(START, {'mode': mode})
                run = self.run
                if mode == 'cooperative':
                    QTimer.singleShot(300, lambda: self.cancel(run))
                elif mode == 'stuck':
                    QTimer.singleShot(350, lambda: self.force_stop(run))

        def auto_reply(self, run):
            if self.current and self.run == run and self.pending:
                QMetaObject.invokeMethod(root, 'autoReply')

        @Slot('QVariant', 'QVariantMap')
        def reply(self, button, values):
            if not self.pending:
                return
            msg, self.pending = self.pending, None
            if msg['generation'] != self.gen or msg['run'] != self.run:
                self.rejections.append({'where': 'GUI reply', 'reason': 'stale target'})
                return
            self.current['qml_reply'] = {'button': button, 'values': values,
                                         'python_types': {k: type(v).__name__ for k, v in values.items()}}
            dialog_object = root.findChild(QObject, 'syntheticScriptDialog')
            self.current['qml_dialog_class'] = dialog_object.metaObject().className() if dialog_object else None
            meta = dialog_object.metaObject() if dialog_object else None
            self.current['qml_dialog_inheritance'] = []
            while meta:
                self.current['qml_dialog_inheritance'].append(meta.className())
                meta = meta.superClass()
            self.current['heartbeat_ticks_while_dialog_waited'] = self.ticks - self.current['dialog_tick']
            if self.current['mode'] == 'cancel_dialog':
                self.send(CANCEL)
            reply = {'button': button, 'values': values}
            self.send(REPLY, reply, msg['request'])
            if self.current['name'] == 'dialog_one':
                self.send(REPLY, reply, msg['request'])  # Controlled duplicate.

        def cancel(self, run):
            if self.current and self.run == run:
                self.current['cancel_sent'] = True
                self.send(CANCEL)

        def force_stop(self, run):
            if self.current and self.run == run and self.process:
                self.current['kill_requested_for_owned_pid'] = int(self.process.processId())
                self.record_event('owned_process_kill', pid=int(self.process.processId()))
                self.process.kill()

        def timeout(self, run):
            if self.current and self.run == run:
                self.current['harness_deadline_exceeded'] = True
                self.current['expects_exit'] = True
                self.process.kill()

        def exited(self, proc, record, code, status):
            self.capture(proc, record, 'stdout')
            self.capture(proc, record, 'stderr')
            record['exit_code'], record['exit_status'] = code, str(status)
            self.record_event('process_exit', generation=record['generation'], code=code, status=str(status))
            if proc is not self.process:
                return
            self.process = None
            if self.socket:
                self.socket.close()
                self.socket = None
            self.server.close()
            if self.current:
                self.current['exit_code'] = code
                self.current['exit_status'] = str(status)
                self.current['terminal'] = 'helper_exit'
                self.current['stderr'] = record['stderr']
                self.finish_case()

        def finish_case(self):
            row = self.current
            row['elapsed_ms'] = round((time.perf_counter() - row.pop('start')) * 1000, 3)
            row['heartbeat_ticks'] = self.ticks - row['start_ticks']
            row['generation'] = self.gen
            self.results.append(row)
            self.current = None
            self.pending = None
            root.setProperty('dialogVisible', False)
            QTimer.singleShot(100, self.advance)

    app = QGuiApplication([])
    probe = Probe()
    engine = QQmlApplicationEngine()
    engine.rootContext().setContextProperty('probe', probe)
    engine.load(QUrl.fromLocalFile(str(HERE / 'Dialog.qml')))
    if not engine.rootObjects():
        raise RuntimeError('QML failed to load')
    root = engine.rootObjects()[0]
    QTimer.singleShot(100, probe.advance)
    app.exec()
    if probe.process:
        probe.process.kill()
        probe.process.waitForFinished(3000)
    if not probe.done:
        raise RuntimeError('Run interrupted before all scenarios completed')
    runtime_dir = Path(PySide6.__file__).parent
    runtime_files = [Path(sys.executable)] + [runtime_dir / n for n in ['Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Qml.dll', 'Qt6Quick.dll', 'Qt6Network.dll']]
    report = {'utc': datetime.now(timezone.utc).isoformat(), 'platform': platform.platform(),
              'python': sys.version, 'pyside': PySide6.__version__, 'qt': qVersion(),
              'qpa': app.platformName(), 'quick_backend_requested': os.environ.get('QT_QUICK_BACKEND'),
              'runtime_sha256': {str(p): sha(p) for p in runtime_files},
              'gui_loaded_modules_after_scenarios': loaded_modules(os.getpid()),
              'cases': probe.results, 'sessions': probe.sessions, 'rejections': probe.rejections,
              'events': probe.events, 'qml_heartbeat_count': probe.ticks,
              'experiment_limits': {'frame_bytes': LIMIT, 'value_depth': 12, 'table_entries': 128,
                                    'stdout_stderr_chars_each': 131072, 'case_watchdog_ms': 10000,
                                    'auto_dialog_reply_ms': 300, 'owned_force_stop_ms': 350},
              'limits_note': 'Harness bounds and delays are experiment parameters, not accepted product policy or calibrated responsiveness thresholds.'}
    (out / 'events.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    return report


def observations(report):
    c = {r['name']: r for r in report['cases']}
    expected = {'text': '静かな港 🌙', 'count': 37, 'enabled': True, 'choice': 'beta'}
    return {
        'all_scenarios_completed_without_watchdog': len(c) == 16 and not any(r.get('harness_deadline_exceeded') for r in c.values()),
        'luajit_ffi_pid_agrees_with_owned_process': all(s.get('runtime', {}).get('ffi_pid') == s.get('process_id') and s.get('runtime', {}).get('version', '').startswith('LuaJIT') for s in report['sessions']),
        'two_invocations_retain_experimental_state': c['dialog_one'].get('value', {}).get('counter') == 1 and c['dialog_two'].get('value', {}).get('counter') == 2,
        'typed_qml_readback_matches_native_lua': c['dialog_one'].get('value', {}).get('values') == expected and c['dialog_one'].get('qml_reply', {}).get('values') == expected,
        'actual_qml_dialog_constructed': 'QQuickDialog' in c['dialog_one'].get('qml_dialog_inheritance', []),
        'qml_ticks_while_lua_waits': all(r.get('heartbeat_ticks_while_dialog_waited', 0) > 0 for r in c.values() if 'qml_reply' in r),
        'custom_cancel_is_string': c['custom_cancel'].get('value', {}).get('button') == 'Cancel' and c['custom_cancel'].get('value', {}).get('button_type') == 'string',
        'default_ok_is_empty_string': c['default_ok'].get('value', {}).get('button') == '' and c['default_ok'].get('value', {}).get('button_type') == 'string',
        'default_close_false_with_values': c['default_close'].get('value', {}).get('button') is False and c['default_close'].get('value', {}).get('values') == expected,
        'cancel_while_waiting_reaches_lua': c['cancel_dialog'].get('value', {}).get('cancelled') is True and c['cancel_dialog'].get('value', {}).get('button') is False,
        'unchanged_edgeblur_selected_only_and_noop_undo': c['unchanged_edgeblur'].get('value', {}).get('selected') == '{\\be1}Harbor' and c['unchanged_edgeblur'].get('value', {}).get('unselected') == 'Unselected' and c['unchanged_edgeblur'].get('value', {}).get('writebacks') == 1 and c['unchanged_edgeblur'].get('value', {}).get('undo_stub_calls') == 1,
        'lua_error_is_terminal_diagnostic': c['lua_error'].get('terminal') == 'lua_error' and 'original fixture error' in c['lua_error'].get('value', ''),
        'cooperative_cancel_at_polled_boundary': c['cooperative_cancel'].get('cancel_sent') is True and 'fixture cooperative cancellation' in c['cooperative_cancel'].get('value', ''),
        'owned_stuck_helper_terminated': bool(c['owned_force_stop'].get('kill_requested_for_owned_pid')) and c['owned_force_stop'].get('terminal') == 'helper_exit',
        'restart_does_not_restore_lost_lua_state': c['restart_after_force_stop'].get('value', {}).get('counter') == 1 and c['restart_after_loss'].get('value', {}).get('counter') == 1,
        'abrupt_helper_loss_observed': c['abrupt_helper_loss'].get('exit_code') == 77,
        'separate_stdout_contains_native_and_lua_noise': 'C_STDOUT' in report['sessions'][0]['stdout'] and 'LUA_STDOUT' in report['sessions'][0]['stdout'] and 'LUA_STDERR' in report['sessions'][0]['stderr'],
        'controlled_stale_generation_rejected_on_both_sides': any(r['where'] == 'GUI' and r.get('controlled_replay') for r in report['rejections']) and any(r['where'] == 'helper' and 'stale generation' in r['reason'] for r in report['rejections']),
        'controlled_duplicate_reply_rejected': any(r['where'] == 'helper' and 'duplicate' in r['reason'] for r in report['rejections']),
        'version_mismatch_rejected': 'protocol version mismatch' in c['version_mismatch'].get('stderr', ''),
        'truncated_frame_rejected': 'truncated frame body' in c['truncated_frame'].get('stderr', ''),
        'oversized_frame_rejected': 'invalid bounded frame length' in c['oversized_frame'].get('stderr', '')}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, default=Path('C:/Work/Kainote'))
    parser.add_argument('--visible', action='store_true', help='Show the automatically driven QML fixture; not manual interaction qualification.')
    args = parser.parse_args()
    if os.name != 'nt':
        raise SystemExit('Windows-only experiment; no Linux result.')
    source = args.source.resolve()
    out = HERE / '_run' / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    out.mkdir(parents=True)
    edge = source / 'Automation/automation/Autoload/macro-1-edgeblur.lua'
    before = sha(edge)
    exe, provenance = build(source, out)
    report = execute(exe, source, out, args.visible)
    report['provenance'] = provenance
    report['edgeblur'] = {'path': str(edge), 'before_sha256': before, 'after_sha256': sha(edge),
                         'unchanged_source': before == sha(edge), 'license_note': 'Read existing bundled source in place; no script copy or modification.'}
    report['observations'] = observations(report)
    report['observations']['bundled_script_source_unchanged'] = report['edgeblur']['unchanged_source']
    report['proof_limits'] = [
        'Windows offscreen QML event loop and original fixtures only; no OS focus, IME, screen reader, GPU or calibrated latency qualification.',
        'One experimental persistent Lua state, not accepted helper lifetime/concurrency. No authoritative Document, native application undo, transaction atomicity or complete userdata implementation.',
        'Edgeblur source unchanged, but only a two-record proxy and minimal host functions. Synthetic dialog is separate, not an unchanged bundled dialog compatibility run.',
        'Abrupt loss is an explicit TerminateProcess call in the owned helper, not a memory-corruption/SEH crash or external-side-effect rollback proof.',
        'Stale result and duplicate reply are controlled replays; cancellation/exit/IPC actions execute natively. No exhaustive scheduling/protocol fuzzing or security sandbox claim.',
        'No Linux, native third-party Lua modules, DependencyControl, media/font/clipboard IPC, complete controls/coercions, all ASS/scripts or production deployment proof.',
        'Existing hashed Debug archive plus installed MSVC2026 is prototype provenance, not reproducible source attestation or the accepted official Qt/MSVC2022 build workflow.']
    evidence = HERE / 'evidence'
    evidence.mkdir(exist_ok=True)
    (out / 'observations.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    (evidence / 'observations.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    rows = ''.join('<tr><td>' + html.escape(k.replace('_', ' ')) + '</td><td>' + ('Observed' if v else 'NOT substantiated') + '</td></tr>' for k, v in report['observations'].items())
    cases = ''.join('<tr><td>' + html.escape(c['name']) + '</td><td>' + html.escape(c.get('terminal', '')) + '</td><td>' + str(c['heartbeat_ticks']) + '</td><td><pre>' + html.escape(json.dumps(c.get('value', {'exit_code': c.get('exit_code')}), ensure_ascii=False, indent=2)) + '</pre></td></tr>' for c in report['cases'])
    page = '''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Lua isolation — native evidence</title><style>body{font:15px system-ui;background:#15181e;color:#e8ebf2;max-width:1150px;margin:32px auto;padding:0 20px}h1,h2{color:#c6bcff}table{width:100%;border-collapse:collapse}td,th{padding:9px;text-align:left;border-bottom:1px solid #414753;vertical-align:top}pre{white-space:pre-wrap;overflow-wrap:anywhere;font-size:12px}li{margin:8px 0}a{color:#aebeff}.note{padding:16px;border:1px solid #8173ba;background:#252337}</style><h1>LuaJIT helper → typed QML dialog</h1><p class="note">Bounded Windows native experiment. Real LuaJIT and named-pipe messages; original synthetic QML controls. No production host, lifetime, transaction or UI-policy acceptance follows.</p>'''
    page += '<p>' + html.escape(report['utc'] + ' · ' + report['platform'] + ' · Qt ' + report['qt'] + ' · ' + report['qpa']) + '</p>'
    page += '<p><a href="README.md">Run guide / source and limits</a> · <a href="evidence/observations.json">Exact observations</a></p><h2>Observed checks</h2><table>' + rows + '</table>'
    page += '<h2>Scenario results</h2><p>Heartbeat counts show QML event-loop activity during each scenario; they are not calibrated latency or OS-input measurements.</p><table><tr><th>Fixture</th><th>Outcome</th><th>QML ticks</th><th>Lua result / process exit</th></tr>' + cases + '</table>'
    page += '<h2>Proof limits</h2><ul>' + ''.join('<li>' + html.escape(s) + '</li>' for s in report['proof_limits']) + '</ul>'
    page += '<details><summary>Full provenance, events and stdout/stderr</summary><pre>' + html.escape(json.dumps(report, ensure_ascii=False, indent=2)) + '</pre></details></html>'
    (HERE / 'report.html').write_text(page, encoding='utf-8')
    print(json.dumps({'scenarios': len(report['cases']), 'observations': report['observations'],
                      'report': str(HERE / 'report.html'), 'run': str(out)}, indent=2))
    return 0 if all(report['observations'].values()) else 2


if __name__ == '__main__':
    raise SystemExit(main())
