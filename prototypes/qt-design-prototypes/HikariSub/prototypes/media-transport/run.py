"""Bounded functional Qt/PortAudio transport probe. Silence only; not a benchmark."""
import argparse
from array import array
from concurrent.futures import ThreadPoolExecutor
import ctypes as C
from datetime import datetime, timezone
import hashlib
import html
import importlib.metadata
import importlib.util
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import threading
import time

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QT_QUICK_BACKEND", "software")
os.environ.setdefault("QT_MEDIA_BACKEND", "ffmpeg")
from PySide6.QtCore import QUrl
from PySide6.QtGui import QGuiApplication, QImage, QFontDatabase
from PySide6.QtQml import QQmlApplicationEngine
from PySide6.QtQuick import QQuickImageProvider
from PySide6.QtMultimedia import QMediaPlayer, QAudioOutput, QVideoSink, QAudioBufferOutput, QAudioFormat, QMediaDevices
import sounddevice as sd

HERE = Path(__file__).resolve().parent

def sha(data):
    return hashlib.sha256(data).hexdigest()

def digest(path):
    return sha(Path(path).read_bytes())

def execute(args, timeout=45):
    done = subprocess.run([str(arg) for arg in args], capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=timeout)
    return {"returncode":done.returncode,"stdout":done.stdout,"stderr":done.stderr,"argv":[str(arg) for arg in args]}

