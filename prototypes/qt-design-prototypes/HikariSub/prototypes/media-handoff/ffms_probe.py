"""Native FFMS2 slice, run in a disposable child by run.py. Not a player.

Only the listed prefix fields of ffms.h structs are read. ABI is restricted to
the observed x64 5.1.0 DLL; the driver records binary and header identities.
"""
import ctypes as C
import hashlib
import json
import os
import sys
from fractions import Fraction
from pathlib import Path


class Error(C.Structure):
    _fields_ = [('type', C.c_int), ('subtype', C.c_int), ('size', C.c_int), ('buffer', C.c_void_p)]


class Frame(C.Structure):
    _fields_ = [('data', C.c_void_p * 4), ('stride', C.c_int * 4)] + [
        (name, C.c_int) for name in ['width', 'height', 'format', 'scaled_width',
                                    'scaled_height', 'output_format', 'keyframe',
                                    'repeat', 'interlaced', 'top_field']
    ] + [('picture_type', C.c_char)] + [(name, C.c_int) for name in
        ['space', 'range', 'primaries', 'transfer', 'chroma']]


class VideoProperties(C.Structure):
    _fields_ = [(name, C.c_int) for name in ['fps_den', 'fps_num', 'rff_den', 'rff_num',
        'frames', 'sar_num', 'sar_den', 'crop_top', 'crop_bottom', 'crop_left', 'crop_right',
        'top_field', 'space', 'range']] + [('first_time', C.c_double), ('last_time', C.c_double)]


class AudioProperties(C.Structure):
    _fields_ = [(name, C.c_int) for name in ['format', 'rate', 'bits', 'channels']] + [
        ('layout', C.c_int64), ('samples', C.c_int64), ('first_time', C.c_double),
        ('last_time', C.c_double), ('last_end_time', C.c_double)]


class TimeBase(C.Structure):
    _fields_ = [('num', C.c_int64), ('den', C.c_int64)]


class FrameInfo(C.Structure):
    _fields_ = [('pts', C.c_int64), ('repeat', C.c_int), ('key', C.c_int), ('original_pts', C.c_int64)]


class Chapter(C.Structure):
    _fields_ = [('title', C.c_char_p), ('start', C.c_int64), ('end', C.c_int64)]


class Chapters(C.Structure):
    _fields_ = [('chapters', C.POINTER(Chapter)), ('count', C.c_int)]


PROGRESS = C.CFUNCTYPE(C.c_int, C.c_int64, C.c_int64, C.c_void_p)
SUBTITLE = C.CFUNCTYPE(C.c_int, C.c_int64, C.c_int64, C.c_int64, C.c_char_p, C.c_void_p)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def text(value):
    return value.decode('utf-8', 'replace') if value else None


