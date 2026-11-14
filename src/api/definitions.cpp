#include "operations.h"

#include <algorithm>

namespace hikari::api::detail {

namespace {

// `base`'s properties without `drop`, none required.
Json fieldsOf(const Json &base, std::initializer_list<std::string_view> drop, std::string description)
{
    Json properties = Json::object();
    for (const Json::Member &m : base.find("properties")->asObject())
        if (std::find(drop.begin(), drop.end(), m.key) == drop.end())
            properties[m.key] = m.value;
    return Json::object({{"type", Json("object")},
                         {"description", Json(std::move(description))},
                         {"properties", properties},
                         {"additionalProperties", Json(false)}});
}

}

void addDefinitions(Registry &registry)
{
    registry.define("DocumentId", schema(R"({"type": "integer", "minimum": 1,
        "description": "An open Document, as documents.list gives it."})"));
    registry.define("SkillName", schema(R"({"type": "string", "pattern": "^[a-z0-9]+(-[a-z0-9]+)*$", "maxLength": 64,
        "description": "A skill's name, which is its folder's."})"));
    registry.define("LineId", schema(R"({"type": "integer", "minimum": 1,
        "description": "A Line's stable id: it survives edits, moves and undo, unlike its row."})"));
    registry.define("Revision", schema(R"({"type": "integer", "minimum": 0,
        "description": "Counts the Document's committed changes; a write names the revision it was prepared against."})"));
    registry.define("LineField", schema(R"({"enum": ["comment", "layer", "start", "end", "style", "actor", "marginL",
        "marginR", "marginV", "effect", "text", "translation", "unconfirmed", "generatedFrom"]})"));
    registry.define("Line", schema(R"({
        "type": "object",
        "description": "Times are milliseconds. In translation mode text is the original and translation the translated text.",
        "properties": {
            "id": {"$ref": "#/$defs/LineId"},
            "row": {"type": "integer", "minimum": 1, "description": "1-based position among the Document's Lines."},
            "comment": {"type": "boolean"},
            "layer": {"type": "integer"},
            "start": {"type": "integer"},
            "end": {"type": "integer"},
            "style": {"type": "string"},
            "actor": {"type": "string"},
            "marginL": {"type": "integer"},
            "marginR": {"type": "integer"},
            "marginV": {"type": "integer"},
            "effect": {"type": "string"},
            "text": {"type": "string"},
            "translation": {"type": "string"},
            "unconfirmed": {"type": "boolean"},
            "generatedFrom": {"anyOf": [{"$ref": "#/$defs/LineId"}, {"type": "null"}],
                "description": "The karaoke Line a templater run generated this one from."}
        },
        "required": ["id", "row"],
        "additionalProperties": false
    })"));
    registry.define("Style", schema(R"({
        "type": "object",
        "description": "An ASS v4+ Style. Text fields hold the field's ASS text; colours are &HAABBGGRR.",
        "properties": {
            "name": {"type": "string"},
            "fontname": {"type": "string"},
            "fontsize": {"type": "string"},
            "primaryColour": {"type": "string"},
            "secondaryColour": {"type": "string"},
            "outlineColour": {"type": "string"},
            "backColour": {"type": "string"},
            "bold": {"type": "boolean"},
            "italic": {"type": "boolean"},
            "underline": {"type": "boolean"},
            "strikeOut": {"type": "boolean"},
            "scaleX": {"type": "string"},
            "scaleY": {"type": "string"},
            "spacing": {"type": "string"},
            "angle": {"type": "string"},
            "borderStyle": {"enum": [1, 3, 4]},
            "outline": {"type": "string"},
            "shadow": {"type": "string"},
            "alignment": {"type": "string"},
            "marginL": {"type": "string"},
            "marginR": {"type": "string"},
            "marginV": {"type": "string"},
            "encoding": {"type": "string"}
        },
        "required": ["name"],
        "additionalProperties": false
    })"));
    registry.define("LineFields", fieldsOf(*registry.defs().find("Line"), {"id", "row", "generatedFrom"},
                                           "Line fields to set; the others keep their values."));
    registry.define("StyleFields", fieldsOf(*registry.defs().find("Style"), {},
                                            "Style fields to set; name renames it. A new Style starts from Arial 48."));
    registry.define("Edit", schema(R"({"oneOf": [
        {"type": "object", "properties": {"op": {"const": "set_line"}, "line": {"$ref": "#/$defs/LineId"},
            "fields": {"$ref": "#/$defs/LineFields"}}, "required": ["op", "line", "fields"], "additionalProperties": false},
        {"type": "object", "description": "After or before a Line; at the end without either.",
            "properties": {"op": {"const": "insert_line"}, "after": {"$ref": "#/$defs/LineId"},
            "before": {"$ref": "#/$defs/LineId"}, "fields": {"$ref": "#/$defs/LineFields"}},
            "required": ["op"], "additionalProperties": false},
        {"type": "object", "properties": {"op": {"const": "delete_line"}, "line": {"$ref": "#/$defs/LineId"}},
            "required": ["op", "line"], "additionalProperties": false},
        {"type": "object", "description": "Before a Line, or to the end with before null.",
            "properties": {"op": {"const": "move_line"}, "line": {"$ref": "#/$defs/LineId"},
            "before": {"anyOf": [{"$ref": "#/$defs/LineId"}, {"type": "null"}]}},
            "required": ["op", "line", "before"], "additionalProperties": false},
        {"type": "object", "description": "Changes the Style with that name, or adds it.",
            "properties": {"op": {"const": "set_style"}, "name": {"type": "string", "minLength": 1},
            "fields": {"$ref": "#/$defs/StyleFields"}}, "required": ["op", "name"], "additionalProperties": false},
        {"type": "object", "properties": {"op": {"const": "delete_style"}, "name": {"type": "string", "minLength": 1}},
            "required": ["op", "name"], "additionalProperties": false},
        {"type": "object", "description": "A null value removes the property.",
            "properties": {"op": {"const": "set_info"}, "key": {"type": "string", "minLength": 1},
            "value": {"type": ["string", "null"]}}, "required": ["op", "key", "value"], "additionalProperties": false}
    ]})"));
    registry.define("SettingValue", schema(R"({"anyOf": [{"type": "boolean"}, {"type": "integer"}, {"type": "string"},
        {"type": "array", "items": {"type": "string"}}]})"));
    registry.define("NodePort", schema(R"({"type": "object", "properties": {"name": {"type": "string"},
        "type": {"type": "string"}}, "required": ["name", "type"], "additionalProperties": false})"));
    registry.define("GrantedFile", schema(R"({"type": "object", "properties": {
        "file": {"type": "string", "description": "The handle files.read and files.write take."},
        "name": {"type": "string"}, "access": {"enum": ["read", "write"]}},
        "required": ["file", "name", "access"], "additionalProperties": false})"));
    registry.define("GrantedFolder", schema(R"({"type": "object", "properties": {
        "folder": {"type": "string", "description": "The handle files.list, files.read and files.write take with a path."},
        "name": {"type": "string"}, "access": {"enum": ["read", "write"]}},
        "required": ["folder", "name", "access"], "additionalProperties": false})"));
    registry.define("FolderEntry", schema(R"({"type": "object", "properties": {
        "path": {"type": "string", "description": "Relative to the granted folder, with / between its parts."},
        "folder": {"type": "boolean"}}, "required": ["path", "folder"], "additionalProperties": false})"));
    registry.define("Project", schema(R"({"anyOf": [{"type": "null"}, {"type": "object", "properties": {
        "name": {"type": "string", "description": "The Project folder's name."},
        "trusted": {"type": "boolean", "description": "Whether the folder is in Trusted folders."}},
        "required": ["name", "trusted"], "additionalProperties": false}],
        "description": "The Project of the window the user works in; null without one."})"));
    registry.define("DialogControl", schema(R"({"type": "object", "properties": {
        "name": {"type": "string"},
        "kind": {"enum": ["edit", "textbox", "intedit", "floatedit", "dropdown", "checkbox", "color", "coloralpha", "alpha"],
            "description": "The control's Lua class, lowercased."},
        "label": {"type": "string", "description": "A checkbox's text."},
        "hint": {"type": "string"},
        "items": {"type": "array", "items": {"type": "string"}, "description": "A dropdown's choices."},
        "min": {"type": "number"}, "max": {"type": "number"},
        "step": {"type": "number", "description": "A floatedit's step; 0 for none."}},
        "required": ["name", "kind"], "additionalProperties": false})"));
    registry.define("DialogAnswer", schema(R"({"type": "object", "properties": {
        "key": {"type": "string", "pattern": "^[0-9a-f]{16}$",
            "description": "The dialog's key: its script and its controls' kinds and names, as Automation remembers answers by. When given, the key alone decides which dialog the answer fills."},
        "controls": {"type": "array", "maxItems": 256, "items": {"$ref": "#/$defs/DialogControl"},
            "description": "The dialog's value-bearing controls, in order. Without a key, the answer fills the dialog of the script being run whose controls have these kinds and names; the other fields don't count."},
        "values": {"type": "object", "additionalProperties": {"type": ["string", "number", "boolean"]},
            "description": "The answered values by control name."},
        "button": {"type": "string", "description": "The pressed button's label; empty when the dialog was closed."}},
        "required": ["values"],
        "anyOf": [{"required": ["key"]}, {"required": ["controls"]}],
        "additionalProperties": false})"));
    registry.define("MarkColour", schema(R"({"enum": ["accent", "warning", "danger", "success", "muted"],
        "description": "A theme role, so the mark follows the theme and high contrast."})"));
}

}
