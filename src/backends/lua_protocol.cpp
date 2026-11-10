#include "hikari/backends/lua_protocol.h"

#include "hikari/backends/helper_protocol.h"

namespace hikari::backends::lua {

using application::DialogControl;
using application::DialogRequest;
using application::DialogResult;
using application::DialogValue;
using application::DialogValueType;
using application::HostServiceReply;
using application::HostServiceRequest;
using application::ScriptInfo;
using helper::Reader;
using helper::Writer;

namespace {

std::optional<std::size_t> count(Reader &r, std::size_t payloadSize)
{
    const std::int32_t n = r.i32();
    if (!r.ok() || n < 0 || static_cast<std::size_t>(n) > payloadSize)
        return std::nullopt;
    return static_cast<std::size_t>(n);
}

}

std::vector<std::byte> encodeInfo(const ScriptInfo &info)
{
    Writer w;
    w.str(info.name).str(info.description).str(info.author).str(info.version);
    w.i32(static_cast<std::int32_t>(info.macros.size()));
    for (const auto &m : info.macros)
        w.str(m.name).str(m.description).u8(m.hasValidate).u8(m.hasIsActive);
    w.i32(static_cast<std::int32_t>(info.loadedFiles.size()));
    for (const auto &f : info.loadedFiles)
        w.str(f);
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
    const auto files = count(r, payload.size());
    if (!files)
        return std::nullopt;
    for (std::size_t i = 0; i < *files; ++i)
        info.loadedFiles.push_back(r.str());
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
    for (std::size_t i = 0; i < request.buttons.size(); ++i)
        w.str(request.buttons[i]).str(i < request.buttonIds.size() ? request.buttonIds[i] : std::string());
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
    for (std::size_t i = 0; i < *buttons; ++i) {
        request.buttons.push_back(r.str());
        request.buttonIds.push_back(r.str());
    }
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
        // MUST KILL: DialogValueType's order must match DialogValue's alternatives.
        if (v.index() != static_cast<std::size_t>(application::dialogValueType(request.controls[i].kind)))
            return std::nullopt;
        result.values.push_back(std::move(v));
    }
    if (!r.ok() || !r.atEnd())
        return std::nullopt;
    return result;
}

namespace {

void writeLists(Writer &w, const std::vector<application::MacroInfoLine> &info,
                const std::vector<application::MacroStyleLine> &styles,
                const std::vector<application::MacroDialogueLine> &dialogues)
{
    w.i32(static_cast<std::int32_t>(info.size()));
    for (const auto &l : info)
        w.str(l.key).str(l.value);
    w.i32(static_cast<std::int32_t>(styles.size()));
    for (const auto &l : styles) {
        w.i32(static_cast<std::int32_t>(l.fields.size()));
        for (const auto &f : l.fields)
            w.str(f);
    }
    w.i32(static_cast<std::int32_t>(dialogues.size()));
    for (const auto &d : dialogues) {
        w.i64(static_cast<std::int64_t>(d.id)).i64(static_cast<std::int64_t>(d.generatedFrom)).u8(d.comment).i32(d.layer).i64(d.startMs).i64(d.endMs).str(d.style)
            .str(d.actor).i32(d.marginL).i32(d.marginR).i32(d.marginV).str(d.effect).str(d.text).str(d.translation)
            .str(d.raw).i32(static_cast<std::int32_t>(d.extra.size()));
        for (const auto &[key, value] : d.extra)
            w.str(key).str(value);
    }
}

bool readLists(Reader &r, std::size_t size, std::vector<application::MacroInfoLine> &info,
               std::vector<application::MacroStyleLine> &styles, std::vector<application::MacroDialogueLine> &dialogues)
{
    const auto ni = count(r, size);
    if (!ni)
        return false;
    for (std::size_t i = 0; i < *ni && r.ok(); ++i) {
        application::MacroInfoLine l;
        l.key = r.str();
        l.value = r.str();
        info.push_back(std::move(l));
    }
    const auto ns = count(r, size);
    if (!ns)
        return false;
    for (std::size_t i = 0; i < *ns && r.ok(); ++i) {
        application::MacroStyleLine l;
        const auto nf = count(r, size);
        if (!nf)
            return false;
        for (std::size_t k = 0; k < *nf; ++k)
            l.fields.push_back(r.str());
        styles.push_back(std::move(l));
    }
    const auto nd = count(r, size);
    if (!nd)
        return false;
    for (std::size_t i = 0; i < *nd && r.ok(); ++i) {
        application::MacroDialogueLine d;
        d.id = static_cast<std::uint64_t>(r.i64());
        d.generatedFrom = static_cast<std::uint64_t>(r.i64());
        d.comment = r.u8() != 0;
        d.layer = r.i32();
        d.startMs = r.i64();
        d.endMs = r.i64();
        d.style = r.str();
        d.actor = r.str();
        d.marginL = r.i32();
        d.marginR = r.i32();
        d.marginV = r.i32();
        d.effect = r.str();
        d.text = r.str();
        d.translation = r.str();
        d.raw = r.str();
        const auto ne = count(r, size);
        if (!ne)
            return false;
        for (std::size_t k = 0; k < *ne && r.ok(); ++k) {
            std::string key = r.str();
            d.extra.emplace_back(std::move(key), r.str());
        }
        dialogues.push_back(std::move(d));
    }
    return r.ok();
}

}

