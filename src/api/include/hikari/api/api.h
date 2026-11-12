#pragma once

#include "hikari/api/host.h"
#include "hikari/api/json.h"

#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::api {

// Grows by one with each release that adds to the API.
inline constexpr int kApiVersion = 1;

enum class Access { Read, Write };
std::string_view accessName(Access access);

namespace errors {
inline constexpr std::string_view kUnknownOperation = "unknown_operation";
inline constexpr std::string_view kInvalidParams = "invalid_params";
inline constexpr std::string_view kNotFound = "not_found";
inline constexpr std::string_view kNotGranted = "not_granted";
inline constexpr std::string_view kUnavailable = "unavailable";
inline constexpr std::string_view kStaleRevision = "stale_revision";
inline constexpr std::string_view kReadOnly = "read_only";
inline constexpr std::string_view kProtected = "protected";
inline constexpr std::string_view kRefused = "refused";
inline constexpr std::string_view kBusy = "busy";
}

struct Error {
    std::string code;
    std::string message;
    Json data;
};
using Result = std::expected<Json, Error>;
using Reply = std::function<void(Result)>;
std::unexpected<Error> fail(std::string_view code, std::string message, Json data = {});

struct Caller {
    std::string id;
    std::string label; // names the caller's undo steps
    // Without it only the editing target and the attached Documents are visible.
    bool allDocuments = false;
    // The Documents the user attached with @, besides the editing target.
    std::vector<application::DocumentId> attached;
    // An AI caller: its document.apply changes go to the review, and privacy.* and recent.* settings stay hidden.
    bool reviewed = false;
    // May read the user's skills (AI callers).
    bool skills = false;
    // The caller's Project folder, UTF-8; its .hikari/skills join the user's. Empty: no Project.
    std::string project;
    // The user approved this call's Lua script; automation.run refuses without it.
    bool scriptApproved = false;
};

// A file the user let one caller use; the caller sees the handle and name, never the path.
struct GrantedFile {
    std::string handle;
    std::string name;
    std::string path;
    bool writable = false;
};

// A folder one caller may use by relative paths; links that lead outside it are refused.
struct GrantedFolder {
    std::string handle;
    std::string name;
    std::string path;
    bool writable = false;
};

// The handle of an AI caller's Project folder, from Caller::project. Its writes stage in the review (Q473).
inline constexpr std::string_view kProjectFolder = "project";

struct Context {
    const Host &host;
    const Caller &caller;
    std::vector<GrantedFile> files; // the caller's grants
    std::vector<GrantedFolder> folders;
    // Grants the caller a file inside one of its folders and answers the handle.
    std::function<std::string(const std::string &path, bool writable)> grantFile;
};

// A handler replies exactly once, now or later, on the GUI thread.
using Handler = std::function<void(const Context &, const Json &params, Reply)>;

struct Operation {
    std::string name;
    Access access = Access::Read;
    std::string summary;
    Json params;
    Json result;
    int since = 1;
    std::optional<int> deprecatedIn;
    Handler handler;
};

struct EventType {
    std::string name;
    std::string summary;
    Json data;
    int since = 1;
};

struct Event {
    std::string name;
    Json data;
};
using Listener = std::function<void(const Event &)>;

class Registry {
public:
    // Names are unique; a second add of a name is ignored and returns false.
    bool add(Operation operation);
    const Operation *find(std::string_view name) const;
    const std::vector<Operation> &operations() const { return m_operations; }
    bool addEvent(EventType event);
    const EventType *findEvent(std::string_view name) const;
    const std::vector<EventType> &events() const { return m_events; }
    // What "$ref": "#/$defs/<name>" resolves into, for every schema here.
    const Json &defs() const { return m_defs; }
    void define(std::string_view name, Json schema) { m_defs[name] = std::move(schema); }

private:
    std::vector<Operation> m_operations;
    std::vector<EventType> m_events;
    Json m_defs = Json::object();
};

class Api {
public:
    explicit Api(Host host) : m_host(host) {}

    // Every operation the API offers.
    static const Registry &registry();

    // Params are validated against the operation's schema before it runs;
    // null counts as {}. A write that arrives while another write runs,
    // say from a listener, is refused as busy.
    void call(const Caller &caller, std::string_view operation, const Json &params, Reply reply);

    // `events` empty: every event. 0 when a name is unknown. The caller's
    // scope applies: without documents.all only the editing target's events come.
    std::uint64_t subscribe(const Caller &caller, std::vector<std::string> events, Listener listener);
    void unsubscribe(std::uint64_t subscription);

    // The host reports each change once, after it happened, whoever made it.
    void documentChanged(application::DocumentId document); // a commit, Undo or Redo
    void selectionChanged(application::DocumentId document);
    void documentsChanged(); // a tab opened or closed, or the editing target moved
    void panelChanged(std::string_view panel);
    void commandRan(std::string_view command, bool ok);
    void projectChanged(); // the Project that project.info answers changed

    // The handle files.* use for `path`; granting a path again updates `writable`.
    std::string grantFile(const std::string &callerId, const std::string &path, bool writable);
    // The same for a folder, which files.* reach by `folder` and a relative `path`.
    std::string grantFolder(const std::string &callerId, const std::string &path, bool writable);
    // Its files and its folders.
    void revokeFiles(const std::string &callerId);
    // The path behind a caller's handle, if the caller holds it.
    std::optional<std::string> grantedPath(const std::string &callerId, std::string_view handle) const;
    // The file a files.write would change, from its file handle or its folder and path; nullopt: neither is granted.
    std::optional<std::string> writeTarget(const Caller &caller, const Json &params) const;

private:
    struct Subscription {
        std::uint64_t id = 0;
        Caller caller;
        std::vector<std::string> events;
        Listener listener;
    };
    // `document`: only callers that see it hear the event. `narrowed`: what
    // callers without documents.all hear instead.
    void publish(const Event &event, std::optional<application::DocumentId> document,
                 const std::function<Event(const Caller &)> &narrowed = {});

    Host m_host;
    bool m_writing = false;
    std::vector<Subscription> m_subscriptions;
    std::uint64_t m_nextSubscription = 1;
    std::vector<std::pair<std::string, GrantedFile>> m_grants; // by caller id
    std::vector<std::pair<std::string, GrantedFolder>> m_folders; // by caller id
    std::uint64_t m_nextGrant = 1;
};

}