def run(dll, media, output, mode):
    directory = os.add_dll_directory(str(dll.parent))
    lib = C.CDLL(str(dll))
    functions = {
        'FFMS_GetVersion': (C.c_int, []), 'FFMS_Init': (None, [C.c_int, C.c_int]),
        'FFMS_CreateIndexer': (C.c_void_p, [C.c_char_p, C.POINTER(Error)]),
        'FFMS_GetNumTracksI': (C.c_int, [C.c_void_p]),
        'FFMS_GetTrackTypeI': (C.c_int, [C.c_void_p, C.c_int]),
        'FFMS_GetTrackName': (C.c_char_p, [C.c_void_p, C.c_int]),
        'FFMS_GetTrackLanguage': (C.c_char_p, [C.c_void_p, C.c_int]),
        'FFMS_GetCodecNameI': (C.c_char_p, [C.c_void_p, C.c_int]),
        'FFMS_GetChapters': (C.POINTER(Chapters), [C.c_void_p]),
        'FFMS_FreeChapters': (None, [C.POINTER(C.POINTER(Chapters))]),
        'FFMS_TrackTypeIndexSettings': (None, [C.c_void_p, C.c_int, C.c_int, C.c_int]),
        'FFMS_SetProgressCallback': (None, [C.c_void_p, PROGRESS, C.c_void_p]),
        'FFMS_DoIndexing2': (C.c_void_p, [C.c_void_p, C.c_int, C.POINTER(Error)]),
        'FFMS_CancelIndexing': (None, [C.c_void_p]),
        'FFMS_DestroyIndex': (None, [C.c_void_p]),
        'FFMS_CreateVideoSource': (C.c_void_p, [C.c_char_p, C.c_int, C.c_void_p, C.c_int, C.c_int, C.POINTER(Error)]),
        'FFMS_DestroyVideoSource': (None, [C.c_void_p]),
        'FFMS_GetVideoProperties': (C.POINTER(VideoProperties), [C.c_void_p]),
        'FFMS_GetFrame': (C.POINTER(Frame), [C.c_void_p, C.c_int, C.POINTER(Error)]),
        'FFMS_GetPixFmt': (C.c_int, [C.c_char_p]),
        'FFMS_SetOutputFormatV2': (C.c_int, [C.c_void_p, C.POINTER(C.c_int), C.c_int, C.c_int, C.c_int, C.POINTER(Error)]),
        'FFMS_GetTrackFromVideo': (C.c_void_p, [C.c_void_p]),
        'FFMS_GetTimeBase': (C.POINTER(TimeBase), [C.c_void_p]),
        'FFMS_GetFrameInfo': (C.POINTER(FrameInfo), [C.c_void_p, C.c_int]),
        'FFMS_CreateAudioSource': (C.c_void_p, [C.c_char_p, C.c_int, C.c_void_p, C.c_int, C.POINTER(Error)]),
        'FFMS_GetAudioProperties': (C.POINTER(AudioProperties), [C.c_void_p]),
        'FFMS_GetAudio': (C.c_int, [C.c_void_p, C.c_void_p, C.c_int64, C.c_int64, C.POINTER(Error)]),
        'FFMS_DestroyAudioSource': (None, [C.c_void_p]),
        'FFMS_GetSubtitleFormat': (C.c_char_p, [C.c_void_p, C.c_int]),
        'FFMS_GetSubtitleExtradata': (C.c_char_p, [C.c_void_p, C.c_int]),
        'FFMS_GetSubtitles': (None, [C.c_void_p, C.c_int, SUBTITLE, C.c_void_p]),
    }
    for name, (result, args) in functions.items():
        fn = getattr(lib, name)
        fn.restype, fn.argtypes = result, args
    version = lib.FFMS_GetVersion()
    if version != 0x05010000:
        raise RuntimeError('This bounded ABI probe expects FFMS2 5.1.0; got ' + hex(version))
    lib.FFMS_Init(0, 0)
    error_buffer = C.create_string_buffer(4096)
    error = Error(0, 0, len(error_buffer), C.addressof(error_buffer))

    def error_value():
        return {'type': error.type, 'subtype': error.subtype, 'message': text(error_buffer.value)}

    def required(value, operation):
        if not value:
            raise RuntimeError(operation + ': ' + json.dumps(error_value()))
        return value

    result = {'mode': mode, 'runtime_version': hex(version), 'fixture': media.name,
              'clock_owner': None, 'clock_estimate': None, 'handoff': 'not executed'}
    kernel = C.WinDLL('kernel32', use_last_error=True)
    kernel.GetModuleHandleW.argtypes, kernel.GetModuleHandleW.restype = [C.c_wchar_p], C.c_void_p
    kernel.GetModuleFileNameW.argtypes = [C.c_void_p, C.c_wchar_p, C.c_uint]
    result['loaded_native_modules'] = []
    for name in ['FFMS2.dll', 'avcodec-61.dll', 'avformat-61.dll', 'avutil-59.dll',
                 'swscale-8.dll', 'swresample-5.dll']:
        handle = kernel.GetModuleHandleW(name)
        path_buffer = C.create_unicode_buffer(32768)
        if handle and kernel.GetModuleFileNameW(handle, path_buffer, len(path_buffer)):
            loaded_path = Path(path_buffer.value)
            result['loaded_native_modules'].append({'name': name, 'path': str(loaded_path),
                                                     'sha256': sha(loaded_path.read_bytes())})

    def save():
        output.write_text(json.dumps(result, indent=2), encoding='utf-8')

    indexer = required(lib.FFMS_CreateIndexer(os.fsencode(media), C.byref(error)), 'create indexer')
    if mode == 'subtitles':
        result['subtitle_tracks'] = []
        for track in range(lib.FFMS_GetNumTracksI(indexer)):
            if lib.FFMS_GetTrackTypeI(indexer, track) != 3:
                continue
            entry = {'track': track, 'format': text(lib.FFMS_GetSubtitleFormat(indexer, track)),
                     'extradata_cstring_sha256': sha(lib.FFMS_GetSubtitleExtradata(indexer, track) or b''),
                     'packets': []}
            result['subtitle_tracks'].append(entry)
            save()  # Preserve the operation being attempted even if native code fails.

            @SUBTITLE
            def callback(start, duration, total, line, opaque):
                entry['packets'].append({'start_raw': start, 'duration_raw': duration,
                                         'total_raw': total, 'text': text(line)})
                return 0
            lib.FFMS_GetSubtitles(indexer, track, callback, None)
            save()
        lib.FFMS_CancelIndexing(indexer)
        result['completed'] = True
        save()
        return
    result['tracks'] = [{'index': i, 'type': lib.FFMS_GetTrackTypeI(indexer, i),
                         'codec': text(lib.FFMS_GetCodecNameI(indexer, i)),
                         'name': text(lib.FFMS_GetTrackName(indexer, i)),
                         'language': text(lib.FFMS_GetTrackLanguage(indexer, i))}
                        for i in range(lib.FFMS_GetNumTracksI(indexer))]
    chapters = lib.FFMS_GetChapters(indexer)
    result['chapters'] = []
    if chapters:
        for i in range(chapters.contents.count):
            chapter = chapters.contents.chapters[i]
            result['chapters'].append({'title': text(chapter.title), 'start_ms': chapter.start, 'end_ms': chapter.end})
        lib.FFMS_FreeChapters(C.byref(chapters))
    progress = []

    @PROGRESS
    def progress_callback(current, total, opaque):
        progress.append([current, total])
        return int(mode == 'cancel')
    lib.FFMS_SetProgressCallback(indexer, progress_callback, None)
    lib.FFMS_TrackTypeIndexSettings(indexer, 1, 1, 0)
    save()
    index = lib.FFMS_DoIndexing2(indexer, 0, C.byref(error))
    # DoIndexing2 consumes/frees indexer on success and failure.
    result['index_progress_calls'] = len(progress)
    result['index_first_progress'] = progress[:3]
    if mode == 'cancel':
        result['index_returned'] = bool(index)
        result['error'] = error_value()
        result['cancel_observed'] = not index and error.type == 10 and len(progress) > 0
        result['completed'] = True
        if index:
            lib.FFMS_DestroyIndex(index)
        save()
        return
    required(index, 'index')
    video = required(lib.FFMS_CreateVideoSource(os.fsencode(media), 0, index, 1, 1, C.byref(error)), 'create video')
    formats = (C.c_int * 2)(lib.FFMS_GetPixFmt(b'bgra'), -1)
    if lib.FFMS_SetOutputFormatV2(video, formats, 320, 180, 0x10, C.byref(error)):
        raise RuntimeError('BGRA conversion: ' + json.dumps(error_value()))
    properties = lib.FFMS_GetVideoProperties(video).contents
    result['video'] = {key: getattr(properties, key) for key in ['frames', 'fps_num', 'fps_den', 'sar_num', 'sar_den']}
    result['video']['seek_mode'] = 'FFMS_SEEK_NORMAL (1)'
    track = lib.FFMS_GetTrackFromVideo(video)
    tb = lib.FFMS_GetTimeBase(track).contents
    result['video']['timebase_milliseconds'] = [tb.num, tb.den]

    def frame(number):
        f = required(lib.FFMS_GetFrame(video, number, C.byref(error)), 'frame ' + str(number)).contents
        # Own packed CPU bytes before the next decoder call; never retain library pointers.
        packed = b''.join(C.string_at(f.data[0] + y * f.stride[0], f.scaled_width * 4)
                          for y in range(f.scaled_height))
        identity = 0
        for bit in range(8):
            pos = (16 * f.scaled_width + 18 + bit * 20) * 4
            identity |= int(sum(packed[pos:pos + 3]) > 384) << bit
        info = lib.FFMS_GetFrameInfo(track, number).contents
        us = Fraction(info.pts * tb.num * 1000, tb.den)
        record = {'requested_index': number, 'pixel_barcode_id': identity, 'sha256_bgra': sha(packed),
                  'pts': info.pts, 'document_time_us_rational': [us.numerator, us.denominator],
                  'keyframe': info.key, 'picture_type': text(f.picture_type),
                  'source_color': {'space': f.space, 'range': f.range, 'primaries': f.primaries,
                                   'transfer': f.transfer, 'chroma': f.chroma}}
        return packed, record
    sequential = []
    saved = None
    for i in range(properties.frames):
        packed, record = frame(i)
        sequential.append(record)
        if i == 25:
            saved = packed
            (output.parent / (media.stem + '-frame25.bgra')).write_bytes(packed)
    result['sequential'] = sequential
    result['random_access'] = []
    for i in [99, 0, 50, 1, 98, 25, 24, 75, 10, 50]:
        packed, record = frame(i)
        record['matches_sequential'] = record['sha256_bgra'] == sequential[i]['sha256_bgra']
        result['random_access'].append(record)
    result['copied_frame_25_still_equal'] = sha(saved) == sequential[25]['sha256_bgra']
    bad = lib.FFMS_GetFrame(video, properties.frames, C.byref(error))
    result['out_of_range'] = {'requested_index': properties.frames, 'returned_frame': bool(bad), 'error': error_value()}
    lib.FFMS_DestroyVideoSource(video)
    result['copied_frame_survives_decoder_destruction'] = sha(saved) == sequential[25]['sha256_bgra']
    result['audio'] = []
    for t in result['tracks']:
        if t['type'] != 1:
            continue
        audio = required(lib.FFMS_CreateAudioSource(os.fsencode(media), t['index'], index, -3, C.byref(error)), 'audio source')
        prop = lib.FFMS_GetAudioProperties(audio).contents
        entry = {key: getattr(prop, key) for key in ['format', 'rate', 'bits', 'channels', 'samples']}
        entry['track'] = t['index']
        entry['range_sample_frames'] = [4800, 4864]
        size = 64 * prop.channels * (prop.bits // 8)
        pcm = C.create_string_buffer(size)
        if lib.FFMS_GetAudio(audio, pcm, 4800, 64, C.byref(error)):
            entry['error'] = error_value()
        else:
            entry['pcm_sha256'] = sha(pcm.raw)
            entry['first_16_bytes_hex'] = pcm.raw[:16].hex()
        result['audio'].append(entry)
        lib.FFMS_DestroyAudioSource(audio)
    lib.FFMS_DestroyIndex(index)
    result['completed'] = True
    save()
    directory.close()


if __name__ == '__main__':
    run(*(Path(p).resolve() for p in sys.argv[1:4]), sys.argv[4])
