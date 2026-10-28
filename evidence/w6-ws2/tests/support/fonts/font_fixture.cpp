// Writes original geometric font fixtures for the font identity tests (N4),
// a C++ port of the accepted Windows experiment's fixtures.py (0f776e44).
// Every glyph is one rectangle; no outlines or tables come from another font.
// The generated fonts are dedicated to the public domain (CC0-1.0). They test
// identity and coverage decisions, not credible shaping.
//
//   hikari_font_fixture <directory> [<refresh directory>]
//
// The optional second directory receives refresh.ttf (family
// HikariProbeRefresh), kept out of the fontconfig-visible fixture directory
// so a test can make it appear later.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

struct Writer {
    Bytes out;
    Writer &u8(std::uint8_t v) { out.push_back(v); return *this; }
    Writer &u16(std::uint16_t v) { return u8(std::uint8_t(v >> 8)).u8(std::uint8_t(v)); }
    Writer &i16(std::int16_t v) { return u16(std::uint16_t(v)); }
    Writer &u32(std::uint32_t v) { return u16(std::uint16_t(v >> 16)).u16(std::uint16_t(v)); }
    Writer &i64(std::int64_t v) { return u32(std::uint32_t(std::uint64_t(v) >> 32)).u32(std::uint32_t(v)); }
    Writer &bytes(const Bytes &b) { out.insert(out.end(), b.begin(), b.end()); return *this; }
    Writer &pad4() { while (out.size() % 4) out.push_back(0); return *this; }
};

std::uint32_t checksum(Bytes data)
{
    while (data.size() % 4)
        data.push_back(0);
    std::uint32_t sum = 0;
    for (std::size_t i = 0; i < data.size(); i += 4)
        sum += std::uint32_t(data[i]) << 24 | std::uint32_t(data[i + 1]) << 16 | std::uint32_t(data[i + 2]) << 8 |
               data[i + 3];
    return sum;
}

void put32(Bytes &b, std::size_t at, std::uint32_t v)
{
    b[at] = std::uint8_t(v >> 24);
    b[at + 1] = std::uint8_t(v >> 16);
    b[at + 2] = std::uint8_t(v >> 8);
    b[at + 3] = std::uint8_t(v);
}

std::uint32_t get32(const Bytes &b, std::size_t at)
{
    return std::uint32_t(b[at]) << 24 | std::uint32_t(b[at + 1]) << 16 | std::uint32_t(b[at + 2]) << 8 | b[at + 3];
}

Bytes utf16be(const std::string &ascii)
{
    Bytes b;
    for (char c : ascii) {
        b.push_back(0);
        b.push_back(std::uint8_t(c));
    }
    return b;
}

int floorLog2(int n)
{
    int p = 0;
    while ((2 << p) <= n)
        ++p;
    return p;
}

struct FontSpec {
    std::string family, postscript, typographic;
    int width = 450;
    std::vector<int> chars; // empty: printable ASCII
    std::uint16_t weight = 400;
    std::string subfamily = "Regular";
};

