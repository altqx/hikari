#include "operations.h"

#include "hikari/application/all_tags.h"
#include "hikari/application/document_overlay.h"
#include "hikari/application/resample.h"
#include "hikari/application/visual_all_tags.h"
#include "hikari/application/visual_clip.h"
#include "hikari/application/visual_drawing.h"
#include "hikari/application/visual_perspective.h"
#include "hikari/application/visual_shift.h"
#include "hikari/core/text_projection.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <map>

namespace hikari::api::detail {

namespace {

namespace visual = application::visual;
namespace persp = application::visual::perspective;

using application::CommandRefusal;
using application::EditSession;
using application::visual::Gesture;
using core::LineId;

constexpr double kDefaultTolerance = 4;
constexpr int kDeviceScale = 100;

std::u16string u16(std::string_view utf8)
{
    return core::toUtf16(std::u8string_view(reinterpret_cast<const char8_t *>(utf8.data()), utf8.size()));
}

std::string utf8(std::u16string_view text)
{
    return narrow(core::toUtf8(text));
}

LineId lineId(const Json &id)
{
    return LineId{std::uint64_t(id.asInt())};
}

int layoutResY(const core::Document &document)
{
    int value = 0;
    const std::u8string text = document.scriptInfo(u8"LayoutResY").value_or(u8"");
    const auto *first = reinterpret_cast<const char *>(text.data());
    std::from_chars(first, first + text.size(), value);
    return value;
}

int videoHeight(const Context &context)
{
    const auto size = context.host.media ? context.host.media->videoSize() : std::nullopt;
    return size ? size->height : 0;
}

double screenZ(const core::Document &document, application::Resolution script, int video)
{
    const int layout = layoutResY(document);
    return persp::screenZ(script.height, layout > 0 ? layout : video > 0 ? video : script.height);
}

persp::Reader readerOf(const Context &context, const core::Document &document)
{
    const application::Resolution script = application::scriptResolution(document);
    persp::Reader reader;
    reader.document = &document;
    reader.width = script.width;
    reader.height = script.height;
    if (context.host.typeset)
        reader.measure = context.host.typeset->measure();
    return reader;
}

persp::Quad quadOf(const Json &points)
{
    persp::Quad quad;
    for (std::size_t i = 0; i < quad.size(); ++i) {
        const Json &p = points.asArray()[i];
        quad[i] = {float(p.find("x")->asNumber()), float(p.find("y")->asNumber())};
    }
    return quad;
}

Json pointJson(visual::PointF p)
{
    return Json::object({{"x", Json(double(p.x))}, {"y", Json(double(p.y))}});
}

std::unexpected<Error> badParam(std::string pointer, std::string_view why)
{
    return fail(errors::kInvalidParams, "Parameter " + pointer + ": " + std::string(why) + ".",
                Json::object({{"pointer", Json(pointer)}}));
}

std::u16string textOf(const core::LineRecord &line)
{
    return core::toUtf16(line.text);
}

Result perspective(const Context &context, const Json &params)
{
    auto t = target(context, params);
    if (!t)
        return std::unexpected(t.error());
    const core::Document &document = t->session->document();
    const LineId id = lineId(*params.find("line"));
    const core::LineRecord *line = findLine(document, id);
    if (!line)
        return noLine(id);
    const persp::Quad quad = quadOf(*params.find("quad"));
    if (!persp::convex(quad))
        return badParam("/quad", "the corners must go round a convex quad: top left, top right, bottom right, bottom left");
    const std::string origin = params.find("origin") ? params.find("origin")->asString() : "center";
    const persp::OrgMode mode = origin == "keep"    ? persp::OrgMode::Keep
                                : origin == "nofax" ? persp::OrgMode::NoFax
                                                    : persp::OrgMode::Center;
    const persp::Reader reader = readerOf(context, document);
    persp::StyleCache styles;
    const auto state = persp::readLine(reader, *line, styles);
    if (!state)
        return fail(errors::kRefused, "Line " + std::to_string(id.value) + " cannot be measured here.",
                    Json::object({{"reason", Json("unmeasured")}, {"line", Json(std::int64_t(id.value))}}));
    const auto transform = persp::quadToTransform(
        quad, state->box, mode, state->transform,
        screenZ(document, {reader.width, reader.height}, videoHeight(context)));
    if (!transform)
        return badParam("/quad", "no transform puts the Line on that quad");
    const persp::Transform &out = *transform;
    return Json::object({{"line", Json(std::int64_t(id.value))},
                         {"text", Json(utf8(persp::writeTransform(reader, styles, *line, out)))},
                         {"transform", Json::object({{"pos", pointJson(out.pos)},
                                                     {"org", pointJson(out.org)},
                                                     {"frx", Json(out.frx)},
                                                     {"fry", Json(out.fry)},
                                                     {"frz", Json(out.frz)},
                                                     {"fax", Json(out.fax)},
                                                     {"fscx", Json(out.fscx)},
                                                     {"fscy", Json(out.fscy)}})}});
}

Result clipOperation(const Context &context, const Json &params)
{
    auto t = target(context, params);
    if (!t)
        return std::unexpected(t.error());
    const LineId id = lineId(*params.find("line"));
    const core::LineRecord *line = findLine(t->session->document(), id);
    if (!line)
        return noLine(id);
    const bool inverse = params.find("inverse") && params.find("inverse")->asBool();
    std::u16string text = textOf(*line);
    if (const Json *rect = params.find("rect")) {
        const auto corner = [&](const char *key) { return int(std::lround(rect->find(key)->asNumber())); };
        const int ax = corner("x1"), bx = corner("x2"), ay = corner("y1"), by = corner("y2");
        text = visual::clip::putRectangle(std::move(text), std::min(ax, bx), std::min(ay, by), std::max(ax, bx),
                                          std::max(ay, by), inverse);
    } else if (const Json *shape = params.find("vector")) {
        text = visual::clip::putVector(std::move(text), u16(shape->asString()));
        if (!shape->asString().empty())
            if (auto renamed = visual::clip::renameClips(text, inverse ? u"iclip" : u"clip", true))
                text = std::move(*renamed);
    } else {
        return badParam("/rect", "give rect or vector");
    }
    return Json::object({{"line", Json(std::int64_t(id.value))}, {"text", Json(utf8(text))}});
}

Result drawingOperation(const Context &context, const Json &params)
{
    auto t = target(context, params);
    if (!t)
        return std::unexpected(t.error());
    const core::Document &document = t->session->document();
    const LineId id = lineId(*params.find("line"));
    const core::LineRecord *line = findLine(document, id);
    if (!line)
        return noLine(id);
    visual::PointF position;
    int alignment = 7;
    if (const Json *at = params.find("position")) {
        position = {float(at->find("x")->asNumber()), float(at->find("y")->asNumber())};
    } else {
        const persp::Reader reader = readerOf(context, document);
        persp::StyleCache styles;
        const persp::LineState state = persp::readTags(reader, *line, styles);
        position = state.transform.pos;
        alignment = state.transform.an;
    }
    if (const Json *an = params.find("alignment"))
        alignment = int(an->asInt());
    const auto put = visual::drawing::putDrawing(textOf(*line), u16(params.find("shape")->asString()), position, alignment);
    return Json::object({{"line", Json(std::int64_t(id.value))}, {"text", Json(utf8(put.text))}});
}

// Runs a visual tool on a copy of the Document, as the video would with one device
// pixel a hundredth of a script unit; nothing reaches the real Document.
class Sandbox final : public visual::VisualHost {
public:
    Sandbox(const core::Document &document, std::vector<LineId> lines, TypesetHost *typeset)
        : m_session(document), m_lines(std::move(lines))
    {
        m_session.setSelection({m_lines.front(), {m_lines.begin(), m_lines.end()}, m_lines.front(), std::nullopt});
        const application::Resolution script = application::scriptResolution(document);
        m_view.setClient(script.width * kDeviceScale, script.height * kDeviceScale, 0);
        m_view.setScript(script.width, script.height);
        m_view.open({script.width, script.height, 1, 1});
        if (const core::LineRecord *active = findLine(document, m_lines.front()))
            m_time = active->start.value.microseconds() / 1000;
        if (typeset)
            m_measure = typeset->measure();
    }

