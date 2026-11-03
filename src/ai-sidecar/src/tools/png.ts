import { deflateSync } from "node:zlib";

const kSignature = Uint8Array.of(0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a);

const kCrcTable = Array.from({ length: 256 }, (_, n) => {
    let c = n;
    for (let k = 0; k < 8; ++k)
        c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    return c >>> 0;
});

function crc32(bytes: Uint8Array): number {
    let c = 0xffffffff;
    for (const byte of bytes)
        c = kCrcTable[(c ^ byte) & 0xff]! ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
}

function chunk(type: string, data: Uint8Array): Uint8Array {
    const out = new Uint8Array(12 + data.length);
    const view = new DataView(out.buffer);
    view.setUint32(0, data.length);
    out.set(new TextEncoder().encode(type), 4);
    out.set(data, 8);
    view.setUint32(8 + data.length, crc32(out.subarray(4, 8 + data.length)));
    return out;
}

/** An RGB PNG of opaque BGRA pixels, top row first, `width * 4` bytes per row; alpha is dropped. */
export function pngFromBgra(width: number, height: number, bgra: Uint8Array): Uint8Array {
    if (bgra.length !== width * height * 4)
        throw new Error(`a ${width}x${height} frame needs ${width * height * 4} bytes, not ${bgra.length}`);
    const header = new Uint8Array(13);
    const view = new DataView(header.buffer);
    view.setUint32(0, width);
    view.setUint32(4, height);
    header.set([8, 2, 0, 0, 0], 8);
    const stride = width * 3 + 1;
    const raw = new Uint8Array(stride * height);
    for (let y = 0; y < height; ++y)
        for (let x = 0; x < width; ++x) {
            const from = (y * width + x) * 4;
            const to = y * stride + 1 + x * 3;
            raw[to] = bgra[from + 2]!;
            raw[to + 1] = bgra[from + 1]!;
            raw[to + 2] = bgra[from]!;
        }
    const parts = [kSignature, chunk("IHDR", header), chunk("IDAT", deflateSync(raw)), chunk("IEND", new Uint8Array())];
    const png = new Uint8Array(parts.reduce((size, part) => size + part.length, 0));
    let at = 0;
    for (const part of parts) {
        png.set(part, at);
        at += part.length;
    }
    return png;
}