std::vector<std::byte> encodeSnapshot(const application::MacroSnapshot &snapshot)
{
    Writer w;
    w.i64(static_cast<std::int64_t>(snapshot.revision));
    writeLists(w, snapshot.info, snapshot.styles, snapshot.dialogues);
    w.i32(static_cast<std::int32_t>(snapshot.selected.size()));
    for (int i : snapshot.selected)
        w.i32(i);
    w.i32(snapshot.active).u8(snapshot.canModify);
    return w.take();
}

std::optional<application::MacroSnapshot> decodeSnapshot(Reader &r, std::size_t size)
{
    application::MacroSnapshot s;
    s.revision = static_cast<std::uint64_t>(r.i64());
    if (!readLists(r, size, s.info, s.styles, s.dialogues))
        return std::nullopt;
    const auto n = count(r, size);
    if (!n)
        return std::nullopt;
    for (std::size_t i = 0; i < *n; ++i)
        s.selected.push_back(r.i32());
    s.active = r.i32();
    s.canModify = r.u8() != 0;
    if (!r.ok())
        return std::nullopt;
    return s;
}

std::vector<std::byte> encodeMacroResult(const application::MacroResult &result)
{
    Writer w;
    writeLists(w, result.info, result.styles, result.dialogues);
    w.u8(result.selected.has_value());
    if (result.selected) {
        w.i32(static_cast<std::int32_t>(result.selected->size()));
        for (int i : *result.selected)
            w.i32(i);
    }
    w.u8(result.active.has_value()).i32(result.active.value_or(0));
    w.u8(result.valid).str(result.validationError).str(result.report);
    return w.take();
}

std::optional<application::MacroResult> decodeMacroResult(const std::vector<std::byte> &payload)
{
    Reader r(payload);
    application::MacroResult result;
    if (!readLists(r, payload.size(), result.info, result.styles, result.dialogues))
        return std::nullopt;
    if (r.u8()) {
        const auto n = count(r, payload.size());
        if (!n)
            return std::nullopt;
        std::vector<int> selected;
        for (std::size_t i = 0; i < *n; ++i)
            selected.push_back(r.i32());
        result.selected = std::move(selected);
    }
    const bool hasActive = r.u8() != 0;
    const int active = r.i32();
    if (hasActive)
        result.active = active;
    result.valid = r.u8() != 0;
    result.validationError = r.str();
    result.report = r.str();
    if (!r.ok() || !r.atEnd())
        return std::nullopt;
    return result;
}