    // The Lines whose text the tool changed, with their new text.
    Json changed(const core::Document &original) const
    {
        Json out = Json::array();
        for (const LineId id : m_lines) {
            const core::LineRecord *before = findLine(original, id);
            const core::LineRecord *after = findLine(m_session.document(), id);
            if (before && after && before->text != after->text)
                out.push(Json::object({{"line", Json(std::int64_t(id.value))}, {"text", Json(narrow(after->text))}}));
        }
        return out;
    }

    const visual::VideoView &view() const override { return m_view; }
    const EditSession *session() const override { return &m_session; }
    std::optional<LineId> activeLine() const override { return m_lines.front(); }
    std::vector<LineId> batchTargets() const override { return m_lines; }
    std::expected<Gesture *, CommandRefusal> beginGesture(std::vector<LineId> targets, std::string history) override
    {
        if (m_gesture)
            return std::unexpected(CommandRefusal::Invalid);
        auto begun = Gesture::begin(m_session, std::move(targets), std::move(history));
        if (!begun)
            return std::unexpected(begun.error());
        m_gesture.emplace(std::move(*begun));
        return &*m_gesture;
    }
    Gesture *gesture() override { return m_gesture ? &*m_gesture : nullptr; }
    std::expected<void, CommandRefusal> commitGesture() override
    {
        auto committed = m_gesture->commit(m_session);
        m_gesture.reset();
        return committed;
    }
    void cancelGesture() override { m_gesture.reset(); }
    std::pair<int, int> measureLabel(std::u16string_view text) const override
    {
        return {7 * int(text.size()), 14};
    }
    void toolChanged() override {}
    std::int64_t videoTimeMs() const override { return m_time; }
    application::TextMeasurePort *textMeasure() const override { return m_measure.port; }
    bool fontLeased(const std::string &name) const override { return m_measure.leased && m_measure.leased(name); }
    bool fallsBackToLeased(const std::string &font, bool bold, bool italic, std::u16string_view text) const override
    {
        return m_measure.fallsBackToLeased && m_measure.fallsBackToLeased(font, bold, italic, text);
    }
    const std::vector<visual::AllTagsSetting> *allTagsSettings() const override { return &m_tags; }

