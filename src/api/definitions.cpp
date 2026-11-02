#include "operations.h"

namespace hikari::api::detail {

void addDefinitions(Registry &registry)
{
    registry.define("DocumentId", schema(R"({"type": "integer", "minimum": 1,
        "description": "An open Document, as documents.list gives it."})"));
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
            "borderStyle": {"enum": [1, 3]},
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
    registry.define("SettingValue", schema(R"({"anyOf": [{"type": "boolean"}, {"type": "integer"}, {"type": "string"},
        {"type": "array", "items": {"type": "string"}}]})"));
}

}
