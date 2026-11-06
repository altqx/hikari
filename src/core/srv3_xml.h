#pragma once

#include "srv3_net.h"

#include <vector>

namespace hikari::core::srv3::xml {

struct Node {
    enum class Kind { Element, Text, CData, Whitespace };

    Kind kind = Kind::Element;
    u16 name;
    u16 value;
    bool noNamespace = true;
    std::vector<std::pair<u16, u16>> attributes;
    std::vector<Node> children;

    u16v localName() const;
    const u16 *attribute(u16v qualifiedName) const;
    u16 innerText() const;
    const Node *child(u16v noNamespaceName) const;
};

// XmlDocument.Load with PreserveWhitespace: the root element. Document type declarations are refused.
Node parse(u16v text);

}