    std::vector<visual::AllTagsSetting> m_tags;

private:
    EditSession m_session;
    std::vector<LineId> m_lines;
    visual::VideoView m_view;
    std::optional<Gesture> m_gesture;
    std::int64_t m_time = 0;
    persp::TextMeasure m_measure;
};

std::expected<std::vector<LineId>, Error> linesOf(const core::Document &document, const Json &ids)
{
    std::vector<LineId> out;
    for (const Json &id : ids.asArray()) {
        const LineId line = lineId(id);
        if (!findLine(document, line))
            return noLine(line);
        if (std::find(out.begin(), out.end(), line) == out.end())
            out.push_back(line);
    }
    return out;
}

constexpr std::array<std::pair<std::string_view, int>, 6> kShiftKinds{{
    {"position", visual::PositionShifterTool::Position},
    {"move_start", visual::PositionShifterTool::MoveStart},
    {"move_end", visual::PositionShifterTool::MoveEnd},
    {"clip", visual::PositionShifterTool::Clip},
    {"drawing", visual::PositionShifterTool::Drawing},
    {"origin", visual::PositionShifterTool::Origin},
}};

Result shift(const Context &context, const Json &params)
{
    auto t = target(context, params);
    if (!t)
        return std::unexpected(t.error());
    const core::Document &document = t->session->document();
    auto lines = linesOf(document, *params.find("lines"));
    if (!lines)
        return std::unexpected(lines.error());
    int kinds = 0;
    if (const Json *named = params.find("kinds")) {
        for (const Json &kind : named->asArray())
            for (const auto &[name, bit] : kShiftKinds)
                if (kind.asString() == name)
                    kinds |= bit;
    } else {
        kinds = visual::PositionShifterTool::Position | visual::PositionShifterTool::MoveStart |
                visual::PositionShifterTool::MoveEnd | visual::PositionShifterTool::Clip |
                visual::PositionShifterTool::Origin;
    }
    Sandbox sandbox(document, *lines, context.host.typeset);
    visual::PositionShifterTool tool;
    int bits = 0;
    for (std::size_t i = 0; i < kShiftKinds.size(); ++i)
        if (kinds & kShiftKinds[i].second)
            bits |= 1 << i;
    tool.setToggled(bits);
    tool.selected(sandbox);
    tool.reset(sandbox);
    if (tool.elements().empty())
        return Json::object({{"lines", Json::array()}});
    const visual::PointF handle = tool.elements().front().elem;
    const int x = int(std::lround(handle.x)), y = int(std::lround(handle.y));
    const int toX = x + int(std::lround(params.find("dx")->asNumber() * kDeviceScale));
    const int toY = y + int(std::lround(params.find("dy")->asNumber() * kDeviceScale));
    visual::Pointer press;
    press.kind = visual::Pointer::Kind::Press;
    press.button = visual::Pointer::Button::Right;
    press.rightDown = true;
    press.x = x;
    press.y = y;
    tool.pointer(press, sandbox);
    visual::Pointer move = press;
    move.kind = visual::Pointer::Kind::Move;
    move.button = visual::Pointer::Button::None;
    move.x = toX;
    move.y = toY;
    tool.pointer(move, sandbox);
    visual::Pointer release = move;
    release.kind = visual::Pointer::Kind::Release;
    release.button = visual::Pointer::Button::Right;
    release.rightDown = false;
    tool.pointer(release, sandbox);
    return Json::object({{"lines", sandbox.changed(document)}});
}

std::optional<visual::AllTagsSetting> definitionNamed(std::string_view name)
{
    std::u16string wanted = u16(name.starts_with('\\') ? name.substr(1) : name);
    if (wanted == u"c")
        wanted = u"1c";
    for (const visual::AllTagsSetting &tag : visual::defaultAllTags())
        if (tag.tag == wanted || tag.name == wanted)
            return tag;
    return std::nullopt;
}

std::string definitionList()
{
    std::string out;
    for (const visual::AllTagsSetting &tag : visual::defaultAllTags())
        out += (out.empty() ? "" : ", ") + utf8(tag.tag);
    return out;
}

std::string numberText(double value)
{
    char buffer[32];
    const auto end = std::to_chars(buffer, buffer + sizeof buffer, value).ptr;
    return std::string(buffer, end);
}

Result hydra(const Context &context, const Json &params)
{
    auto t = target(context, params);
    if (!t)
        return std::unexpected(t.error());
    const core::Document &document = t->session->document();
    auto lines = linesOf(document, *params.find("lines"));
    if (!lines)
        return std::unexpected(lines.error());
    auto definition = definitionNamed(params.find("tag")->asString());
    if (!definition)
        return badParam("/tag", "it is none of " + definitionList());
    static const std::map<std::string, visual::AllTagsTool::Change, std::less<>> changes{
        {"set", visual::AllTagsTool::Change::Set},
        {"add", visual::AllTagsTool::Change::Add},
        {"multiply", visual::AllTagsTool::Change::Multiply},
        {"gradient", visual::AllTagsTool::Change::Gradient}};
    const auto change = changes.find(params.find("change")->asString())->second;

    std::vector<std::pair<std::string, double>> fields;
    const auto take = [&](const char *key, const std::string &role) {
        if (const Json *values = params.find(key))
            for (std::size_t i = 0; i < values->asArray().size() && i < definition->numOfValues; ++i)
                fields.emplace_back(role + std::to_string(i), values->asArray()[i].asNumber());
    };
    int variant = -1;
    switch (change) {
    case visual::AllTagsTool::Change::Set:
        take("values", "value");
        definition->mode = 1;
        break;
    case visual::AllTagsTool::Change::Add: take("values", "by"); break;
    case visual::AllTagsTool::Change::Multiply:
        take("values", "start");
        take("perLine", "perLine");
        variant = params.find("byLine") && params.find("byLine")->asString() == "add" ? 1 : 0;
        break;
    case visual::AllTagsTool::Change::Gradient: {
        take("values", "from");
        take("to", "to");
        const bool lines = params.find("across") && params.find("across")->asString() == "lines";
        const bool decreasing = params.find("decreasing") && params.find("decreasing")->asBool();
        variant = (lines ? 2 : 0) + (decreasing ? 1 : 0);
        break;
    }
    }
    if (fields.empty())
        return badParam("/values", "give at least one value");

    Sandbox sandbox(document, *lines, context.host.typeset);
    sandbox.m_tags = {*definition};
    visual::AllTagsTool tool;
    tool.selected(sandbox);
    tool.reset(sandbox);
    tool.setOption("tag", 0, sandbox);
    tool.setOption("change", int(change), sandbox);
    if (variant >= 0)
        tool.setOption("variant", variant, sandbox);
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const auto edit = i + 1 < fields.size() ? visual::ValueEdit::Sample : visual::ValueEdit::Commit;
        if (!tool.setValue(fields[i].first, u16(numberText(fields[i].second)), sandbox, edit))
            return fail(errors::kRefused, "The tag \\" + utf8(definition->tag) + " cannot take that change here.",
                        Json::object({{"reason", Json("refused")}, {"value", Json(fields[i].first)}}));
    }
    return Json::object({{"lines", sandbox.changed(document)}});
}

struct Bounds {
    double left = 0, top = 0, right = 0, bottom = 0;
};

Json boundsJson(const Bounds &b)
{
    return Json::object({{"x", Json(b.left)}, {"y", Json(b.top)}, {"width", Json(b.right - b.left)},
                         {"height", Json(b.bottom - b.top)}});
}

// The target in video pixels: a quad's or a box's bounds, from script coordinates.
Bounds targetBounds(const Json &target, double scaleX, double scaleY)
{
    Bounds b;
    if (const Json *quad = target.find("quad")) {
        b = {1e300, 1e300, -1e300, -1e300};
        for (const Json &p : quad->asArray()) {
            const double x = p.find("x")->asNumber() * scaleX, y = p.find("y")->asNumber() * scaleY;
            b = {std::min(b.left, x), std::min(b.top, y), std::max(b.right, x), std::max(b.bottom, y)};
        }
        return b;
    }
    const Json &box = *target.find("box");
    const double x = box.find("x")->asNumber(), y = box.find("y")->asNumber();
    return {x * scaleX, y * scaleY, (x + box.find("width")->asNumber()) * scaleX,
            (y + box.find("height")->asNumber()) * scaleY};
}

struct Check {
    std::int64_t frame = 0;
    std::optional<Bounds> ink;
    Bounds target;
    std::optional<double> error;
    bool pass = false;