def modules():
    """Loaded paths, not PATH guesses. Only relevant native libraries are retained."""
    if os.name != "nt":
        return []
    kernel = C.WinDLL("kernel32", use_last_error=True)
    psapi = C.WinDLL("psapi", use_last_error=True)
    kernel.GetCurrentProcess.restype = C.c_void_p
    process = kernel.GetCurrentProcess()
    entries = (C.c_void_p * 2048)()
    size = C.c_ulong()
    psapi.EnumProcessModules.argtypes = [C.c_void_p,C.c_void_p,C.c_ulong,C.POINTER(C.c_ulong)]
    psapi.GetModuleFileNameExW.argtypes = [C.c_void_p,C.c_void_p,C.c_wchar_p,C.c_ulong]
    psapi.EnumProcessModules(process,entries,C.sizeof(entries),C.byref(size))
    result = []
    for module in entries[:size.value//C.sizeof(C.c_void_p)]:
        buf = C.create_unicode_buffer(32768)
        if psapi.GetModuleFileNameExW(process,module,buf,len(buf)):
            path = Path(buf.value)
            if any(key in path.name.lower() for key in ("ffmpeg","avcodec","avformat","avutil","swscale","swresample","portaudio","qtmultimedia")):
                result.append({"path":str(path),"sha256":digest(path),"bytes":path.stat().st_size})
    return result

def barcode(image, scale=1, offset_y=0):
    identity = 0
    for bit in range(8):
        pixel = image.pixelColor((18+20*bit)*scale, offset_y+16*scale)
        identity |= int(pixel.red()+pixel.green()+pixel.blue()>384) << bit
    return identity

class Provider(QQuickImageProvider):
    def __init__(self):
        super().__init__(QQuickImageProvider.Image)
        self.images = {}
    def requestImage(self, name, size, requested):
        image = self.images[name]
        if size is not None:
            size.setWidth(image.width()); size.setHeight(image.height())
        return image.copy()

class Probe:
    def __init__(self, app, folder):
        self.app, self.folder = app, folder
        self.generation = 1
        self.owner = None
        self.events = []
        self.players = []
        self.pool = ThreadPoolExecutor(max_workers=2)
        self.provider = Provider()
        self.engine = QQmlApplicationEngine()
        self.engine.addImageProvider("owned",self.provider)
        self.engine.load(QUrl.fromLocalFile(str(HERE/"Presenter.qml")))
        if not self.engine.rootObjects():
            raise RuntimeError("Software presenter QML failed")
        self.window = self.engine.rootObjects()[0]
        self.swap_count = 0
        self.window.frameSwapped.connect(self.swapped)
        self.started = time.perf_counter()
    def swapped(self):
        self.swap_count += 1
    def event(self, kind, **values):
        self.events.append({"elapsed_ms":round((time.perf_counter()-self.started)*1000,3),"generation":self.generation,"owner":self.owner,"kind":kind,**values})
    def spin(self, predicate, timeout=6, minimum=0):
        start = time.perf_counter()
        while time.perf_counter()-start < timeout:
            self.app.processEvents()
            elapsed = time.perf_counter()-start
            if elapsed >= minimum and predicate():
                return True
            time.sleep(0.005)
        return False
    def require(self, predicate, label, timeout=6, minimum=0):
        if not self.spin(predicate,timeout,minimum):
            raise RuntimeError("Experiment bound reached: "+label)
    def present(self, image, generation, identity, name):
        # This is an actual owned QImage passed to a Qt Quick software scene, not a timer acknowledgement.
        key = str(generation)+"-"+name
        self.provider.images[key] = image.copy()
        before = self.swap_count
        self.window.setProperty("label",name+" · generation "+str(generation)+" · frame "+str(identity))
        self.window.setProperty("frameUrl","image://owned/"+key)
        self.require(lambda:self.window.property("imageReady"),"QML image ready",minimum=0.05)
        self.spin(lambda:self.swap_count>before,timeout=2,minimum=0.05)
        captured = self.window.grabWindow()
        observed = barcode(captured,2,60) if not captured.isNull() else None
        path = self.folder/(name+"-presented.png")
        saved = captured.save(str(path))
        result = {"generation":generation,"expected_frame_id":identity,"captured_frame_id":observed,
                  "software_capture_matches":observed==identity,"frameSwapped_delta":self.swap_count-before,
                  "capture":str(path),"saved":saved,"gpu_or_scanout_ack":False}
        self.event("software-presentation-observed",**result)
        if not result["software_capture_matches"]:
            raise RuntimeError("Software presenter did not show the expected frame identity")
        return result
    def player(self, media, generation):
        player = QMediaPlayer()
        output = QAudioOutput()
        output.setVolume(0.0); output.setMuted(True)
        sink = QVideoSink()
        audio_format = QAudioFormat()
        audio_format.setSampleRate(48000); audio_format.setChannelCount(1)
        audio_format.setSampleFormat(QAudioFormat.Int16)
        audio_buffers = QAudioBufferOutput(audio_format)
        player.setAudioOutput(output); player.setVideoSink(sink); player.setAudioBufferOutput(audio_buffers)
        records = {"fixture":str(media),"generation":generation,"phase":"load","frames":[],"audio_buffers":[],
                   "subtitles":[],"errors":[],"states":[],"images":{},"player":player,"output":output,"sink":sink,"bufferOutput":audio_buffers}
        def frame_received(frame):
            if not frame.isValid():
                return
            image = frame.toImage().convertToFormat(QImage.Format_RGBA8888).copy()
            if image.isNull():
                return
            data = bytes(image.constBits())
            row = {"arrival_ms":round((time.perf_counter()-self.started)*1000,3),"generation":generation,
                   "phase":records["phase"],"start_us":frame.startTime(),"end_us":frame.endTime(),
                   "frame_id":barcode(image),"player_position_ms":player.position(),"width":image.width(),"height":image.height(),
                   "pixel_format":str(frame.pixelFormat()),"handle_type":str(frame.handleType()),"sha256_rgba":sha(data),
                   "accepted_generation":generation==self.generation}
            records["frames"].append(row)
            if len(records["frames"]) > 600:
                records["frames"].pop(0)
            records["images"][row["frame_id"]] = image
            if generation != self.generation:
                self.event("old-player-frame-rejected",frame_id=row["frame_id"],source_generation=generation)
        def buffer_received(buffer):
            if not buffer.isValid() or not buffer.byteCount():
                return
            fmt = buffer.format()
            data = bytes(buffer.constData())
            samples = array("h")
            if fmt.sampleFormat()==QAudioFormat.Int16:
                samples.frombytes(data)
                if sys.byteorder!="little":
                    samples.byteswap()
            crossings = sum(1 for a,b in zip(samples,samples[1:]) if a<=0<b)
            records["audio_buffers"].append({"phase":records["phase"],"active_track_property":player.activeAudioTrack(),
                "start_us":buffer.startTime(),"frames":buffer.frameCount(),"rate":fmt.sampleRate(),"channels":fmt.channelCount(),
                "sample_format":str(fmt.sampleFormat()),"sha256":sha(data),
                "positive_crossings":crossings,"rough_frequency_hz":round(crossings*fmt.sampleRate()/len(samples),2) if samples else None})
        sink.videoFrameChanged.connect(frame_received)
        sink.subtitleTextChanged.connect(lambda value:records["subtitles"].append({"phase":records["phase"],"active_track":player.activeSubtitleTrack(),"text":value,"position_ms":player.position()}))
        audio_buffers.audioBufferReceived.connect(buffer_received)
        player.errorOccurred.connect(lambda error,message:records["errors"].append({"error":str(error),"message":message,"phase":records["phase"]}))
        player.playbackStateChanged.connect(lambda value:records["states"].append({"phase":records["phase"],"state":str(value),"position_ms":player.position()}))
        self.players.append(records)
        player.setSource(QUrl.fromLocalFile(str(media)))
        return records
    def stop(self, record, reason):
        player = record["player"]
        record["phase"] = reason
        self.event("general-stop-request",position_ms=player.position())
        player.stop()
        self.require(lambda:player.playbackState()==QMediaPlayer.StoppedState,"QMediaPlayer StoppedState")
        self.owner = None
        self.event("general-stopped-state",explicit_device_flush_ack=False,audible_tail_measured=False)
        player.setSource(QUrl())
        self.spin(lambda:player.mediaStatus()==QMediaPlayer.NoMedia,timeout=2)
        self.event("general-source-cleared",media_status=str(player.mediaStatus()))
    def inspect_player(self, record, pts):
        player, sink = record["player"],record["sink"]
        self.owner = "Qt general player (muted)"
        player.play()
        self.require(lambda:player.duration()>0 and len(player.audioTracks())>=2 and len(record["frames"])>0,"tracks and first delivered video")
        def metadata(items):
            return [{str(key):item.stringValue(key) for key in item.keys()} for item in items]
        result = {"duration_ms":player.duration(),"seekable":player.isSeekable(),
                  "tracks":{"audio":metadata(player.audioTracks()),"subtitle":metadata(player.subtitleTracks()),"video":metadata(player.videoTracks())},
                  "muted":record["output"].isMuted(),"application_volume":record["output"].volume(),
                  "audio_device":{"description":record["output"].device().description(),"id_hex":bytes(record["output"].device().id()).hex()},
                  "audio_selection":[],"subtitle_selection":[],"seeks":[]}
        for track in range(min(2,len(player.audioTracks()))):
            record["phase"] = "audio-track-"+str(track)
            player.setActiveAudioTrack(track); player.setPosition(200); player.play()
            self.require(lambda:len([row for row in record["audio_buffers"] if row["phase"]==record["phase"]])>=6,"decoded audio buffers after track selection",minimum=0.2)
            player.pause()
            rows = [row for row in record["audio_buffers"] if row["phase"]==record["phase"]]
            settled = rows[-4:]
            count = sum(row["frames"] for row in settled)
            freq = sum(row["positive_crossings"] for row in settled)*48000/count if count else None
            result["audio_selection"].append({"requested":track,"active_property":player.activeAudioTrack(),
                "decoded_buffer_count":len(rows),"last_four_rough_frequency_hz":freq,
                "expected_fixture_frequency_hz":[440,660][track],"acoustic_measurement":False})
        for track in range(min(2,len(player.subtitleTracks()))):
            record["phase"] = "subtitle-track-"+str(track)
            player.setActiveSubtitleTrack(track); player.setPosition(400); player.play()
            self.spin(lambda:player.position()>=1000,timeout=3,minimum=0.65)
            player.pause()
            result["subtitle_selection"].append({"requested":track,"active_property":player.activeSubtitleTrack(),
                "sink_text":sink.subtitleText(),"events":[row for row in record["subtitles"] if row["phase"]==record["phase"]]})
        record["phase"] = "subtitles-disabled"
        player.setActiveSubtitleTrack(-1); player.setPosition(400); player.play()
        self.spin(lambda:player.position()>=1100,timeout=3,minimum=0.75)
        player.pause()
        result["subtitle_disable"] = {"active_property":player.activeSubtitleTrack(),"sink_text":sink.subtitleText(),
            "events":[row for row in record["subtitles"] if row["phase"]==record["phase"]],
            "nonempty_events_after_disable":[row for row in record["subtitles"] if row["phase"]==record["phase"] and row["text"]]}
        for target in (1035,1980,20,3500,1000):
            record["phase"] = "seek-"+str(target)
            before = len(record["frames"])
            player.setPosition(target); player.play()
            self.require(lambda:len(record["frames"])>before,"new delivered frame after time seek "+str(target))
            first = record["frames"][before]
            def near_request(row):
                if row["end_us"] > row["start_us"]:
                    return row["end_us"] > target*1000 and row["start_us"] <= (target+100)*1000
                # Zero / unknown duration is not a containing-frame proof; inspect the next start.
                return target*1000 <= row["start_us"] <= (target+100)*1000
            near_arrived = self.spin(lambda:any(near_request(row) for row in record["frames"][before:]),timeout=2)
            player.pause()
            delivered = record["frames"][before:]
            settled = next((row for row in delivered if near_request(row)),None)
            expected = max(index for index,start in enumerate(pts) if start<=target)
            result["seeks"].append({"requested_ms":target,"expected_containing_fixture_index":expected,
                "first_delivered":first,"observed_identity_matches_containing":first["frame_id"]==expected,
                "delivered_start_offset_ms":first["start_us"]/1000-target,
                "near_request_arrived":near_arrived,"near_request_frame":settled,
                "post_request_arrivals":delivered,
                "near_request_matches_containing":bool(settled and settled["frame_id"]==expected),
                "ack_sampling_policy":"known duration: end_us > target_us and start_us <= target_us + 100000; unknown duration: target_us <= start_us <= target_us + 100000; near-window sampling, not containment",
                "first_frame_duration_known":first["end_us"]>first["start_us"],
                "exact_indexed_seek_claim":False})
        return result

def silence_probe(probe):
    """The sole device stream opened here is OUTPUT ONLY, filled with zero bytes."""
    device = sd.query_devices(kind="output")
    rate = int(device["default_samplerate"])
    channels = min(2,device["max_output_channels"])
    frames_per_buffer = 256
    target_frames = int(rate*0.45)
    zeroes = bytes(frames_per_buffer*channels*4)
    callbacks = []
    finished = threading.Event()
    total = 0
    def callback(outdata, frames, timing, status):
        nonlocal total
        # Python bookkeeping is deliberate probe instrumentation, not a hard-real-time callback design.
        outdata[:] = zeroes if len(outdata)==len(zeroes) else bytes(len(outdata))
        callbacks.append({"ordinal":len(callbacks),"frames":frames,"stream_current_time":timing.currentTime,
            "output_buffer_dac_time":timing.outputBufferDacTime,"status":str(status),"output_underflow":status.output_underflow,
            "bytes_filled":len(outdata),"all_zero":bytes(outdata)==bytes(len(outdata)),
            "logical_media_start_us":1000000+total*1000000/rate})
        total += frames
        if total>=target_frames:
            raise sd.CallbackStop
    result = {"device":dict(device),"host_api":dict(sd.query_hostapis(device["hostapi"])),"portaudio_version":sd.get_portaudio_version(),
        "sample_rate":rate,"channels":channels,"dtype":"float32","requested_blocksize":frames_per_buffer,
        "input_opened":False,"loopback_opened":False,"os_volume_changed":False,"output_content":"zero bytes only",
        "calibrated_latency_drift_or_acoustic_measurement":False}
    result["logical_media_anchor_us"] = 1000000
    result["anchor_limit"] = "Scheduled silence is mapped to the requested 1s handoff point; no source PCM or resampling is played."
    probe.event("portaudio-output-only-open-request")
    stream = None
    try:
        stream = sd.RawOutputStream(device=device["index"],samplerate=rate,channels=channels,dtype="float32",
            blocksize=frames_per_buffer,callback=callback,finished_callback=finished.set)
        result["reported_output_latency_seconds"] = stream.latency
        stream.start()
        probe.owner = "PortAudio editor output (silence)"
        probe.require(lambda:len(callbacks)>0,"first PortAudio silence callback",timeout=3)
        probe.event("portaudio-first-callback",clock_estimate=callbacks[0]["output_buffer_dac_time"],acoustic_clock=False)
        probe.require(lambda:finished.is_set(),"bounded PortAudio callback stop/drain",timeout=4)
        stream.stop()
        result["active_after_stop"] = stream.active
        result["stopped_after_stop"] = stream.stopped
        result["stream_time_after_stop"] = stream.time
        result["callbacks"] = callbacks
        result["frames_written"] = total
        result["all_callbacks_silent"] = all(row["all_zero"] for row in callbacks)
        result["underflow_callbacks"] = sum(row["output_underflow"] for row in callbacks)
        probe.event("portaudio-stop-returned",active=stream.active,stopped=stream.stopped,audible_tail_measured=False)
    except Exception as error:
        result["error"] = repr(error)
    finally:
        if stream is not None:
            stream.close()
        probe.owner = None
        probe.event("portaudio-closed-clock-invalid")
    return result

def choose_fixtures(source, folder, explicit):
    prior = HERE.parent/"media-handoff"
    candidates = sorted((prior/"_run").glob("*/cfr.mkv"))
    if explicit:
        chosen = Path(explicit).resolve()
    elif candidates:
        chosen = candidates[-1].parent
    else:
        chosen = folder/"generated"
        chosen.mkdir()
        spec = importlib.util.spec_from_file_location("prior_fixture_generator",prior/"run.py")
        module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        module.fixtures(chosen,source/"Thirdparty/ffmpeg/bin/ffmpeg.exe",source/"Thirdparty/ffmpeg/bin/ffprobe.exe")
    return chosen

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--source",type=Path,default=Path("C:/Work/Kainote"))
    parser.add_argument("--fixtures",type=Path)
    args=parser.parse_args()
    stamp=datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    folder=HERE/"_run"/stamp; folder.mkdir(parents=True)
    source=args.source.resolve()
    fixtures=choose_fixtures(source,folder,args.fixtures)
    install=Path(os.environ["LOCALAPPDATA"])/"HikariSub/prototype-runtime/media-install-report.json"
    installation=json.loads(install.read_text(encoding="utf-8-sig"))
    report={"observed_utc":stamp,"python":sys.version,"executable":sys.executable,"platform":platform.platform(),
        "fixture_directory":str(fixtures),"rights":"Prior original generated CC0-1.0 pixels/tones/subtitles; no external media.",
        "installation_report_sha256":digest(install),
        "packages":[{"name":row["metadata"]["name"],"version":row["metadata"]["version"],
            "wheel_url":row["download_info"]["url"],"sha256":row["download_info"]["archive_info"]["hashes"]["sha256"]} for row in installation["install"]],
        "qt_backend_requested":os.environ["QT_MEDIA_BACKEND"],"presentation_backend":os.environ["QT_QUICK_BACKEND"],
        "fixtures":{},"handoff":{},"safety":{"qt_application_volume":0,"qt_muted":True,"portaudio_zero_output_only":True,"microphone":False,"loopback":False},
        "missing_gates":["real Windows visible-window/GPU presentation","Linux","calibrated latency/drift/acoustic stop tails",
            "libass/QSG color-alpha composition","device loss/reopen/default following","production callback hard-real-time guarantees",
            "chapter UI/transport API","ten-minute 1080p/reference-machine performance"]}
    report["installed_qt_packages"] = {name:importlib.metadata.version(name) for name in ("PySide6-Essentials","PySide6-Addons","shiboken6")}
    drivers=execute(["powershell","-NoProfile","-Command","Get-CimInstance Win32_PnPSignedDriver | Where-Object { $_.DeviceClass -eq 'MEDIA' } | Select-Object DeviceName,DriverVersion,DriverProviderName,InfName | ConvertTo-Json"])
    report["windows_audio_driver_inventory"] = json.loads(drivers["stdout"]) if drivers["returncode"]==0 else drivers
    report["driver_inventory_limit"] = "PnP media-driver inventory is not an endpoint-to-driver routing trace."
    app=QGuiApplication(sys.argv[:1])
    font_dir=Path(os.environ.get("WINDIR","C:/Windows"))/"Fonts"
    for name in ("segoeui.ttf","segoeuib.ttf"):
        if (font_dir/name).exists():
            QFontDatabase.addApplicationFont(str(font_dir/name))
    probe=Probe(app,folder)
    try:
        for kind in ("cfr","vfr"):
            media=fixtures/(kind+".mkv")
            ffprobe=execute([source/"Thirdparty/ffmpeg/bin/ffprobe.exe","-v","error","-show_streams","-show_chapters","-show_frames","-select_streams","v:0","-of","json",media])
            parsed=json.loads(ffprobe["stdout"])
            pts=[round(float(frame["pts_time"])*1000) for frame in parsed["frames"]]
            record=probe.player(media,probe.generation)
            entry={"fixture_sha256":digest(media),"qt":probe.inspect_player(record,pts),"ffprobe_chapters":parsed.get("chapters",[])}
            entry["scope"] = {"same_position_handoff_case":kind=="cfr","indexed_owned_comparison_frame":25,
                              "indexed_frame_start_ms":pts[25],"general_last_requested_ms":1000}
            report["fixtures"][kind]=entry
            image=record["images"].get(25)
            if kind=="cfr" and image is not None:
                entry["qt_owned_frame25_presentation"]=probe.present(image,probe.generation,25,"general-cfr-25")
            # Actual captured CPU work is deliberately held until its generation is invalidated.
            old_generation=probe.generation
            gate=threading.Event()
            last_identity=record["frames"][-1]["frame_id"]
            late_image=record["images"][last_identity]
            owned=bytes(late_image.constBits())
            qt25_hash=sha(bytes(image.constBits())) if image is not None else None
            def delayed_owned_completion(data=owned,generation=old_generation,release=gate):
                release.wait(5)
                return {"source_generation":generation,"sha256_owned_rgba":sha(data),"bytes":len(data)}
            stale=probe.pool.submit(delayed_owned_completion)
            probe.stop(record,"handoff-stop")
            probe.generation+=1
            probe.event("generation-invalidated",old_generation=old_generation)
            gate.set()
            probe.require(stale.done,"old-generation CPU completion")
            completion=stale.result()
            completion["accepted"]=completion["source_generation"]==probe.generation
            completion["controlled_delay_of_real_owned_frame"]=True
            completion["actual_frame_id"]=last_identity
            probe.event("late-owned-completion-rejected",**completion)
            entry["late_completion"]=completion
            child_output=folder/(kind+"-ffms.json")
            future=probe.pool.submit(execute,[sys.executable,HERE/"ffms_child.py",source/"x64/Debug/FFMS2.dll",media,child_output,"decode"],45)
            probe.event("indexed-child-decode-request",expected_frame_id=25,process_isolation=True,
                        purpose="same-position CFR handoff" if kind=="cfr" else "VFR owned-frame comparison; not same-position handoff")
            probe.require(future.done,"isolated FFMS2 child",timeout=46)
            child_process=future.result()
            if child_process["returncode"]:
                raise RuntimeError("FFMS child failed: "+child_process["stderr"])
            child=json.loads(child_output.read_text(encoding="utf-8"))
            bgra=(folder/(kind+"-frame25.bgra")).read_bytes()
            native25=child["sequential"][25]
            indexed={"frame_id":native25["pixel_barcode_id"],"sha256_bgra":sha(bgra),
                     "copied_bytes_match_child_record":sha(bgra)==native25["sha256_bgra"],
                     "child_modules":child["loaded_native_modules"],"child_output":str(child_output),
                     "all_sequential_identity_matches":all(row["requested_index"]==row["pixel_barcode_id"] for row in child["sequential"]),
                     "all_random_hash_matches":all(row["matches_sequential"] for row in child["random_access"])}
            indexed["same_identity_as_qt_owned_frame25"] = bool(image is not None and barcode(image)==native25["pixel_barcode_id"])
            indexed["qt_owned_frame25_survived_source_clear_and_child"] = bool(image is not None and sha(bytes(image.constBits()))==qt25_hash)
            indexed["cross_backend_pixel_color_equality_claim"] = False
            entry["indexed_child"]=indexed
            probe.event("indexed-owned-cpu-frame-ready",frame_id=indexed["frame_id"],bytes=len(bgra))
            indexed_image=QImage(bgra,320,180,1280,QImage.Format_ARGB32).copy()
            indexed["software_presentation"]=probe.present(indexed_image,probe.generation,25,"indexed-"+kind+"-25")
            if kind=="cfr":
                report["portaudio"]=silence_probe(probe)
                probe.generation+=1
                probe.event("editor-clock-invalidated-before-general-return")
                returned=probe.player(media,probe.generation)
                returned["phase"]="general-return"
                returned["player"].play()
                probe.require(lambda:returned["player"].duration()>0,"return player load")
                returned["player"].setActiveSubtitleTrack(-1)
                returned["player"].setPosition(1000)
                probe.require(lambda:any(row["frame_id"]==25 for row in returned["frames"]),"general return delivered frame25")
                returned["player"].pause()
                report["handoff"]["general_return_presentation"]=probe.present(returned["images"][25],probe.generation,25,"general-return-25")
                probe.owner="Qt general player (muted)"
                probe.event("general-clock-resumed-after-software-ack",actual_frame_id=25)
                returned["player"].play()
                probe.spin(lambda:True,minimum=0.08)
                probe.stop(returned,"return-stop")
                probe.generation+=1
            entry["qt"]["frames"]=record["frames"]
            entry["qt"]["audio_buffers"]=record["audio_buffers"]
            entry["qt"]["subtitle_events"]=record["subtitles"]
            entry["qt"]["errors"]=record["errors"]
            entry["qt"]["states"]=record["states"]
        report["completed"]=True
        report["handoff"].update({"logical_stop_generation_and_software_frame_ack_exercised":True,
            "handoff_fixture":"cfr.mkv","requested_media_time_ms":1000,"requested_frame_id":25,
            "end_to_end_acoustic_or_gpu_handoff_qualified":False,"qt_explicit_device_flush_ack_available":False})
    except Exception as error:
        report["error"]=repr(error)
        report["completed"]=False
    finally:
        for record in probe.players:
            record["player"].stop()
        probe.owner=None
        report["events"]=probe.events
        report["loaded_native_modules"]=modules()
        report["final_clock_owner"]=None
        report["chapter_named_player_methods"]=[name for name in dir(QMediaPlayer) if "chapter" in name.lower()]
        probe.pool.shutdown(wait=True,cancel_futures=True)
        (folder/"observations.json").write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding="utf-8")
        (HERE/"observations.json").write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding="utf-8")
        build_report(report,folder)
    print(json.dumps({"completed":report["completed"],"error":report.get("error"),"folder":str(folder),
        "qt_fixture_count":len(report["fixtures"]),"portaudio_callbacks":len(report.get("portaudio",{}).get("callbacks",[])),
        "loaded_native_modules":[row["path"] for row in report["loaded_native_modules"]]},indent=2))
    return 0 if report["completed"] else 2