namespace {

bool plausible(std::int32_t count, const std::vector<std::byte> &payload)
{
    return count >= 0 && static_cast<std::size_t>(count) <= payload.size();
}

void writeIntegers(Writer &w, const std::vector<std::int64_t> &v)
{
    w.i32(static_cast<std::int32_t>(v.size()));
    for (const auto x : v)
        w.i64(x);
}

void writeStrings(Writer &w, const std::vector<std::string> &v)
{
    w.i32(static_cast<std::int32_t>(v.size()));
    for (const auto &x : v)
        w.str(x);
}

bool readIntegers(Reader &r, const std::vector<std::byte> &payload, std::vector<std::int64_t> &out)
{
    const auto n = r.i32();
    if (!r.ok() || !plausible(n, payload))
        return false;
    for (std::int32_t i = 0; i < n && r.ok(); ++i)
        out.push_back(r.i64());
    return r.ok();
}

bool readStrings(Reader &r, const std::vector<std::byte> &payload, std::vector<std::string> &out)
{
    const auto n = r.i32();
    if (!r.ok() || !plausible(n, payload))
        return false;
    for (std::int32_t i = 0; i < n && r.ok(); ++i)
        out.push_back(r.str());
    return r.ok();
}

}

std::vector<std::byte> encodeHostRequest(const HostServiceRequest &request)
{
    Writer w;
    w.i32(static_cast<std::int32_t>(request.service));
    writeIntegers(w, request.integers);
    writeStrings(w, request.strings);
    writeStrings(w, request.style);
    return w.take();
}

std::optional<HostServiceRequest> decodeHostRequest(const std::vector<std::byte> &payload)
{
    Reader r(payload);
    HostServiceRequest request;
    const auto service = r.i32();
    if (!r.ok() || service < 1 || service > application::kLastHostService)
        return std::nullopt;
    request.service = static_cast<application::HostService>(service);
    if (!readIntegers(r, payload, request.integers) || !readStrings(r, payload, request.strings) ||
        !readStrings(r, payload, request.style) || !r.atEnd())
        return std::nullopt;
    return request;
}

std::vector<std::byte> encodeHostReply(const HostServiceReply &reply)
{
    Writer w;
    w.i32(static_cast<std::int32_t>(reply.status));
    writeIntegers(w, reply.integers);
    w.i32(static_cast<std::int32_t>(reply.numbers.size()));
    for (const double x : reply.numbers)
        w.f64(x);
    writeStrings(w, reply.strings);
    w.bytes(reply.pixels);
    return w.take();
}

bool isLuaNodeType(application::PortType type)
{
    using enum application::PortType;
    return type == Number || type == Text || type == Colour || type == Alpha || type == Time || type == Point ||
           type == Tags || type == Bool;
}

