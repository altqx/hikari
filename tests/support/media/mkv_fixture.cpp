// Writes Matroska files for MKV extraction (Y9) with a small EBML writer of
// its own, so the tracks, codec IDs, codec-private data, block order, block
// bytes and attachment names are exactly what the cases need (FFmpeg's muxer
// would rewrite several of them: it has no S_TEXT/SSA and checks names).
//
//   hikari_mkv_fixture <out> <kind>
//   kinds: subs    six subtitle tracks (ASS, SSA, SubRip, plain text, PGS,
//                  WebVTT) with the packets in mkvFixtureCases() order, and
//                  nine attachments (fonts under the four MIME types, a
//                  directory in the name, an empty name, "..", a Latin-1
//                  name, two of one name, a picture)
//          onesub  one SubRip track and one PGS track, no attachments
//          nosubs  a PGS and a WebVTT track only
//
// The content is described by mkv_fixture.h, which the tests include.
#include "mkv_fixture.h"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

using Bytes = std::string;

void putId(Bytes &out, std::uint32_t id)
{
    if (id > 0xFFFFFF)
        out.push_back(char(id >> 24));
    if (id > 0xFFFF)
        out.push_back(char(id >> 16));
    if (id > 0xFF)
        out.push_back(char(id >> 8));
    out.push_back(char(id));
}

// Sizes as 8-byte vints (0x01 then 7 bytes).
void putSize(Bytes &out, std::uint64_t size)
{
    out.push_back(char(0x01));
    for (int i = 6; i >= 0; --i)
        out.push_back(char(size >> (8 * i)));
}

Bytes element(std::uint32_t id, const Bytes &payload)
{
    Bytes out;
    putId(out, id);
    putSize(out, payload.size());
    return out + payload;
}

Bytes uintElement(std::uint32_t id, std::uint64_t v)
{
    Bytes payload;
    for (int i = 7; i >= 0; --i)
        payload.push_back(char(v >> (8 * i)));
    return element(id, payload);
}

Bytes header()
{
    return element(0x1A45DFA3, uintElement(0x4286, 1) + uintElement(0x42F7, 1) + uintElement(0x42F2, 4) +
                                   uintElement(0x42F3, 8) + element(0x4282, "matroska") + uintElement(0x4287, 4) +
                                   uintElement(0x4285, 2));
}

Bytes trackEntry(int number, const mkvfixture::Track &t)
{
    Bytes payload = uintElement(0xD7, std::uint64_t(number)) + uintElement(0x73C5, std::uint64_t(1000 + number)) +
                    uintElement(0x83, 0x11) + uintElement(0x9C, 0) + element(0x86, t.codecId);
    if (!t.codecPrivate.empty())
        payload += element(0x63A2, t.codecPrivate);
    if (!t.language.empty())
        payload += element(0x22B59C, t.language);
    if (!t.name.empty())
        payload += element(0x536E, t.name);
    return element(0xAE, payload);
}

// One cluster per packet: its time is the packet's, so any time fits the
// block's 16-bit relative timestamp, and the file order is the list order.
Bytes cluster(int trackNumber, const mkvfixture::Packet &p)
{
    Bytes block;
    block.push_back(char(0x80 | trackNumber)); // track number as a 1-byte vint
    block.push_back(0);                        // relative timestamp 0
    block.push_back(0);
    block.push_back(0); // flags
    block += p.data;
    const Bytes group = element(0xA0, element(0xA1, block) + uintElement(0x9B, std::uint64_t(p.duration)));
    return element(0x1F43B675, uintElement(0xE7, std::uint64_t(p.start)) + group);
}

Bytes attachment(int uid, const mkvfixture::Attachment &a)
{
    return element(0x61A7, element(0x466E, a.filename) + element(0x4660, a.mimetype) + element(0x465C, a.data) +
                               uintElement(0x46AE, std::uint64_t(5000 + uid)));
}

int write(const std::string &out, const mkvfixture::File &file)
{
    Bytes info = uintElement(0x2AD7B1, 1000000) + element(0x4D80, "hikari_mkv_fixture") +
                 element(0x5741, "hikari_mkv_fixture");
    if (file.durationMs > 0) // Duration, a big-endian double in TimestampScale units
        info += uintElement(0x4489, std::bit_cast<std::uint64_t>(file.durationMs));
    Bytes segment = element(0x1549A966, info);
    Bytes tracks;
    for (std::size_t i = 0; i < file.tracks.size(); ++i)
        tracks += trackEntry(int(i + 1), file.tracks[i]);
    segment += element(0x1654AE6B, tracks);
    if (!file.attachments.empty()) {
        Bytes list;
        for (std::size_t i = 0; i < file.attachments.size(); ++i)
            list += attachment(int(i), file.attachments[i]);
        segment += element(0x1941A469, list);
    }
    for (const auto &[track, packet] : file.packets)
        segment += cluster(track + 1, packet);
    std::ofstream f(out, std::ios::binary);
    const Bytes bytes = header() + element(0x18538067, segment);
    f.write(bytes.data(), std::streamsize(bytes.size()));
    return f ? 0 : 1;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 3) {
        std::fprintf(stderr, "usage: hikari_mkv_fixture <out> <subs|onesub|nosubs>\n");
        return 1;
    }
    const std::string kind = argv[2];
    if (kind == "subs")
        return write(argv[1], mkvfixture::subs());
    if (kind == "onesub")
        return write(argv[1], mkvfixture::oneSub());
    if (kind == "nosubs")
        return write(argv[1], mkvfixture::noSubs());
    std::fprintf(stderr, "hikari_mkv_fixture: unknown kind %s\n", kind.c_str());
    return 1;
}
