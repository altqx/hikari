#include "hikari/backends/lua_protocol.h"

#include "hikari/backends/helper_protocol.h"

namespace hikari::backends::lua {

using application::DialogControl;
using application::DialogRequest;
using application::DialogResult;
using application::DialogValue;
using application::DialogValueType;
using application::ScriptInfo;
using helper::Reader;
using helper::Writer;

namespace {

// A count read from the wire, bounded by what the payload could hold.
std::optional<std::size_t> count(Reader &r, std::size_t payloadSize)
{
    const std::int32_t n = r.i32();
    if (!r.ok() || n < 0 || static_cast<std::size_t>(n) > payloadSize)
        return std::nullopt;
    return static_cast<std::size_t>(n);
}

} // namespace

std::vector<std::byte> encodeInfo(const ScriptInfo &info)
{
    Writer w;
    w.str(info.name).str(info.description).str(info.author).str(info.version);
    w.i32(static_cast<std::int32_t>(info.macros.size()));
    for (const auto &m : info.macros)
        w.str(m.name).str(m.description).u8(m.hasValidate).u8(m.hasIsActive);
    return w.take();
}

std::optional<ScriptInfo> decodeInfo(const std::vector<std::byte> &payload)
{
    Reader r(payload);
    ScriptInfo info;
    info.name = r.str();
    info.description = r.str();
    info.author = r.str();
    info.version = r.str();
    const auto n = count(r, payload.size());
    if (!n)
        return std::nullopt;
    for (std::size_t i = 0; i < *n; ++i) {
        application::ScriptMacro m;
        m.name = r.str();
        m.description = r.str();
        m.hasValidate = r.u8() != 0;
        m.hasIsActive = r.u8() != 0;
        info.macros.push_back(std::move(m));
    }
    if (!r.ok() || !r.atEnd())
        return std::nullopt;
    return info;
}

std::vector<std::byte> encodeDialogRequest(const DialogRequest &request)
{
    Writer w;
    w.i32(static_cast<std::int32_t>(request.controls.size()));
    for (const DialogControl &c : request.controls) {
        w.str(c.kind).str(c.name).str(c.hint).i32(c.x).i32(c.y).i32(c.width).i32(c.height);
        w.str(c.label).str(c.text).i32(c.intValue).i32(c.intMin).i32(c.intMax);
        w.f64(c.number).f64(c.numberMin).f64(c.numberMax).f64(c.step).u8(c.checked);
        w.i32(static_cast<std::int32_t>(c.items.size()));
        for (const auto &item : c.items)
            w.str(item);
    }
    w.i32(static_cast<std::int32_t>(request.buttons.size()));
    for (const auto &b : request.buttons)
        w.str(b);
    return w.take();
}

std::optional<DialogRequest> decodeDialogRequest(const std::vector<std::byte> &payload)
{
    Reader r(payload);
    DialogRequest request;
    const auto controls = count(r, payload.size());
    if (!controls)
        return std::nullopt;
    for (std::size_t i = 0; i < *controls && r.ok(); ++i) {
        DialogControl c;
        c.kind = r.str();
        c.name = r.str();
        c.hint = r.str();
        c.x = r.i32();
        c.y = r.i32();
        c.width = r.i32();
        c.height = r.i32();
        c.label = r.str();
        c.text = r.str();
        c.intValue = r.i32();
        c.intMin = r.i32();
        c.intMax = r.i32();
        c.number = r.f64();
        c.numberMin = r.f64();
        c.numberMax = r.f64();
        c.step = r.f64();
        c.checked = r.u8() != 0;
        const auto items = count(r, payload.size());
        if (!items)
            return std::nullopt;
        for (std::size_t k = 0; k < *items; ++k)
            c.items.push_back(r.str());
        request.controls.push_back(std::move(c));
    }
    const auto buttons = count(r, payload.size());
    if (!buttons)
        return std::nullopt;
    for (std::size_t i = 0; i < *buttons; ++i)
        request.buttons.push_back(r.str());
    if (!r.ok() || !r.atEnd())
        return std::nullopt;
    return request;
}

std::vector<std::byte> encodeDialogResult(const DialogResult &result)
{
    Writer w;
    w.i32(result.pressed).i32(static_cast<std::int32_t>(result.values.size()));
    for (const DialogValue &v : result.values) {
        w.u8(static_cast<std::uint8_t>(v.index()));
        if (const auto *s = std::get_if<std::string>(&v))
            w.str(*s);
        else if (const auto *i = std::get_if<int>(&v))
            w.i32(*i);
        else if (const auto *d = std::get_if<double>(&v))
            w.f64(*d);
        else if (const auto *b = std::get_if<bool>(&v))
            w.u8(*b);
    }
    return w.take();
}

std::optional<DialogResult> decodeDialogResult(const std::vector<std::byte> &payload, const DialogRequest &request)
{
    Reader r(payload);
    DialogResult result;
    result.pressed = r.i32();
    const auto n = count(r, payload.size());
    const int buttons = request.buttons.empty() ? 2 : static_cast<int>(request.buttons.size());
    if (!n || *n != request.controls.size() || result.pressed < -1 || result.pressed >= buttons)
        return std::nullopt;
    for (std::size_t i = 0; i < *n; ++i) {
        const auto tag = r.u8();
        DialogValue v;
        switch (tag) {
        case 0: break;
        case 1: v = r.str(); break;
        case 2: v = r.i32(); break;
        case 3: v = r.f64(); break;
        case 4: v = r.u8() != 0; break;
        default: return std::nullopt;
        }
        // The value must have the type its control returns (DialogValueType
        // follows the DialogValue alternatives' order).
        if (v.index() != static_cast<std::size_t>(application::dialogValueType(request.controls[i].kind)))
            return std::nullopt;
        result.values.push_back(std::move(v));
    }
    if (!r.ok() || !r.atEnd())
        return std::nullopt;
    return result;
}

} // namespace hikari::backends::lua