namespace {

using application::NodeValue;
using application::PortSpec;
using application::PortType;

template <class T> T valueAs(const NodeValue &v)
{
    const T *x = std::get_if<T>(&v);
    return x ? *x : T{};
}

void writeValue(Writer &w, PortType type, const NodeValue &v)
{
    switch (type) {
    case PortType::Text:
        w.str(valueAs<std::string>(v));
        break;
    case PortType::Tags:
        w.str(valueAs<application::NodeTags>(v).text);
        break;
    case PortType::Bool:
        w.u8(valueAs<bool>(v) ? 1 : 0);
        break;
    case PortType::Colour: {
        const auto c = valueAs<application::NodeColour>(v);
        w.i32(c.r).i32(c.g).i32(c.b);
        break;
    }
    case PortType::Point: {
        const auto p = valueAs<application::NodePoint>(v);
        w.f64(p.x).f64(p.y);
        break;
    }
    default:
        w.f64(valueAs<double>(v));
    }
}

NodeValue readValue(Reader &r, PortType type)
{
    switch (type) {
    case PortType::Text:
        return r.str();
    case PortType::Tags:
        return application::NodeTags{r.str()};
    case PortType::Bool:
        return r.u8() != 0;
    case PortType::Colour: {
        application::NodeColour c;
        c.r = r.i32();
        c.g = r.i32();
        c.b = r.i32();
        return c;
    }
    case PortType::Point: {
        application::NodePoint p;
        p.x = r.f64();
        p.y = r.f64();
        return p;
    }
    default:
        return r.f64();
    }
}

void writePorts(Writer &w, const std::vector<PortSpec> &ports)
{
    w.i32(static_cast<std::int32_t>(ports.size()));
    for (const PortSpec &p : ports)
        w.str(p.name).i32(static_cast<std::int32_t>(p.type));
}

bool readPorts(Reader &r, std::size_t payloadSize, std::vector<PortSpec> &out)
{
    const auto n = count(r, payloadSize);
    if (!n)
        return false;
    for (std::size_t i = 0; i < *n && r.ok(); ++i) {
        PortSpec p;
        p.name = r.str();
        p.type = static_cast<PortType>(r.i32());
        if (!isLuaNodeType(p.type))
            return false;
        out.push_back(std::move(p));
    }
    return r.ok();
}

void writeRows(Writer &w, const NodeRows &rows, const std::vector<PortSpec> &ports)
{
    w.i32(static_cast<std::int32_t>(rows.size()));
    for (const auto &row : rows)
        for (std::size_t k = 0; k < ports.size(); ++k)
            writeValue(w, ports[k].type, k < row.size() ? row[k] : NodeValue{});
}

std::optional<NodeRows> readRows(Reader &r, std::size_t payloadSize, const std::vector<PortSpec> &ports)
{
    const auto n = count(r, payloadSize);
    if (!n)
        return std::nullopt;
    NodeRows rows;
    for (std::size_t i = 0; i < *n && r.ok(); ++i) {
        std::vector<NodeValue> row;
        for (const PortSpec &p : ports)
            row.push_back(readValue(r, p.type));
        rows.push_back(std::move(row));
    }
    if (!r.ok())
        return std::nullopt;
    return rows;
}

}

std::vector<std::byte> encodeNodeCall(const application::LuaNodePort::Call &call)
{
    Writer w;
    w.str(call.node).str(call.code);
    writePorts(w, call.inputs);
    writePorts(w, call.outputs);
    writeRows(w, call.rows, call.inputs);
    return w.take();
}

std::optional<application::LuaNodePort::Call> decodeNodeCall(Reader &in, std::size_t payloadSize)
{
    application::LuaNodePort::Call call;
    call.node = in.str();
    call.code = in.str();
    if (!readPorts(in, payloadSize, call.inputs) || !readPorts(in, payloadSize, call.outputs))
        return std::nullopt;
    auto rows = readRows(in, payloadSize, call.inputs);
    if (!rows || !in.atEnd())
        return std::nullopt;
    call.rows = std::move(*rows);
    return call;
}

std::vector<std::byte> encodeNodeRows(const NodeRows &rows, const std::vector<PortSpec> &outputs)
{
    Writer w;
    writeRows(w, rows, outputs);
    return w.take();
}

std::optional<NodeRows> decodeNodeRows(const std::vector<std::byte> &payload, const std::vector<PortSpec> &outputs)
{
    Reader r(payload);
    auto rows = readRows(r, payload.size(), outputs);
    if (!rows || !r.atEnd())
        return std::nullopt;
    return rows;
}

std::optional<HostServiceReply> decodeHostReply(const std::vector<std::byte> &payload)
{
    Reader r(payload);
    HostServiceReply reply;
    const auto status = r.i32();
    if (!r.ok() || (status != 0 && status != 1))
        return std::nullopt;
    reply.status = static_cast<HostServiceReply::Status>(status);
    if (!readIntegers(r, payload, reply.integers))
        return std::nullopt;
    const auto numbers = r.i32();
    if (!r.ok() || !plausible(numbers, payload))
        return std::nullopt;
    for (std::int32_t i = 0; i < numbers && r.ok(); ++i)
        reply.numbers.push_back(r.f64());
    if (!readStrings(r, payload, reply.strings))
        return std::nullopt;
    reply.pixels = r.bytes();
    if (!r.ok() || !r.atEnd())
        return std::nullopt;
    return reply;
}

}