Bytes font(const FontSpec &spec)
{
    std::vector<int> chars = spec.chars;
    if (chars.empty())
        for (int c = 32; c < 127; ++c)
            chars.push_back(c);
    std::sort(chars.begin(), chars.end());
    chars.erase(std::unique(chars.begin(), chars.end()), chars.end());
    const int width = spec.width;

    // Glyph 0 is .notdef and glyph 2 the space, both empty; glyph 1 is one
    // original four-point rectangle. (Mapping space to .notdef would make a
    // renderer look for a fallback space.)
    Writer glyph;
    glyph.i16(1).i16(0).i16(0).i16(std::int16_t(width)).i16(700);
    glyph.u16(3).u16(0).u8(1).u8(1).u8(1).u8(1);
    glyph.i16(0).i16(std::int16_t(width)).i16(0).i16(std::int16_t(-width));
    glyph.i16(0).i16(0).i16(700).i16(0);
    glyph.pad4();

    Writer head;
    head.u32(0x10000).u32(0x10000).u32(0).u32(0x5f0f3cf5).u16(0).u16(1000).i64(0).i64(0);
    head.i16(0).i16(0).i16(std::int16_t(width)).i16(700).u16(spec.weight >= 700 ? 1 : 0).u16(8).i16(2).i16(1).i16(0);
    Writer hhea;
    hhea.u32(0x10000).i16(800).i16(-200).i16(0).u16(std::uint16_t(width + 80)).i16(0).i16(0).i16(std::int16_t(width));
    hhea.i16(1).i16(0).i16(0).i16(0).i16(0).i16(0).i16(0).i16(0).u16(3);
    Writer maxp;
    maxp.u32(0x10000).u16(3).u16(4).u16(1).u16(0).u16(0).u16(2); // version 1.0: 13 fields after numGlyphs
    for (int i = 0; i < 8; ++i)
        maxp.u16(0);

    std::map<int, std::string> names = {
        {1, spec.family}, {2, spec.subfamily}, {3, spec.postscript + "-fixture-v1"},
        {4, spec.family + " " + spec.subfamily}, {5, "Version 1.000"}, {6, spec.postscript},
        {13, "Original geometric fixture, CC0-1.0"}, {14, "https://creativecommons.org/publicdomain/zero/1.0/"}};
    if (!spec.typographic.empty()) {
        names[16] = spec.typographic;
        names[17] = spec.subfamily;
    }
    Writer records;
    Bytes strings;
    for (const auto &[id, text] : names) {
        const Bytes encoded = utf16be(text);
        records.u16(3).u16(1).u16(0x409).u16(std::uint16_t(id)).u16(std::uint16_t(encoded.size()))
            .u16(std::uint16_t(strings.size()));
        strings.insert(strings.end(), encoded.begin(), encoded.end());
    }
    Writer name;
    name.u16(0).u16(std::uint16_t(names.size())).u16(std::uint16_t(6 + 12 * names.size())).bytes(records.out)
        .bytes(strings);

    // cmap format 4: one segment per code point, idDelta only.
    std::vector<int> codes = chars;
    codes.push_back(0xffff);
    const int n = int(codes.size());
    const int power = 1 << floorLog2(n);
    Writer cmap4;
    cmap4.u16(4).u16(std::uint16_t(16 + 8 * n)).u16(0).u16(std::uint16_t(2 * n)).u16(std::uint16_t(2 * power))
        .u16(std::uint16_t(floorLog2(power))).u16(std::uint16_t(2 * n - 2 * power));
    for (int c : codes)
        cmap4.u16(std::uint16_t(c));
    cmap4.u16(0);
    for (int c : codes)
        cmap4.u16(std::uint16_t(c));
    for (int c : codes)
        cmap4.u16(c == 0xffff ? 1 : std::uint16_t(((c == 32 ? 2 : 1) - c) & 0xffff));
    for (int i = 0; i < n; ++i)
        cmap4.u16(0);
    Writer cmap;
    cmap.u16(0).u16(1).u16(3).u16(1).u32(12).bytes(cmap4.out);

    Writer os2;
    os2.u16(0).i16(std::int16_t(width)).u16(spec.weight).u16(5).u16(0);
    os2.i16(650).i16(600).i16(0).i16(75).i16(650).i16(600).i16(0).i16(350).i16(50).i16(250);
    os2.i16(0);                                  // sFamilyClass
    for (int i = 0; i < 10; ++i)                 // panose
        os2.u8(0);
    os2.u32(0).u32(0).u32(0).u32(0);             // unicode ranges
    os2.u8('H').u8('K').u8('P').u8('R');
    os2.u16(spec.weight >= 700 ? 0x20 : 0x40);   // fsSelection: BOLD or REGULAR
    os2.u16(std::uint16_t(std::min(chars.front(), 0xffff))).u16(std::uint16_t(std::min(chars.back(), 0xffff)));
    os2.i16(800).i16(-200).i16(0).u16(800).u16(200);

    Writer post;
    post.u32(0x30000).u32(0).i16(-75).i16(50).u32(0).u32(0).u32(0).u32(0).u32(0);
    Writer hmtx;
    hmtx.u16(std::uint16_t(width + 80)).i16(0).u16(std::uint16_t(width + 80)).i16(0).u16(std::uint16_t(width / 2)).i16(0);
    Writer loca;
    loca.u32(0).u32(0).u32(std::uint32_t(glyph.out.size())).u32(std::uint32_t(glyph.out.size()));

    std::map<std::string, Bytes> tables = {{"head", head.out}, {"hhea", hhea.out}, {"maxp", maxp.out},
                                           {"hmtx", hmtx.out}, {"loca", loca.out}, {"glyf", glyph.out},
                                           {"cmap", cmap.out}, {"name", name.out}, {"OS/2", os2.out},
                                           {"post", post.out}};
    const int count = int(tables.size());
    const int p = 1 << floorLog2(count);
    Writer file;
    file.u32(0x10000).u16(std::uint16_t(count)).u16(std::uint16_t(p * 16)).u16(std::uint16_t(floorLog2(p)))
        .u16(std::uint16_t(count * 16 - p * 16));
    Bytes payload;
    std::size_t headOffset = 0;
    Writer directory;
    for (const auto &[tag, data] : tables) {
        const std::size_t offset = 12 + 16 * count + payload.size();
        if (tag == "head")
            headOffset = offset;
        for (char c : tag)
            directory.u8(std::uint8_t(c));
        directory.u32(checksum(data)).u32(std::uint32_t(offset)).u32(std::uint32_t(data.size()));
        payload.insert(payload.end(), data.begin(), data.end());
        while (payload.size() % 4)
            payload.push_back(0);
    }
    Bytes result = file.out;
    result.insert(result.end(), directory.out.begin(), directory.out.end());
    result.insert(result.end(), payload.begin(), payload.end());
    put32(result, headOffset + 8, 0xb1b0afba - checksum(result));
    return result;
}