def build_report(report,folder):
    import base64
    overview='<h2>Observed operations</h2><table><tr><th>Fixture</th><th>Tracks</th><th>Decoded audio identities</th><th>Subtitle suppression</th></tr>'
    seek_rows=''
    for kind,entry in report.get("fixtures",{}).items():
        qt=entry["qt"]
        frequencies=', '.join(str(round(row["last_four_rough_frequency_hz"],2))+' Hz' for row in qt["audio_selection"])
        disabled=qt["subtitle_disable"]
        overview+='<tr><td>'+kind+'</td><td>'+str(len(qt["tracks"]["audio"]))+' audio / '+str(len(qt["tracks"]["subtitle"]))+' subtitles</td><td>'+frequencies+' (decoded PCM only)</td><td>active '+str(disabled["active_property"])+', '+str(len(disabled["nonempty_events_after_disable"]))+' nonempty later events</td></tr>'
        for row in qt["seeks"]:
            near=row.get("near_request_frame")
            seek_rows+='<tr><td>'+kind+'</td><td>'+str(row["requested_ms"])+'</td><td>'+str(row["first_delivered"]["frame_id"])+'</td><td>'+str(near["frame_id"] if near else None)+'</td><td>'+str(near["start_us"]/1000 if near else None)+'</td><td>'+str(row["expected_containing_fixture_index"])+'</td></tr>'
    overview+='</table><h2>Time seeks: delivered frames, not player-position claims</h2><p>The first post-request frame can precede the requested target. VFR frames reported zero duration, and 1035 ms delivered frame 23 at 1040 ms instead of the containing frame 22. Qt playback is not an exact indexed-step oracle. The near-request sampling window is an experiment criterion, not a product deadline.</p><table><tr><th>Fixture</th><th>Requested ms</th><th>First ID</th><th>Near-request ID</th><th>Delivered PTS ms</th><th>Containing fixture ID</th></tr>'+seek_rows+'</table>'
    pa=report.get("portaudio",{})
    overview+='<h2>Silent output and acknowledgement boundary</h2><p>'+html.escape(str(pa.get("device",{}).get("name","unavailable")))+' / '+html.escape(str(pa.get("host_api",{}).get("name","unknown")))+': '+str(len(pa.get("callbacks",[])))+' callbacks, '+str(pa.get("frames_written"))+' device frames; all callback buffers verified zero: '+str(pa.get("all_callbacks_silent"))+'. Reported underruns: '+str(pa.get("underflow_callbacks"))+'. Output latency reported by the API: '+str(pa.get("reported_output_latency_seconds"))+' seconds (not an acoustic measurement).</p><p>CFR 1-second sequence: player StoppedState → source cleared → generation invalidated → controlled late owned-CPU completion rejected → isolated FFMS frame 25 copied → actual software scene capture shows 25 → silent PortAudio callbacks → stop/close → new Qt source frame 25 captured → general clock resumed. There is no explicit Qt device-flush acknowledgement, calibrated tail measurement or GPU/scanout proof. VFR frame-25 comparison is not a same-position handoff.</p>'
    if pa.get("error"):
        overview+='<p>PortAudio error: '+html.escape(pa["error"])+'</p>'
    pictures=""
    for path in folder.glob("*-presented.png"):
        pictures+='<figure><img src="data:image/png;base64,'+base64.b64encode(path.read_bytes()).decode()+'"><figcaption>'+html.escape(path.name)+'</figcaption></figure>'
    body='<!doctype html><meta charset="utf-8"><title>Native media transport observations</title><style>body{background:#171b20;color:#e8edf2;font:14px Segoe UI,sans-serif;margin:25px}h1{color:#9cdbc9}p{max-width:1000px;line-height:1.5}pre{white-space:pre-wrap;overflow-wrap:anywhere;font-size:11px;background:#20262d;padding:16px}figure{display:inline-block;margin:8px}img{width:480px;max-width:100%}a{color:#9cdbc9}</style><h1>Actual Qt media + silent PortAudio probe</h1><p>Bounded functional evidence, not a latency / GPU / acoustic / Linux acceptance pass. General playback was muted at application volume zero. PortAudio opened output only and received zero-filled buffers. The screenshots are actual offscreen Qt Quick software presentations of owned decoded frames.</p><p>Historical missing-prerequisite findings in the earlier media-handoff probe remain valid as of that run. This continuation uses the newly installed pinned runtime.</p>'+pictures+'<details><summary>Full observed log</summary><pre>'+html.escape(json.dumps(report,indent=2,ensure_ascii=False))+'</pre></details>'
    body=body.replace('<details><summary>Full observed log',overview+'<details><summary>Full observed log')
    body=body.replace('</style>','table{border-collapse:collapse;margin:18px 0}th,td{padding:8px 12px;border:1px solid #414b57;text-align:left;font-size:12px}th{background:#29313a}</style>')
    (HERE/"report.html").write_text(body,encoding="utf-8")

if __name__=="__main__":
    raise SystemExit(main())
