#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application::blocks {

using u8s = std::u8string;
using u8v = std::u8string_view;
using Values = std::vector<double>;

struct Tag {
    u8s name;
    u8s value;
};

struct Item {
    std::optional<Tag> tag;
    u8s raw;
};

struct Piece {
    bool block = false;
    bool emptied = false;
    u8s text;
    std::vector<Item> items;
};

std::string narrow(u8v v);
u8s wide(const std::string &s);
double number(u8v v);

std::vector<Item> parseTags(u8v content);
std::vector<Piece> parse(u8v text);
u8s serialize(const std::vector<Piece> &pieces);

enum class Kind { Number, Colour, Alpha, Rect, Other };

u8s canonical(u8v name);
Kind kindOf(u8v canonicalName);
std::vector<u8s> arguments(u8v value);
int hexValue(u8v value);
std::optional<Values> parseValue(Kind kind, u8v value);
std::string fixed(double v);
int channel(double v);
u8s format(Kind kind, const Values &v);

}
