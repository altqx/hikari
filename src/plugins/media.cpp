#include "media.h"

#include "hikari/api/json.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>

namespace hikari::plugins {

namespace {

constexpr std::size_t kKiB = 1024;
constexpr std::size_t kMiB = 1024 * kKiB;

struct Pixels {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

using Bytes = std::vector<std::byte>;
using Problem = std::optional<std::string>;

std::string_view textOf(const Bytes &data)
{
    return {reinterpret_cast<const char *>(data.data()), data.size()};
}

unsigned byteAt(const Bytes &data, std::size_t at)
{
    return std::to_integer<unsigned>(data[at]);
}

std::uint32_t bigEndian(const Bytes &data, std::size_t at, int count)
{
    std::uint32_t out = 0;
    for (int i = 0; i < count; ++i)
        out = (out << 8) | byteAt(data, at + std::size_t(i));
    return out;
}

std::string pixels(Pixels p)
{
    return std::to_string(p.width) + "x" + std::to_string(p.height) + " px";
}

std::optional<Pixels> pngPixels(const Bytes &data)
{
    static constexpr std::string_view kSignature("\x89PNG\r\n\x1A\n", 8);
    const std::string_view text = textOf(data);
    if (!text.starts_with(kSignature) || text.size() < 24 || bigEndian(data, 8, 4) != 13 || text.substr(12, 4) != "IHDR")
        return std::nullopt;
    return Pixels{bigEndian(data, 16, 4), bigEndian(data, 20, 4)};
}

bool jpegSignature(const Bytes &data)
{
    return data.size() >= 3 && byteAt(data, 0) == 0xFF && byteAt(data, 1) == 0xD8 && byteAt(data, 2) == 0xFF;
}

bool startOfFrame(unsigned marker)
{
    return marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
}

std::optional<Pixels> jpegPixels(const Bytes &data)
{
    std::size_t at = 2;
    while (at + 2 <= data.size()) {
        if (byteAt(data, at) != 0xFF)
            return std::nullopt;
        const unsigned marker = byteAt(data, at + 1);
        if (marker == 0xFF) {
            ++at;
            continue;
        }
        at += 2;
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD8))
            continue;
        if (marker == 0xD9 || marker == 0xDA || at + 2 > data.size())
            return std::nullopt;
        const std::uint32_t length = bigEndian(data, at, 2);
        if (length < 2)
            return std::nullopt;
        if (startOfFrame(marker))
            return length < 7 || at + 7 > data.size() ? std::nullopt
                                                      : std::optional(Pixels{bigEndian(data, at + 5, 2), bigEndian(data, at + 3, 2)});
        at += length;
    }
    return std::nullopt;
}

char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c;
}

std::string lowered(std::string_view s)
{
    std::string out(s);
    std::ranges::transform(out, out.begin(), lower);
    return out;
}

std::string_view localName(std::string_view name)
{
    const std::size_t colon = name.rfind(':');
    return colon == std::string_view::npos ? name : name.substr(colon + 1);
}

bool space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

void appendUtf8(std::string &out, std::uint32_t code)
{
    if (code < 0x80) {
        out += char(code);
    } else if (code < 0x800) {
        out += char(0xC0 | (code >> 6));
        out += char(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        out += char(0xE0 | (code >> 12));
        out += char(0x80 | ((code >> 6) & 0x3F));
        out += char(0x80 | (code & 0x3F));
    } else {
        out += char(0xF0 | (code >> 18));
        out += char(0x80 | ((code >> 12) & 0x3F));
        out += char(0x80 | ((code >> 6) & 0x3F));
        out += char(0x80 | (code & 0x3F));
    }
}

std::string withCharacterReferences(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const std::size_t end = text.substr(i, 2) == "&#" ? text.find(';', i) : std::string_view::npos;
        if (end != std::string_view::npos) {
            const bool hex = i + 2 < end && lower(text[i + 2]) == 'x';
            const std::string_view digits = text.substr(i + (hex ? 3 : 2), end - i - (hex ? 3 : 2));
            std::uint32_t code = 0;
            const auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), code, hex ? 16 : 10);
            if (ec == std::errc() && ptr == digits.data() + digits.size() && !digits.empty() && code <= 0x10FFFF) {
                appendUtf8(out, code);
                i = end + 1;
                continue;
            }
        }
        out += text[i++];
    }
    return out;
}

