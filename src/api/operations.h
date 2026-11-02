#pragma once

#include "hikari/api/api.h"

#include <string_view>

namespace hikari::api::detail {

// Aborts on malformed text: schemas are compiled-in literals the tests check.
Json schema(std::string_view text);

// A handler that answers before it returns.
using SyncHandler = std::function<Result(const Context &, const Json &params)>;
Handler now(SyncHandler handler);

void addDefinitions(Registry &registry);
void addDocumentOperations(Registry &registry);
void addApplicationOperations(Registry &registry);
void addWriteOperations(Registry &registry);
void addEvents(Registry &registry);

struct Target {
    application::DocumentId id;
    application::EditSession *session = nullptr;
};
// The Document a call names, else the editing target; a caller without
// documents.all reaches the editing target alone.
std::expected<Target, Error> target(const Context &context, const Json &params);
bool visible(const Context &context, application::DocumentId document);

std::string narrow(std::u8string_view s);
Json selectionJson(const application::EditSession &session);
Json panelJson(const PanelState &panel);

}
