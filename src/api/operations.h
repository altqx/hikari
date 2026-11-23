#pragma once

#include "hikari/api/api.h"
#include "hikari/api/base64.h"
#include "hikari/application/node_graph.h"

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
void addAutomationOperations(Registry &registry);
void addTranslationOperations(Registry &registry);
void addNodeOperations(Registry &registry);
void addEffectsOperations(Registry &registry);
void addPluginOperations(Registry &registry);
void addFormatOperations(Registry &registry);
void addTypesetOperations(Registry &registry);
void addProjectOperations(Registry &registry);
void addKaraokeOperations(Registry &registry);
void addMarkOperations(Registry &registry);
void addProblemOperations(Registry &registry);
void addVcsOperations(Registry &registry);
void addProgramOperations(Registry &registry);
void addPresenceOperations(Registry &registry);
void addEvents(Registry &registry);
Json projectJson(const Host &host);

void addOperation(Registry &registry, Access access, std::string name, std::string summary, std::string_view params,
                  std::string_view result, Handler handler, int since = 1);

struct Target {
    application::DocumentId id;
    application::EditSession *session = nullptr;
};
// The Document a call names, else the editing target; a caller without
// documents.all reaches the editing target and its attached Documents alone.
std::expected<Target, Error> target(const Context &context, const Json &params);
bool visible(const Context &context, application::DocumentId document);
bool sees(const Caller &caller, std::optional<application::DocumentId> editingTarget, application::DocumentId document);

bool writable(const Context &context, const Target &t);
std::unexpected<Error> unwritable(const Context &context, const Target &t);
std::unexpected<Error> refusal(application::CommandRefusal refusal, const application::EditSession &session);
Json problemsJson(const std::vector<application::GraphProblem> &problems);

std::string narrow(std::u8string_view s);
std::string lowerAscii(std::string_view s);
// document.apply: the edits as one undo step, recorded for review when the caller is reviewed.
Result documentApply(const Context &context, const Json &params);
std::unexpected<Error> noVideo();
// A plugin handler's context.via for this caller: "ai" or "api".
std::string_view viaOf(const Caller &caller);
// refused, reason disabled, with the reason commands.list gives the plugin command.
std::unexpected<Error> disabled(const PluginHost &plugins, std::string_view command);
bool videoOpen(const Context &context);
const core::LineRecord *findLine(const core::Document &document, core::LineId id);
std::unexpected<Error> noLine(core::LineId id);
std::unexpected<Error> cannotDraw();
std::unexpected<Error> notDrawn(std::int64_t frame);
// `params`' frame, else the frame shown when it lies in `record`'s span, else the span's first frame.
std::int64_t frameOf(const application::MacroMediaPort &media, const Json &params, const core::LineRecord *record);

Json selectionJson(const application::EditSession &session);
Json dialogValueJson(const application::DialogValue &value);
Json dialogsJson(const std::vector<DialogAnswer> &dialogs);
Json panelJson(const PanelState &panel);

}
