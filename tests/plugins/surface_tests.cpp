#include "hikari/plugins/surface.h"

#include <gtest/gtest.h>

using namespace hikari::plugins;
using hikari::api::Json;

namespace {

Json json(std::string_view text)
{
    auto parsed = Json::parse(text);
    EXPECT_TRUE(parsed) << text;
    return parsed ? *parsed : Json();
}

std::string treeProblem(std::string_view text)
{
    return panelTreeProblem(json(text)).value_or("");
}

std::string dialogRefusal(std::string_view text)
{
    return dialogProblem(json(text)).value_or("");
}

}

TEST(PluginSurface, EveryKindTheDocsListIsAccepted)
{
    const auto problem = panelTreeProblem(json(R"({"kind": "column", "menu": [{"id": "again", "text": "Check again"}],
        "children": [
          {"kind": "label", "text": "Heading", "style": "heading"},
          {"kind": "button", "id": "apply", "text": "Apply", "style": "primary"},
          {"kind": "icon_button", "id": "refresh", "icon": "refresh", "name": "Check again", "checked": false},
          {"kind": "text", "id": "filter", "value": "", "placeholder": "Search the lines", "grow": true},
          {"kind": "text_area", "id": "note", "rows": 3},
          {"kind": "number", "id": "gap", "value": 80, "minimum": 0, "unit": "ms", "decimals": 0},
          {"kind": "time", "id": "start", "value": 83450},
          {"kind": "check", "id": "comments", "text": "Keep comments", "value": true},
          {"kind": "choice", "id": "which", "options": ["All", "Errors", "Warnings"], "value": "All"},
          {"kind": "radio", "id": "edge", "options": [{"value": "s", "text": "Start"}, {"value": "e", "text": "End"}]},
          {"kind": "dropdown", "id": "glue", "options": ["space", "\\N"]},
          {"kind": "colour", "id": "primary", "value": "&H003DA3E8", "alpha": true},
          {"kind": "slider", "id": "zoom", "value": 0.5, "minimum": 0, "maximum": 1},
          {"kind": "progress", "value": 0.4, "text": "Checking 120 of 312"},
          {"kind": "list", "id": "problems", "rows": [{"id": "1", "text": "First line", "detail": "0:00.00", "icon": "notice-warning"}],
           "selected": "1"},
          {"kind": "tree", "id": "styles", "rows": [{"id": "a", "text": "A", "expanded": true,
            "children": [{"id": "b", "text": "B"}]}]},
          {"kind": "tabs", "id": "pages", "tabs": [{"text": "One", "body": {"kind": "empty", "text": "Nothing"}}]},
          {"kind": "group", "title": "Group", "children": [{"kind": "link", "id": "how", "text": "How the checks work"}]},
          {"kind": "image", "src": "media/logo.svg", "width": 16, "height": 16, "name": "Logo"},
          {"kind": "row", "children": []},
          {"kind": "grid", "columns": 2, "children": []},
          {"kind": "canvas", "id": "chart", "height": 160, "name": "Reading speed", "hover": true,
           "tip": {"x": 10, "y": 4, "text": "Line 9"},
           "draw": [{"op": "rect", "x": 0, "y": 0, "w": 10, "h": 20, "fill": "warning", "stroke": "text", "width": 1},
                    {"op": "line", "x1": 0, "y1": 5, "x2": 300, "y2": 5, "stroke": "danger", "dash": [4, 3]},
                    {"op": "polyline", "points": [0, 0, 5, 5, 10, 0], "stroke": "accent"},
                    {"op": "path", "d": "M0 0 L10 10", "fill": "&H0000FF&"},
                    {"op": "ellipse", "x": 1, "y": 1, "w": 4, "h": 4, "fill": "success"},
                    {"op": "text", "x": 4, "y": 4, "text": "17 CPS", "fill": "danger", "align": "right", "size": "small"},
                    {"op": "image", "x": 0, "y": 0, "w": 16, "h": 16, "src": "icon.png"}]}
        ]})"));
    EXPECT_EQ(problem, std::nullopt) << *problem;
}

