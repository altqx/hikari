"""Original geometric font fixtures. No copied font outlines; CC0-1.0 dedication.

This deliberately tiny sfnt writer has no third-party Python dependencies.
The fonts are identity probes, not credible examples of language shaping.
"""
import struct
from pathlib import Path


def pack(fmt, *values):
    return struct.pack('>' + fmt, *values)


def checksum(data):
    data += b'\0' * (-len(data) % 4)
    return sum(struct.unpack('>' + 'I' * (len(data) // 4), data)) & 0xffffffff


def font(family, ps, width=450, chars=None, modern=None):
    chars = sorted(set(chars or range(32, 127)))
    # One empty glyph and one original four-point rectangle.
    glyph = pack('hhhhh', 1, 0, 0, width, 700)
    glyph += pack('HH', 3, 0) + bytes([1, 1, 1, 1])
    glyph += pack('hhhh', 0, width, 0, -width) + pack('hhhh', 0, 0, 700, 0)
    glyph += b'\0' * (-len(glyph) % 4)
    head = pack('IIIIHHqqhhhhHHhhh', 0x10000, 0x10000, 0, 0x5f0f3cf5,
                0, 1000, 0, 0, 0, 0, width, 700, 0, 8, 2, 1, 0)
    hhea = pack('IhhhHhhhhhhhhhhhH', 0x10000, 800, -200, 0, width+80,
                0, 0, width, 1, 0, 0, 0, 0, 0, 0, 0, 2)
    maxp = pack('IH' + 'H'*13, 0x10000, 2, 4, 1, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0)
    names = {1: family, 2: 'Regular', 3: ps+'-fixture-v1', 4: family+' Regular',
             5: 'Version 1.000', 6: ps, 13: 'Original geometric fixture, CC0-1.0',
             14: 'https://creativecommons.org/publicdomain/zero/1.0/'}
    if modern:
        names.update({16: modern, 17: 'Regular'})
    records, strings = b'', b''
    for name_id, text in sorted(names.items()):
        encoded = text.encode('utf-16-be')
        records += pack('HHHHHH', 3, 1, 0x409, name_id, len(encoded), len(strings))
        strings += encoded
    name = pack('HHH', 0, len(names), 6+12*len(names)) + records + strings
    # Format 4: single-codepoint segments with idDelta, no glyph arrays.
    codes = chars + [0xffff]
    n = len(codes)
    power = 2**(n.bit_length()-1)
    cmap4 = pack('HHHHHHH', 4, 16+8*n, 0, 2*n, 2*power, power.bit_length()-1, 2*n-2*power)
    cmap4 += pack('H'*n, *codes) + pack('H', 0) + pack('H'*n, *codes)
    cmap4 += pack('H'*n, *[((0 if c == 32 else 1)-c)&0xffff if c != 0xffff else 1 for c in codes])
    cmap4 += b'\0\0'*n
    cmap = pack('HHHHI', 0, 1, 3, 1, 12) + cmap4
    os2 = bytearray(78)
    struct.pack_into('>HhHHH', os2, 0, 0, width, 400, 5, 0)
    struct.pack_into('>hhhhhhhhhh', os2, 10, 650, 600, 0, 75, 650, 600, 0, 350, 50, 250)
    os2[58:62] = b'HKPR'
    struct.pack_into('>HHHhhhHH', os2, 62, 0x40, min(chars), max(chars), 800, -200, 0, 800, 200)
    tables = {'head': head, 'hhea': hhea, 'maxp': maxp, 'hmtx': pack('HhHh', width+80, 0, width+80, 0),
              'loca': pack('III', 0, 0, len(glyph)), 'glyf': glyph, 'cmap': cmap, 'name': name,
              'OS/2': bytes(os2), 'post': pack('IihhIIIII', 0x30000, 0, -75, 50, 0, 0, 0, 0, 0)}
    n = len(tables)
    power = 2**(n.bit_length()-1)
    header = pack('IHHHH', 0x10000, n, power*16, power.bit_length()-1, n*16-power*16)
    records, payload = b'', b''
    offsets = {}
    for tag, data in sorted(tables.items()):
        offset = 12+16*n+len(payload)
        offsets[tag] = offset
        records += tag.encode('ascii') + pack('III', checksum(data), offset, len(data))
        payload += data + b'\0'*(-len(data)%4)
    result = bytearray(header+records+payload)
    struct.pack_into('>I', result, offsets['head']+8, (0xb1b0afba-checksum(bytes(result)))&0xffffffff)
    return bytes(result)


def collection(fonts):
    result = bytearray(pack('III', 0x74746366, 0x10000, len(fonts)) + b'\0'*(4*len(fonts)))
    for i, data in enumerate(fonts):
        start = len(result)
        struct.pack_into('>I', result, 12+i*4, start)
        face = bytearray(data)
        n, = struct.unpack_from('>H', face, 4)
        for j in range(n):
            pos = 12+j*16+8
            offset, = struct.unpack_from('>I', face, pos)
            struct.pack_into('>I', face, pos, offset+start)
        result.extend(face)
        result.extend(b'\0'*(-len(result)%4))
    return bytes(result)


def generate(folder):
    folder.mkdir(parents=True, exist_ok=True)
    fixtures = {
        'collision-a.ttf': font('HikariProbeCollision', 'HikariProbeCollision-Regular', 300),
        'collision-b.ttf': font('HikariProbeCollision', 'HikariProbeCollision-Regular', 680),
        'legacy.ttf': font('HikariProbeLegacy', 'HikariProbeLegacyPS', modern='HikariProbeModern'),
        'fallback.ttf': font('HikariProbeFallback', 'HikariProbeFallback-Regular', 500,
                             [32, 0x301, 0x627, 0x639, 0x4e2d]),
        'collection.ttc': collection([font('HikariProbeCollectionA', 'HikariProbeCollectionA-Regular', 350),
                                       font('HikariProbeCollectionB', 'HikariProbeCollectionB-Regular', 650)]),
        # Original outline deliberately takes a system-family name for a collision probe.
        'arial-conflict.ttf': font('Arial', 'ArialMT', 260),
    }
    for name, data in fixtures.items():
        (folder/name).write_bytes(data)
    return fixtures


if __name__ == '__main__':
    generate(Path(__file__).parent/'_run'/'fixtures')