    Json json() const
    {
        return Json::object({{"frame", Json(frame)},
                             {"pass", Json(pass)},
                             {"error", error ? Json(*error) : Json()},
                             {"ink", ink ? boundsJson(*ink) : Json()},
                             {"target", boundsJson(target)}});
    }
};

std::expected<Check, Error> check(const Context &context, const Target &t, const Json &item, double tolerance)
{
    const core::Document &document = t.session->document();
    const LineId id = lineId(*item.find("line"));
    const core::LineRecord *line = findLine(document, id);
    if (!line)
        return noLine(id);
    core::Document drawn = application::withOnlyLine(document, id);
    if (const Json *text = item.find("text"))
        drawn.editLine(id, [&](core::LineRecord &record) {
            record.text = std::u8string(text->asString().begin(), text->asString().end());
            record.unparsed = false;
        });
    const auto size = context.host.media->videoSize();
    const application::Resolution script = application::scriptResolution(document);
    Check out;
    out.frame = frameOf(*context.host.media, item, line);
    out.target = targetBounds(*item.find("target"), double(size->width) / script.width,
                              double(size->height) / script.height);
    const auto overlay = context.host.render->render(t.id, drawn, out.frame, size->width, size->height);
    if (!overlay)
        return notDrawn(out.frame);
    if (const auto ink = application::inkBounds(*overlay)) {
        out.ink = Bounds{double(ink->x), double(ink->y), double(ink->x + ink->width), double(ink->y + ink->height)};
        out.error = std::max({std::abs(out.ink->left - out.target.left), std::abs(out.ink->top - out.target.top),
                              std::abs(out.ink->right - out.target.right),
                              std::abs(out.ink->bottom - out.target.bottom)});
        out.pass = *out.error <= tolerance;
    }
    return out;
}

double toleranceOf(const Json &params)
{
    return params.find("tolerance") ? params.find("tolerance")->asNumber() : kDefaultTolerance;
}

std::optional<Error> canRender(const Context &context)
{
    if (!videoOpen(context))
        return noVideo().error();
    if (!context.host.render)
        return cannotDraw().error();
    return std::nullopt;
}

Result checkOperation(const Context &context, const Json &params)
{
    if (auto missing = canRender(context))
        return std::unexpected(*missing);
    auto t = target(context, params);
    if (!t)
        return std::unexpected(t.error());
    auto checked = check(context, *t, params, toleranceOf(params));
    if (!checked)
        return std::unexpected(checked.error());
    Json out = checked->json();
    out["line"] = *params.find("line");
    return out;
}

Result apply(const Context &context, const Json &params)
{
    if (auto missing = canRender(context))
        return std::unexpected(*missing);
    auto t = target(context, params);
    if (!t)
        return std::unexpected(t.error());
    const double tolerance = toleranceOf(params);
    Json checks = Json::array(), failed = Json::array(), edits = Json::array();
    for (const Json &item : params.find("lines")->asArray()) {
        auto checked = check(context, *t, item, tolerance);
        if (!checked)
            return std::unexpected(checked.error());
        Json result = checked->json();
        result["line"] = *item.find("line");
        if (!checked->pass)
            failed.push(result);
        checks.push(std::move(result));
        edits.push(Json::object({{"op", Json("set_line")},
                                 {"line", *item.find("line")},
                                 {"fields", Json::object({{"text", *item.find("text")}})}}));
    }
    if (!failed.asArray().empty())
        return fail(errors::kRefused,
                    std::to_string(failed.asArray().size()) +
                        " Line(s) do not land on their target within " + numberText(tolerance) +
                        " video pixels; nothing was applied. Fix the placement and check again.",
                    Json::object({{"reason", Json("check_failed")}, {"lines", std::move(failed)}}));
    Json forward = Json::object({{"revision", *params.find("revision")},
                                 {"name", *params.find("name")},
                                 {"edits", std::move(edits)}});
    if (const Json *document = params.find("document"))
        forward["document"] = *document;
    auto applied = documentApply(context, forward);
    if (!applied)
        return applied;
    (*applied)["checks"] = std::move(checks);
    return applied;
}

constexpr std::string_view kPoint = R"({"type": "object", "properties": {"x": {"type": "number"},
    "y": {"type": "number"}}, "required": ["x", "y"], "additionalProperties": false})";