Bytes collection(const std::vector<Bytes> &fonts)
{
    Writer header;
    header.u32(0x74746366).u32(0x10000).u32(std::uint32_t(fonts.size()));
    Bytes result = header.out;
    result.insert(result.end(), 4 * fonts.size(), std::uint8_t(0)); // the offset table
    for (std::size_t i = 0; i < fonts.size(); ++i) {
        const std::size_t start = result.size();
        put32(result, 12 + i * 4, std::uint32_t(start));
        Bytes face = fonts[i];
        const int n = face[4] << 8 | face[5];
        for (int j = 0; j < n; ++j) {
            const std::size_t at = 12 + std::size_t(j) * 16 + 8;
            put32(face, at, get32(face, at) + std::uint32_t(start));
        }
        result.insert(result.end(), face.begin(), face.end());
        while (result.size() % 4)
            result.push_back(0);
    }
    return result;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 2 && argc != 3) {
        std::fprintf(stderr, "usage: hikari_font_fixture <directory> [<refresh directory>]\n");
        return 1;
    }
    if (argc == 3) {
        const Bytes refresh = font({"HikariProbeRefresh", "HikariProbeRefresh-Regular", "", 460});
        std::ofstream out(std::string(argv[2]) + "/refresh.ttf", std::ios::binary);
        out.write(reinterpret_cast<const char *>(refresh.data()), std::streamsize(refresh.size()));
        if (!out)
            return 1;
    }
    const std::string dir = argv[1];
    const std::map<std::string, Bytes> fixtures = {
        {"collision-a.ttf", font({"HikariProbeCollision", "HikariProbeCollision-Regular", "", 300})},
        {"collision-b.ttf", font({"HikariProbeCollision", "HikariProbeCollision-Regular", "", 680})},
        {"legacy.ttf", font({"HikariProbeLegacy", "HikariProbeLegacyPS", "HikariProbeModern"})},
        {"fallback.ttf", font({"HikariProbeFallback", "HikariProbeFallback-Regular", "", 500,
                               {32, 0x301, 0x627, 0x639, 0x4e2d}})},
        {"collection.ttc", collection({font({"HikariProbeCollectionA", "HikariProbeCollectionA-Regular", "", 350}),
                                       font({"HikariProbeCollectionB", "HikariProbeCollectionB-Regular", "", 650})})},
        {"base.ttf", font({"HikariProbeBase", "HikariProbeBase-Regular", "", 420})},
        {"base-bold.ttf", font({"HikariProbeWeighted", "HikariProbeWeighted-Bold", "", 520, {}, 700, "Bold"})},
        {"weighted.ttf", font({"HikariProbeWeighted", "HikariProbeWeighted-Regular", "", 400})},
    };
    for (const auto &[name, data] : fixtures) {
        std::ofstream out(dir + "/" + name, std::ios::binary);
        out.write(reinterpret_cast<const char *>(data.data()), std::streamsize(data.size()));
        if (!out) {
            std::fprintf(stderr, "font_fixture: cannot write %s\n", name.c_str());
            return 1;
        }
    }
    return 0;
}