bool squareViewBox(std::string_view box)
{
    std::array<double, 4> values{};
    std::size_t count = 0;
    std::size_t i = 0;
    while (i < box.size()) {
        if (space(box[i]) || box[i] == ',') {
            ++i;
            continue;
        }
        if (count == values.size())
            return false;
        const auto [ptr, ec] = std::from_chars(box.data() + i, box.data() + box.size(), values[count]);
        if (ec != std::errc())
            return false;
        i = std::size_t(ptr - box.data());
        ++count;
    }
    return count == 4 && values[2] > 0 && values[2] == values[3];
}

class SvgReader {
public:
    explicit SvgReader(std::string_view text) : m_text(text) {}

    Problem problem()
    {
        while (true) {
            const std::size_t open = m_text.find('<', m_at);
            if (open == std::string_view::npos)
                break;
            m_at = open;
            const std::string_view rest = m_text.substr(open);
            Problem found;
            if (rest.starts_with("<!--"))
                found = skipPast("-->");
            else if (rest.starts_with("<![CDATA["))
                found = skipPast("]]>");
            else if (rest.starts_with("<?"))
                found = skipPast("?>");
            else if (rest.starts_with("<!"))
                found = doctype();
            else if (rest.starts_with("</"))
                found = skipPast(">");
            else
                found = element();
            if (found)
                return found;
        }
        if (!m_rootSeen)
            return "has no <svg> root element";
        return outsideUrl(withCharacterReferences(m_text));
    }

private:
    static constexpr std::string_view kMalformed = "isn't well-formed XML";

    Problem skipPast(std::string_view close)
    {
        const std::size_t end = m_text.find(close, m_at + 1);
        if (end == std::string_view::npos)
            return std::string(kMalformed);
        m_at = end + close.size();
        return std::nullopt;
    }

    Problem doctype()
    {
        const std::size_t end = m_text.find('>', m_at);
        if (end == std::string_view::npos)
            return std::string(kMalformed);
        if (m_text.substr(m_at, end - m_at).find('[') != std::string_view::npos)
            return "declares entities or attributes in its DOCTYPE";
        m_at = end + 1;
        return std::nullopt;
    }

    std::string_view name()
    {
        const std::size_t start = m_at;
        while (m_at < m_text.size() && !space(m_text[m_at]) && m_text[m_at] != '=' && m_text[m_at] != '>' &&
               m_text[m_at] != '/' && m_text[m_at] != '<' && m_text[m_at] != '"' && m_text[m_at] != '\'')
            ++m_at;
        return m_text.substr(start, m_at - start);
    }

    void skipSpace()
    {
        while (m_at < m_text.size() && space(m_text[m_at]))
            ++m_at;
    }

    Problem element()
    {
        ++m_at;
        const std::string_view tag = name();
        if (tag.empty())
            return std::string(kMalformed);
        const std::string local = lowered(localName(tag));
        const bool root = !m_rootSeen;
        if (root && local != "svg")
            return "has a root element other than <svg>";
        m_rootSeen = true;
        if (local == "script")
            return "holds a <script> element";
        if (local == "foreignobject")
            return "holds a <foreignObject> element";
        std::optional<std::string> viewBox;
        while (true) {
            skipSpace();
            if (m_at >= m_text.size())
                return std::string(kMalformed);
            if (m_text[m_at] == '>' || m_text.substr(m_at, 2) == "/>") {
                m_at += m_text[m_at] == '>' ? 1 : 2;
                break;
            }
            const std::string_view attribute = name();
            skipSpace();
            if (attribute.empty() || m_at >= m_text.size() || m_text[m_at] != '=')
                return std::string(kMalformed);
            ++m_at;
            skipSpace();
            const char quote = m_at < m_text.size() ? m_text[m_at] : '\0';
            const std::size_t end = quote == '"' || quote == '\'' ? m_text.find(quote, m_at + 1) : std::string_view::npos;
            if (end == std::string_view::npos)
                return std::string(kMalformed);
            const std::string value = withCharacterReferences(m_text.substr(m_at + 1, end - m_at - 1));
            m_at = end + 1;
            const std::string attributeLocal = lowered(localName(attribute));
            if (attributeLocal.starts_with("on"))
                return "has an " + std::string(attribute) + " attribute";
            if (attributeLocal == "href" && !value.starts_with('#'))
                return "has an " + std::string(attribute) + " that points outside the file";
            if (root && attribute == "viewBox")
                viewBox = value;
        }
        if (root && !(viewBox && squareViewBox(*viewBox)))
            return "has no square viewBox on its <svg>";
        return std::nullopt;
    }