std::string expand(std::string_view text)
{
    const std::string quad = R"({"type": "array", "minItems": 4, "maxItems": 4, "items": )" + std::string(kPoint) +
                             R"(, "description": "Script coordinates: top left, top right, bottom right, bottom left."})";
    const std::string box = R"({"type": "object", "properties": {"x": {"type": "number"}, "y": {"type": "number"},
        "width": {"type": "number", "minimum": 0}, "height": {"type": "number", "minimum": 0}},
        "required": ["x", "y", "width", "height"], "additionalProperties": false})";
    const std::string target = R"({"description": "Where the Line's ink should land, in script coordinates: a quad or a box.",
        "oneOf": [{"type": "object", "properties": {"quad": )" + quad + R"(}, "required": ["quad"], "additionalProperties": false},
        {"type": "object", "properties": {"box": )" + box + R"(}, "required": ["box"], "additionalProperties": false}]})";
    std::string out(text);
    const auto put = [&](std::string_view key, const std::string &value) {
        for (std::size_t at = out.find(key); at != std::string::npos; at = out.find(key, at + value.size()))
            out.replace(at, key.size(), value);
    };
    put("@quad", quad);
    put("@target", target);
    put("@point", std::string(kPoint));
    return out;
}

constexpr std::string_view kDocument = R"("document": {"$ref": "#/$defs/DocumentId", "description": "Defaults to the active Document."})";

