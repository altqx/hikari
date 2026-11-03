#include "hikari/backends/plugin_protocol.h"

namespace hikari::backends::plugin {

using helper::Reader;
using helper::Writer;

namespace {

constexpr std::size_t kMaxCommands = 4096;

void writeStrings(Writer &w, const std::vector<std::string> &items)
{
    w.i32(static_cast<std::int32_t>(items.size()));
    for (const auto &item : items)
        w.str(item);
}

std::optional<std::vector<std::string>> readStrings(Reader &in)
{
    const std::int32_t count = in.i32();
    if (!in.ok() || count < 0 || static_cast<std::size_t>(count) > kMaxCommands)
        return std::nullopt;
    std::vector<std::string> items;
    items.reserve(static_cast<std::size_t>(count));
    for (std::int32_t i = 0; i < count; ++i)
        items.push_back(in.str());
    if (!in.ok())
        return std::nullopt;
    return items;
}

}

std::vector<std::byte> encodeLoad(const LoadRequest &load)
{
    Writer w;
    w.i32(static_cast<std::int32_t>(Command::Load))
        .str(load.root)
        .str(load.main)
        .str(load.id)
        .str(load.version)
        .i32(static_cast<std::int32_t>(load.tier))
        .i64(load.memoryLimit)
        .str(load.locale)
        .str(load.settings);
    writeStrings(w, load.commands);
    return w.take();
}

std::optional<LoadRequest> decodeLoad(Reader &in)
{
    LoadRequest load;
    load.root = in.str();
    load.main = in.str();
    load.id = in.str();
    load.version = in.str();
    const std::int32_t tier = in.i32();
    load.memoryLimit = in.i64();
    load.locale = in.str();
    load.settings = in.str();
    auto commands = readStrings(in);
    if (!in.ok() || !commands || !in.atEnd() || (tier != 0 && tier != 1) || load.memoryLimit <= 0)
        return std::nullopt;
    load.tier = static_cast<Tier>(tier);
    load.commands = std::move(*commands);
    return load;
}

std::vector<std::byte> encodeStrings(const std::vector<std::string> &items)
{
    Writer w;
    writeStrings(w, items);
    return w.take();
}

std::optional<std::vector<std::string>> decodeStrings(const std::vector<std::byte> &payload)
{
    Reader in(payload);
    auto items = readStrings(in);
    if (!items || !in.atEnd())
        return std::nullopt;
    return items;
}

}