    static Problem outsideUrl(const std::string &text)
    {
        const std::string folded = lowered(text);
        for (std::size_t at = folded.find("url("); at != std::string::npos; at = folded.find("url(", at + 4)) {
            std::size_t i = at + 4;
            while (i < folded.size() && (space(folded[i]) || folded[i] == '"' || folded[i] == '\''))
                ++i;
            if (i >= folded.size() || folded[i] != '#')
                return "has a url( that points outside the file";
        }
        return std::nullopt;
    }

    std::string_view m_text;
    std::size_t m_at = 0;
    bool m_rootSeen = false;
};

Problem iconProblem(const PackageFile &icon)
{
    if (icon.path.ends_with(".svg")) {
        if (icon.data.size() > 64 * kKiB)
            return "is larger than 64 KiB";
        if (!api::isUtf8(textOf(icon.data)))
            return "isn't UTF-8";
        return SvgReader(textOf(icon.data)).problem();
    }
    if (icon.data.size() > 256 * kKiB)
        return "is larger than 256 KiB";
    const auto size = pngPixels(icon.data);
    if (!size)
        return "isn't a PNG";
    if (size->width != 256 || size->height != 256)
        return "is " + pixels(*size) + ", not 256x256 px";
    return std::nullopt;
}

bool screenshotPixels(Pixels p)
{
    const auto drift = std::int64_t(p.width) * 9 - std::int64_t(p.height) * 16;
    return p.width >= 1280 && p.width <= 1920 && p.height >= 720 && p.height <= 1080 && drift >= -16 && drift <= 16;
}

Problem screenshotProblem(const PackageFile &shot)
{
    if (shot.data.size() > 2 * kMiB)
        return "is larger than 2 MiB";
    std::optional<Pixels> size;
    if (shot.path.ends_with(".png")) {
        size = pngPixels(shot.data);
        if (!size)
            return "isn't a PNG";
    } else {
        if (!jpegSignature(shot.data))
            return "isn't a JPEG";
        size = jpegPixels(shot.data);
        if (!size)
            return "has no JPEG frame header";
    }
    if (!screenshotPixels(*size))
        return "is " + pixels(*size) + "; a screenshot is 16:9, from 1280x720 to 1920x1080 px";
    return std::nullopt;
}

}

std::optional<PackageProblem> mediaProblem(const Manifest &manifest, const std::vector<PackageFile> &files)
{
    const auto file = [&](std::string_view path) -> const PackageFile * {
        const auto it = std::ranges::find(files, path, &PackageFile::path);
        return it == files.end() ? nullptr : &*it;
    };
    const auto refuse = [](std::string path, std::string detail) {
        return PackageProblem{PackageProblem::Kind::Media, std::move(path), {}, std::move(detail)};
    };
    constexpr std::string_view kMissing = "names no file in the package (the case must match)";

    std::size_t media = 0;
    if (!manifest.icon.empty()) {
        const PackageFile *icon = file(manifest.icon);
        if (!icon)
            return refuse(manifest.icon, std::string(kMissing));
        if (auto problem = iconProblem(*icon))
            return refuse(manifest.icon, std::move(*problem));
        media += icon->data.size();
    }
    std::set<std::string_view> shown;
    for (const Screenshot &shot : manifest.screenshots) {
        if (!shown.insert(shot.path).second)
            return refuse(shot.path, "repeats another screenshot");
        const PackageFile *image = file(shot.path);
        if (!image)
            return refuse(shot.path, std::string(kMissing));
        if (auto problem = screenshotProblem(*image))
            return refuse(shot.path, std::move(*problem));
        media += image->data.size();
        if (media > 8 * kMiB)
            return refuse(shot.path, "takes the icon and screenshots past 8 MiB together");
    }
    if (const PackageFile *readme = file("README.md")) {
        if (readme->data.size() > 128 * kKiB)
            return refuse(readme->path, "is larger than 128 KiB");
        if (!api::isUtf8(textOf(readme->data)))
            return refuse(readme->path, "isn't UTF-8");
    }
    return std::nullopt;
}

}