constexpr std::string_view kProposed = R"({"type": "object", "properties": {"line": {"$ref": "#/$defs/LineId"},
    "text": {"type": "string", "description": "The Line's text with the tags written; nothing is applied."}},
    "required": ["line", "text"], "additionalProperties": false})";

constexpr std::string_view kProposedLines = R"({"type": "object", "properties": {"lines": {"type": "array",
    "items": {"type": "object", "properties": {"line": {"$ref": "#/$defs/LineId"}, "text": {"type": "string"}},
    "required": ["line", "text"], "additionalProperties": false},
    "description": "The Lines the change alters, with their new text; nothing is applied."}},
    "required": ["lines"], "additionalProperties": false})";

constexpr std::string_view kCheck = R"("frame": {"type": "integer"}, "pass": {"type": "boolean"},
    "error": {"type": ["number", "null"], "description": "The largest distance between an edge of the ink's bounds and the target's, in video pixels; null when nothing is drawn."},
    "ink": {"anyOf": [@rect, {"type": "null"}]}, "target": @rect)";

std::string withCheckRects(std::string_view text)
{
    std::string out(text);
    constexpr std::string_view rect = R"({"type": "object", "properties": {"x": {"type": "number"},
        "y": {"type": "number"}, "width": {"type": "number"}, "height": {"type": "number"}},
        "required": ["x", "y", "width", "height"], "additionalProperties": false})";
    for (std::size_t at = out.find("@rect"); at != std::string::npos; at = out.find("@rect", at + rect.size()))
        out.replace(at, 5, rect);
    return out;
}

