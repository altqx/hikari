#include "hikari/application/file_diff.h"

#include "hikari/json/json.h"

#include <algorithm>

namespace hikari::application {

namespace {

std::vector<std::string_view> linesOf(std::string_view text)
{
    std::vector<std::string_view> out;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        if (line.ends_with('\r'))
            line.remove_suffix(1);
        out.push_back(line);
        if (end == std::string_view::npos)
            break;
        text.remove_prefix(end + 1);
    }
    return out;
}

using Kind = FileDiffLine::Kind;

constexpr int kMaxEdits = 4000;

// Myers' O(ND) diff: the shortest edit script from a to b, as one Kind per line; nullopt past kMaxEdits.
std::optional<std::vector<Kind>> editScript(const std::vector<std::string_view> &a,
                                            const std::vector<std::string_view> &b)
{
    const int n = int(a.size()), m = int(b.size());
    const int max = std::min(n + m, kMaxEdits);
    const int offset = max + 1;
    std::vector<int> v(std::size_t(2 * max + 3), 0);
    std::vector<std::vector<int>> trace; // trace[d]: v for k in [-d, d] before step d
    const auto at = [&](const std::vector<int> &band, int d, int k) { return band[std::size_t(k + d)]; };
    for (int d = 0; d <= max; ++d) {
        trace.emplace_back(v.begin() + (offset - d), v.begin() + (offset + d + 1));
        for (int k = -d; k <= d; k += 2) {
            int x = (k == -d || (k != d && v[std::size_t(offset + k - 1)] < v[std::size_t(offset + k + 1)]))
                        ? v[std::size_t(offset + k + 1)]
                        : v[std::size_t(offset + k - 1)] + 1;
            int y = x - k;
            while (x < n && y < m && a[std::size_t(x)] == b[std::size_t(y)])
                ++x, ++y;
            v[std::size_t(offset + k)] = x;
            if (x < n || y < m)
                continue;
            std::vector<Kind> out;
            for (int back = d; back > 0; --back) {
                const std::vector<int> &prev = trace[std::size_t(back)];
                const int kk = x - y;
                const bool down =
                    kk == -back || (kk != back && at(prev, back, kk - 1) < at(prev, back, kk + 1));
                const int prevK = down ? kk + 1 : kk - 1;
                const int prevX = at(prev, back, prevK), prevY = prevX - prevK;
                while (x > prevX && y > prevY)
                    out.push_back(Kind::Same), --x, --y;
                out.push_back(down ? Kind::Added : Kind::Removed);
                x = prevX, y = prevY;
            }
            while (x > 0 && y > 0)
                out.push_back(Kind::Same), --x, --y;
            std::ranges::reverse(out);
            return out;
        }
    }
    return std::nullopt;
}

}

FileDiff fileDiff(const std::optional<std::string> &before, std::string_view after, std::size_t context)
{
    FileDiff out;
    out.beforeBytes = before ? before->size() : 0;
    out.afterBytes = after.size();
    const std::string_view old = before ? std::string_view(*before) : std::string_view();
    if (old.size() > kFileDiffMaxBytes || after.size() > kFileDiffMaxBytes || !json::isUtf8(old) ||
        !json::isUtf8(after))
        return out;
    const std::vector<std::string_view> a = linesOf(old), b = linesOf(after);
    const std::optional<std::vector<Kind>> script = editScript(a, b);
    if (!script)
        return out;
    out.text = true;
    out.lines = b.size();
    std::vector<FileDiffLine> all;
    std::size_t ai = 0, bi = 0;
    for (const Kind kind : *script) {
        if (kind == Kind::Removed) {
            all.push_back({kind, std::string(a[ai++])});
            ++out.removed;
        } else if (kind == Kind::Added) {
            all.push_back({kind, std::string(b[bi++])});
            ++out.added;
        } else {
            all.push_back({kind, std::string(b[bi++])});
            ++ai;
        }
    }
    std::vector<char> near(all.size(), 0);
    for (std::size_t i = 0; i < all.size(); ++i)
        if (all[i].kind != Kind::Same)
            for (std::size_t j = i > context ? i - context : 0; j <= std::min(all.size() - 1, i + context); ++j)
                near[j] = 1;
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (near[i])
            out.shown.push_back(std::move(all[i]));
        else if (out.shown.empty() || out.shown.back().kind != Kind::Gap)
            out.shown.push_back({Kind::Gap, {}});
    }
    return out;
}

}