TEST(PluginSurface, ARefusalNamesThePlace)
{
    EXPECT_EQ(treeProblem(R"({"kind": "column", "children": [{"kind": "label", "text": "a"}, {"kind": "foo"}]})"),
              "tree.children[2]: unknown kind “foo”");
    EXPECT_EQ(treeProblem(R"({"kind": "button", "text": "Apply"})"), "tree: needs “id”");
    EXPECT_EQ(treeProblem(R"({"kind": "label", "text": "a", "colour": "red"})"),
              "tree: “colour” isn't a field of a label");
    EXPECT_EQ(treeProblem(R"({"kind": "label", "text": 3})"), "tree.text: must be text");
    EXPECT_EQ(treeProblem(R"({"kind": "label"})"), "tree: needs “text”");
    EXPECT_EQ(treeProblem(R"(["label"])"), "tree: must be a widget, a table with a kind");
    EXPECT_EQ(treeProblem(R"({"kind": "label", "text": "a", "style": "huge"})"),
              "tree.style: must be one of heading, body, muted, small");
    EXPECT_EQ(treeProblem(R"({"kind": "icon_button", "id": "x", "icon": "refresh"})"), "tree: needs “name”");
    EXPECT_EQ(treeProblem(R"({"kind": "image", "src": "../secret.png"})"),
              "tree.src: must be a .png, .svg or .jpg in the package");
}

TEST(PluginSurface, IdsAreUniqueAcrossTheTree)
{
    EXPECT_EQ(treeProblem(R"({"kind": "row", "children": [{"kind": "button", "id": "go", "text": "Go"},
        {"kind": "column", "children": [{"kind": "link", "id": "go", "text": "Go"}]}]})"),
              "tree.children[2].children[1].id: “go” is used twice");
    EXPECT_EQ(treeProblem(R"({"kind": "list", "id": "l", "rows": [{"id": "1", "text": "a"}, {"id": "1", "text": "b"}]})"),
              "tree.rows[2].id: “1” is used twice");
    EXPECT_EQ(treeProblem(R"({"kind": "button", "id": "a b", "text": "Go"})"),
              "tree.id: must be an id of 1 to 64 letters, digits, _ - . or :");
}

TEST(PluginSurface, CanvasColoursAreThemeRolesOrSubtitleColours)
{
    EXPECT_EQ(treeProblem(R"({"kind": "canvas", "id": "c", "height": 10, "name": "C",
        "draw": [{"op": "rect", "x": 0, "y": 0, "w": 1, "h": 1, "fill": "#ff0000"}]})"),
              "tree.draw[1].fill: must be a theme colour (accent, text, muted, line, danger, warning, success, panel, "
              "field) or an ASS colour such as &H0000FF&");
    EXPECT_EQ(treeProblem(R"({"kind": "canvas", "id": "c", "height": 10, "name": "C", "draw": [{"op": "spline"}]})"),
              "tree.draw[1]: unknown operation “spline”");
    EXPECT_EQ(treeProblem(R"({"kind": "canvas", "id": "c", "height": 10, "draw": []})"), "tree: needs “name”");
    EXPECT_EQ(treeProblem(R"({"kind": "canvas", "id": "c", "height": 10, "name": "C",
        "draw": [{"op": "polyline", "points": [1, 2, 3], "stroke": "text"}]})"),
              "tree.draw[1].points: must be x, y pairs, at least two points");
}

TEST(PluginSurface, ChoicesTakeTwoToFourOptions)
{
    EXPECT_EQ(treeProblem(R"({"kind": "choice", "id": "c", "options": ["one"]})"),
              "tree.options: a choice takes 2 to 4 options");
    EXPECT_EQ(treeProblem(R"({"kind": "dropdown", "id": "d", "options": ["one"]})"), "");
}