void read(Registry &registry, std::string name, std::string summary, const std::string &params,
          std::string_view result, SyncHandler handler)
{
    addOperation(registry, Access::Read, std::move(name), std::move(summary), params, result, now(std::move(handler)));
}

}

void addTypesetOperations(Registry &registry)
{
    read(registry, "typeset.perspective",
         "The Line's text with the tags that render it on a quad (VT3's perspective solver): \\pos, \\org, "
         "\\frx, \\fry, \\frz, \\fax, \\fscx and \\fscy.",
         expand(R"({"type": "object", "properties": {)" + std::string(kDocument) + R"(,
             "line": {"$ref": "#/$defs/LineId"}, "quad": @quad,
             "origin": {"enum": ["center", "nofax", "keep"], "description": "Where \\org goes: the quad's centre (default), the centre without \\fax, or where the Line has it."}},
             "required": ["line", "quad"], "additionalProperties": false})"),
         R"({"type": "object", "properties": {"line": {"$ref": "#/$defs/LineId"}, "text": {"type": "string"},
             "transform": {"type": "object"}}, "required": ["line", "text", "transform"], "additionalProperties": false})",
         perspective);
    read(registry, "typeset.clip", "The Line's text with its first clip replaced by a rectangle or a vector clip.",
         R"({"type": "object", "properties": {)" + std::string(kDocument) + R"(,
             "line": {"$ref": "#/$defs/LineId"},
             "rect": {"type": "object", "properties": {"x1": {"type": "number"}, "y1": {"type": "number"},
                 "x2": {"type": "number"}, "y2": {"type": "number"}}, "required": ["x1", "y1", "x2", "y2"],
                 "additionalProperties": false, "description": "Script coordinates, rounded to whole units."},
             "vector": {"type": "string", "maxLength": 100000, "description": "ASS drawing commands such as \"m 0 0 l 100 0 100 50 0 50\"; empty removes the vector clip."},
             "inverse": {"type": "boolean", "description": "\\iclip: draw outside the clip."}},
             "required": ["line"], "additionalProperties": false})",
         kProposed, clipOperation);
    read(registry, "typeset.drawing",
         "The Line's text with its first drawing replaced by a shape (\\p1), placed at a position with an alignment.",
         expand(R"({"type": "object", "properties": {)" + std::string(kDocument) + R"(,
             "line": {"$ref": "#/$defs/LineId"},
             "shape": {"type": "string", "minLength": 1, "maxLength": 100000, "description": "ASS drawing commands, in script units from the drawing's origin."},
             "position": @point,
             "alignment": {"type": "integer", "minimum": 1, "maximum": 9, "description": "\\an; 7 (top left) with position, else the Line's."}},
             "required": ["line", "shape"], "additionalProperties": false})"),
         kProposed, drawingOperation);
    read(registry, "typeset.shift",
         "Moves the Lines' positions, \\move points, clips, drawings and \\org by an offset, as the Position "
         "shifter does.",
         R"({"type": "object", "properties": {)" + std::string(kDocument) + R"(,
             "lines": {"type": "array", "minItems": 1, "maxItems": 1000, "items": {"$ref": "#/$defs/LineId"}},
             "dx": {"type": "number", "description": "Script units."}, "dy": {"type": "number"},
             "kinds": {"type": "array", "items": {"enum": ["position", "move_start", "move_end", "clip", "drawing", "origin"]},
                 "description": "What moves; every kind but drawing when omitted."}},
             "required": ["lines", "dx", "dy"], "additionalProperties": false})",
         kProposedLines, shift);
    read(registry, "typeset.hydra",
         "Changes one tag across the Lines as the Hydra strip does: set it, add to it, multiply it by line "
         "number, or run a gradient across characters or Lines.",
         R"({"type": "object", "properties": {)" + std::string(kDocument) + R"(,
             "lines": {"type": "array", "minItems": 1, "maxItems": 1000, "items": {"$ref": "#/$defs/LineId"},
                 "description": "Multiply and the line gradients count them in this order."},
             "tag": {"type": "string", "minLength": 1, "description": "A Hydra tag without the backslash: blur, bord, be, alpha, 1a, 3a, 4a, 1c (or c), 3c, 4c, fad, fax, fay, fs, fsp, shad, xbord, ybord, xshad, yshad, pos or t."},
             "change": {"enum": ["set", "add", "multiply", "gradient"]},
             "values": {"type": "array", "minItems": 1, "maxItems": 4, "items": {"type": "number"},
                 "description": "set: the value; add: what to add; multiply: the start; gradient: the first value. Colours are R, G, B."},
             "perLine": {"type": "array", "maxItems": 4, "items": {"type": "number"}, "description": "multiply: added for each Line."},
             "byLine": {"enum": ["set", "add"], "description": "multiply: set each Line's value, or add to it. Default set."},
             "to": {"type": "array", "maxItems": 4, "items": {"type": "number"}, "description": "gradient: the last value."},
             "across": {"enum": ["characters", "lines"], "description": "gradient: default characters."},
             "decreasing": {"type": "boolean", "description": "gradient: run from the end."}},
             "required": ["lines", "tag", "change", "values"], "additionalProperties": false})",
         kProposedLines, hydra);
    read(registry, "typeset.check",
         "Renders one Line alone, with a proposed text, on its frame and compares its ink with a target.",
         expand(R"({"type": "object", "properties": {)" + std::string(kDocument) + R"(,
             "line": {"$ref": "#/$defs/LineId"},
             "text": {"type": "string", "description": "The proposed text; the Line's own when omitted."},
             "target": @target,
             "frame": {"type": "integer", "minimum": 0, "description": "Default: the frame shown when the Line is on it, else the Line's first."},
             "tolerance": {"type": "number", "minimum": 0, "description": "Video pixels; default 4."}},
             "required": ["line", "target"], "additionalProperties": false})"),
         withCheckRects(R"({"type": "object", "properties": {"line": {"$ref": "#/$defs/LineId"}, )" +
                        std::string(kCheck) + R"(}, "required": ["line", "frame", "pass", "error", "ink", "target"],
             "additionalProperties": false})"),
         checkOperation);
    addOperation(
        registry, Access::Write, "typeset.apply",
        "Applies typeset Lines as one undo step after every one passes the render check; when one misses its "
        "target nothing is applied.",
        expand(R"({"type": "object", "properties": {)" + std::string(kDocument) + R"(,
            "revision": {"$ref": "#/$defs/Revision"},
            "name": {"type": "string", "minLength": 1, "maxLength": 200, "description": "A few words naming the change, such as \"Typeset the shop sign\"."},
            "lines": {"type": "array", "minItems": 1, "maxItems": 100, "items": {"type": "object", "properties": {
                "line": {"$ref": "#/$defs/LineId"}, "text": {"type": "string"}, "target": @target,
                "frame": {"type": "integer", "minimum": 0}}, "required": ["line", "text", "target"], "additionalProperties": false}},
            "tolerance": {"type": "number", "minimum": 0, "description": "Video pixels; default 4."}},
            "required": ["revision", "name", "lines"], "additionalProperties": false})"),
        withCheckRects(R"({"type": "object", "properties": {"revision": {"$ref": "#/$defs/Revision"},
            "inserted": {"type": "array", "items": {"$ref": "#/$defs/LineId"}}, "changed": {"type": "boolean"},
            "checks": {"type": "array", "items": {"type": "object", "properties": {"line": {"$ref": "#/$defs/LineId"}, )" +
                       std::string(kCheck) + R"(}, "required": ["line", "frame", "pass", "error", "ink", "target"],
                "additionalProperties": false}}}, "required": ["revision", "inserted", "changed", "checks"],
            "additionalProperties": false})"),
        now(apply));
}

}
