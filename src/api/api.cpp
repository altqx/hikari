#include "operations.h"

#include "hikari/api/schema.h"

#include <cstdio>
#include <cstdlib>

namespace hikari::api {

std::string_view accessName(Access access)
{
    return access == Access::Read ? "read" : "write";
}

std::unexpected<Error> fail(std::string_view code, std::string message, Json data)
{
    return std::unexpected(Error{std::string(code), std::move(message), std::move(data)});
}

bool Registry::add(Operation operation)
{
    if (find(operation.name))
        return false;
    m_operations.push_back(std::move(operation));
    return true;
}

bool Registry::addEvent(EventType event)
{
    if (findEvent(event.name))
        return false;
    m_events.push_back(std::move(event));
    return true;
}

const EventType *Registry::findEvent(std::string_view name) const
{
    for (const EventType &event : m_events)
        if (event.name == name)
            return &event;
    return nullptr;
}

const Operation *Registry::find(std::string_view name) const
{
    for (const Operation &operation : m_operations)
        if (operation.name == name)
            return &operation;
    return nullptr;
}

namespace detail {

Json schema(std::string_view text)
{
    auto parsed = Json::parse(text);
    if (!parsed) {
        std::fprintf(stderr, "hikari_api: bad schema at %zu: %s\n", parsed.error().offset, parsed.error().message.c_str());
        std::abort();
    }
    return std::move(*parsed);
}

Handler now(SyncHandler handler)
{
    return [handler = std::move(handler)](const Context &context, const Json &params, Reply reply) {
        reply(handler(context, params));
    };
}

}

const Registry &Api::registry()
{
    static const Registry registry = [] {
        Registry r;
        detail::addDefinitions(r);
        detail::addDocumentOperations(r);
        detail::addApplicationOperations(r);
        detail::addWriteOperations(r);
        detail::addMediaOperations(r);
        detail::addFileOperations(r);
        detail::addSkillOperations(r);
        detail::addNodeOperations(r);
        detail::addTypesetOperations(r);
        detail::addEvents(r);
        return r;
    }();
    return registry;
}

void Api::call(const Caller &caller, std::string_view name, const Json &params, Reply reply)
{
    const Registry &operations = registry();
    const Operation *operation = operations.find(name);
    if (!operation) {
        reply(fail(errors::kUnknownOperation, "No operation is named \"" + std::string(name) + "\".",
                   Json::object({{"operation", Json(name)}})));
        return;
    }
    const Json given = params.isNull() ? Json::object() : params;
    if (auto failure = validate(operation->params, given, operations.defs())) {
        reply(fail(errors::kInvalidParams, "Parameter " + (failure->path.empty() ? "object" : failure->path) + ": " +
                                                failure->message + ".",
                   Json::object({{"pointer", Json(failure->path)}})));
        return;
    }
    std::vector<GrantedFile> files;
    for (const auto &[callerId, file] : m_grants)
        if (callerId == caller.id)
            files.push_back(file);
    const Context context{m_host, caller, std::move(files)};
    if (operation->access == Access::Read) {
        operation->handler(context, given, std::move(reply));
        return;
    }
    if (m_writing) {
        reply(fail(errors::kBusy, "Another write is applying; try again after it.", Json::object()));
        return;
    }
    m_writing = true;
    operation->handler(context, given, [this, reply = std::move(reply)](Result result) {
        m_writing = false;
        reply(std::move(result));
    });
}

std::string Api::grantFile(const std::string &callerId, const std::string &path, bool writable)
{
    for (auto &[id, file] : m_grants)
        if (id == callerId && file.path == path) {
            file.writable = writable;
            return file.handle;
        }
    const std::size_t slash = path.find_last_of("/\\");
    GrantedFile file{"f" + std::to_string(m_nextGrant++), slash == std::string::npos ? path : path.substr(slash + 1),
                     path, writable};
    m_grants.emplace_back(callerId, file);
    return file.handle;
}

void Api::revokeFiles(const std::string &callerId)
{
    std::erase_if(m_grants, [&](const auto &grant) { return grant.first == callerId; });
}

std::optional<std::string> Api::grantedPath(const std::string &callerId, std::string_view handle) const
{
    for (const auto &[id, file] : m_grants)
        if (id == callerId && file.handle == handle)
            return file.path;
    return std::nullopt;
}

}