TEST(PluginSurface, OnlyAPanelsRootCarriesAMenu)
{
    EXPECT_EQ(treeProblem(R"({"kind": "column", "children": [{"kind": "empty", "text": "a",
        "menu": [{"id": "x", "text": "X"}]}]})"),
              "tree.children[1]: “menu” isn't a field of an empty");
    EXPECT_EQ(treeProblem(R"({"kind": "column", "children": [], "menu": [{"id": "x", "text": "X"}, {"id": "x", "text": "Y"}]})"),
              "tree.menu[2].id: “x” is used twice");
}

TEST(PluginSurface, OnlyAPanelsRootTakesDrops)
{
    EXPECT_EQ(treeProblem(R"({"kind": "column", "drop": true, "children": []})"), "");
    EXPECT_EQ(treeProblem(R"({"kind": "column", "drop": "yes", "children": []})"), "tree.drop: must be true or false");
    EXPECT_EQ(treeProblem(R"({"kind": "column", "children": [{"kind": "empty", "text": "a", "drop": true}]})"),
              "tree.children[1]: “drop” isn't a field of an empty");
}

TEST(PluginSurface, TreesAreBounded)
{
    std::string deep = R"({"kind": "label", "text": "x"})";
    for (int i = 0; i < kMaxDepth + 1; ++i)
        deep = R"({"kind": "column", "children": [)" + deep + "]}";
    EXPECT_NE(treeProblem(deep).find("nests deeper than"), std::string::npos);

    Json wide = Json::object({{"kind", "column"}, {"children", Json::array()}});
    for (std::size_t i = 0; i < kMaxNodes; ++i)
        wide["children"].push(Json::object({{"kind", "label"}, {"text", "x"}}));
    EXPECT_NE(panelTreeProblem(wide).value_or("").find("more than 2000 widgets"), std::string::npos);
}

TEST(PluginSurface, ADialogHasATitleABodyAndItsButtons)
{
    const std::string body = R"("body": {"kind": "empty", "text": "a"})";
    EXPECT_EQ(dialogRefusal(R"({"title": "Join", )" + body +
                            R"(, "buttons": [{"id": "join", "text": "Join", "default": true, "style": "primary"},
                                             {"id": "preview", "text": "Preview", "stays_open": true},
                                             {"id": "cancel", "text": "Cancel", "cancel": true}]})"),
              "");
    EXPECT_EQ(dialogRefusal(R"({"title": "Join", )" + body + "}"), "spec: needs “buttons”");
    EXPECT_EQ(dialogRefusal(R"({"title": "Join", )" + body + R"(, "buttons": []})"), "spec.buttons: needs at least one button");
    EXPECT_EQ(dialogRefusal(R"({"title": "Join", )" + body +
                            R"(, "buttons": [{"id": "a", "text": "A", "default": true}, {"id": "b", "text": "B", "default": true}]})"),
              "spec.buttons: only one button may be the default, and only one may cancel");
    EXPECT_EQ(dialogRefusal(R"({"title": "Join", "body": {"kind": "nope"}, "buttons": [{"id": "a", "text": "A"}]})"),
              "spec.body: unknown kind “nope”");
}

TEST(PluginSurface, InputValuesStartFromTheTree)
{
    const Json values = inputValues(json(R"({"kind": "column", "children": [
        {"kind": "text", "id": "filter"}, {"kind": "check", "id": "keep"}, {"kind": "number", "id": "gap", "value": 80},
        {"kind": "list", "id": "rows", "rows": [], "selected": "7"}, {"kind": "button", "id": "go", "text": "Go"},
        {"kind": "tabs", "id": "pages", "tabs": [{"text": "A", "body": {"kind": "dropdown", "id": "glue", "options": ["a"], "value": "a"}}]}
    ]})"));
    EXPECT_EQ(values.dump(), json(R"({"filter": "", "keep": false, "gap": 80, "rows": "7", "pages": 0, "glue": "a"})").dump());
}
