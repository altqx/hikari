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
void addMediaOperations(Registry &registry);
void addFileOperations(Registry &registry);
void addSkillOperations(Registry &registry);
void addNodeOperations(Registry &registry);
void addEvents(Registry &registry);

void addOperation(Registry &registry, Access access, std::string name, std::string summary, std::string_view params,
                  std::string_view result, Handler handler);

std::string base64(std::string_view bytes);
// nullopt: not base64.
std::optional<std::string> fromBase64(std::string_view text);

struct Target {
    application::DocumentId id;
    application::EditSession *session = nullptr;
};
// The Document a call names, else the editing target; a caller without
// documents.all reaches the editing target and its attached Documents alone.
std::expected<Target, Error> target(const Context &context, const Json &params);
bool visible(const Context &context, application::DocumentId document);
bool sees(const Caller &caller, std::optional<application::DocumentId> editingTarget, application::DocumentId document);

std::string narrow(std::u8string_view s);
Json selectionJson(const application::EditSession &session);
Json panelJson(const PanelState &panel);

}
