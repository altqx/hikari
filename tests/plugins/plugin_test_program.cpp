#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace {

bool has(const std::string &line, const std::string &part)
{
    return line.find(part) != std::string::npos;
}

std::string idOf(const std::string &line)
{
    std::size_t at = line.find("\"id\":");
    if (at == std::string::npos)
        return {};
    at += 5;
    return line.substr(at, line.find_first_of(",}", at) - at);
}

void send(const std::string &message)
{
    std::cout << message << '\n' << std::flush;
}

int g_next = 100;

std::string callHikari(const std::string &method, const std::string &params)
{
    const std::string id = std::to_string(g_next++);
    send("{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"method\":\"" + method + "\",\"params\":" + params + "}");
    for (std::string line; std::getline(std::cin, line);)
        if (!has(line, "\"method\"") && idOf(line) == id)
            return line;
    return {};
}

void answer(const std::string &id, const std::string &result)
{
    send("{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":" + result + "}");
}

// The plugin protocol over JSON-RPC, scripted for program_plugin_tests; each answer HikariSub gives goes to stderr.
int rpc(const std::string &variant)
{
    for (std::string line; std::getline(std::cin, line);) {
        const std::string id = idOf(line);
        if (has(line, "\"method\":\"initialize\"")) {
            if (variant == "silent")
                continue;
            std::cerr << "init " << line << std::endl;
            answer(id, "{}");
            if (variant == "early")
                std::cerr << "early " << callHikari("hikari/notify", R"({"text":"too soon"})") << std::endl;
        } else if (has(line, "\"method\":\"export\"")) {
            std::cerr << "export " << line << std::endl;
            answer(id, has(line, "\"format\":\"bin\"") ? R"({"data":"AAEC","encoding":"base64"})" : R"({"data":"Line one\n"})");
        } else if (has(line, "\"method\":\"import\"")) {
            std::cerr << "import " << line << std::endl;
            answer(id, has(line, "\"format\":\"bad\"") ? "[]" : R"({"lines":[{"text":"Read"}],"styles":[],"info":{}})");
        } else if (has(line, "\"method\":\"complete\"")) {
            std::cerr << "complete " << line << std::endl;
            answer(id, R"([{"label":"\\fad","insert":"\\fad(","from":1,"to":2}])");
        } else if (has(line, "\"method\":\"hover\"")) {
            std::cerr << "hover " << line << std::endl;
            answer(id, has(line, "\"text\":\"\"") ? "\"no\"" : R"({"text":"Fade","from":0,"to":4})");
        } else if (has(line, "\"command\":\"hello\"")) {
            std::cerr << "hello " << line << std::endl;
            std::cerr << "notify " << callHikari("hikari/notify", R"({"text":"hi","level":"warning"})") << std::endl;
            std::cerr << "call " << callHikari("lines.list", "{}") << std::endl;
            std::cerr << "setting " << callHikari("hikari/settings.get", R"({"id":"name"})") << std::endl;
            answer(id, R"({"ok":true})");
        } else if (has(line, "\"command\":\"fail\"")) {
            send("{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"error\":{\"code\":-32000,\"message\":\"it broke\"}}");
        } else if (has(line, "\"command\":\"subscribe\"")) {
            std::cerr << "subscribe " << callHikari("hikari/subscribe", R"({"events":["document.changed"]})") << std::endl;
            answer(id, "null");
        } else if (has(line, "\"command\":\"edit\"")) {
            std::cerr << "begin " << callHikari("hikari/edit.begin", R"({"name":"Fix"})") << std::endl;
            std::cerr << "end " << callHikari("hikari/edit.end", "{}") << std::endl;
            answer(id, "null");
        } else if (has(line, "\"command\":\"bad\"")) {
            send("this is not JSON-RPC");
        } else if (has(line, "\"method\":\"shutdown\"")) {
            answer(id, "null");
        } else if (has(line, "\"method\":\"exit\"")) {
            return 0;
        } else if (!id.empty()) {
            std::cerr << "got " << line << std::endl;
            answer(id, "null");
        }
    }
    return 0;
}

std::string readFrame()
{
    std::size_t length = 0;
    bool any = false;
    for (std::string header; std::getline(std::cin, header);) {
        if (!header.empty() && header.back() == '\r')
            header.pop_back();
        if (header.empty()) {
            if (!any)
                continue;
            std::string body(length, '\0');
            std::cin.read(body.data(), std::streamsize(length));
            return std::cin ? body : std::string();
        }
        any = true;
        if (header.rfind("Content-Length:", 0) == 0)
            length = std::stoul(header.substr(15));
    }
    return {};
}

void sendFrame(const std::string &body)
{
    std::cout << "Content-Length: " << body.size() << "\r\n\r\n" << body << std::flush;
}

std::string stringAfter(const std::string &body, const std::string &key)
{
    std::size_t at = body.find("\"" + key + "\":\"");
    if (at == std::string::npos)
        return {};
    std::string out;
    for (at += key.size() + 4; at < body.size() && body[at] != '"'; ++at) {
        if (body[at] != '\\') {
            out += body[at];
            continue;
        }
        const char escaped = body[++at];
        out += escaped == 'n' ? '\n' : escaped == 't' ? '\t' : escaped;
    }
    return out;
}

std::string numberAfter(const std::string &body, const std::string &key)
{
    const std::size_t at = body.find("\"" + key + "\":");
    if (at == std::string::npos)
        return "null";
    const std::size_t from = at + key.size() + 3;
    return body.substr(from, body.find_first_of(",}", from) - from);
}

// Each "bad" as its LSP line and UTF-16 character.
std::vector<std::pair<int, int>> bads(const std::string &text)
{
    std::vector<std::pair<int, int>> out;
    int line = 0, units = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text.compare(i, 3, "bad") == 0)
            out.emplace_back(line, units);
        const auto byte = static_cast<unsigned char>(text[i]);
        if (byte == '\n') {
            ++line;
            units = 0;
        } else if (byte < 0x80 || byte >= 0xC0) {
            units += byte >= 0xF0 ? 2 : 1;
        }
    }
    return out;
}

std::string position(int line, int character)
{
    return "{\"character\":" + std::to_string(character) + ",\"line\":" + std::to_string(line) + "}";
}

std::string range(int line, int from, int toLine, int to)
{
    return "{\"end\":" + position(toLine, to) + ",\"start\":" + position(line, from) + "}";
}

std::string badDiagnostic(int line, int units)
{
    return "{\"code\":\"Bad\",\"message\":\"Avoid bad\",\"range\":" + range(line, units, line, units + 3) +
           ",\"severity\":2,\"source\":\"fake\"}";
}

std::string badDiagnostics(const std::string &text)
{
    std::string out;
    for (const auto &[line, units] : bads(text))
        out += std::string(out.empty() ? "" : ",") + badDiagnostic(line, units);
    return "[" + out + "]";
}

int intAfter(const std::string &body, const std::string &key, std::size_t from = 0)
{
    const std::size_t at = body.find("\"" + key + "\":", from);
    return at == std::string::npos ? -1 : std::atoi(body.c_str() + at + key.size() + 3);
}

std::string edit(const std::string &uri, const std::string &range, const std::string &text)
{
    return "{\"changes\":{\"" + uri + "\":[{\"newText\":\"" + text + "\",\"range\":" + range + "}]}}";
}

// For each "bad" on the asked line: a fix, then one across two lines, one into the line's first character (a tag
// when the line starts with one) and one that only runs a command.
std::string codeActions(const std::string &uri, const std::string &text, int line)
{
    std::string out;
    const auto add = [&out](const std::string &action) { out += std::string(out.empty() ? "" : ",") + action; };
    for (const auto &[at, units] : bads(text)) {
        if (at != line)
            continue;
        const std::string diagnostics = "[" + badDiagnostic(at, units) + "]";
        add("{\"diagnostics\":" + diagnostics + ",\"edit\":" + edit(uri, range(at, units, at, units + 3), "good") +
            ",\"isPreferred\":true,\"kind\":\"quickfix\",\"title\":\"Use good\"}");
        add("{\"diagnostics\":" + diagnostics + ",\"edit\":" + edit(uri, range(at, units, at + 1, 0), "") +
            ",\"kind\":\"quickfix\",\"title\":\"Across lines\"}");
        add("{\"diagnostics\":" + diagnostics + ",\"edit\":" + edit(uri, range(at, 0, at, 1), "x") +
            ",\"kind\":\"quickfix\",\"title\":\"Into the start\"}");
        add("{\"command\":{\"command\":\"fake.fix\",\"title\":\"Run\"},\"diagnostics\":" + diagnostics +
            ",\"kind\":\"quickfix\",\"title\":\"Run a command\"}");
    }
    return "[" + out + "]";
}

// A language server, scripted for language_servers_tests; what HikariSub sends goes to stderr.
int lsp(const std::string &variant)
{
    std::map<std::string, std::string> texts;
    for (std::string body = readFrame(); !body.empty(); body = readFrame()) {
        const std::string method = stringAfter(body, "method");
        const std::string id = idOf(body);
        if (method == "initialize") {
            std::cerr << "init " << body << std::endl;
            sendFrame("{\"jsonrpc\":\"2.0\",\"id\":" + id +
                      R"(,"result":{"capabilities":{"textDocumentSync":1,"hoverProvider":true,"codeActionProvider":true,)"
                      R"("completionProvider":{"triggerCharacters":["\\"]}}}})");
        } else if (method == "initialized") {
            sendFrame(R"({"jsonrpc":"2.0","id":"c1","method":"workspace/configuration","params":{"items":[{"section":"style"},{}]}})");
            sendFrame(R"({"jsonrpc":"2.0","method":"window/logMessage","params":{"type":3,"message":"ready"}})");
            sendFrame(R"({"jsonrpc":"2.0","method":"window/showMessage","params":{"type":1,"message":"look"}})");
        } else if (method.empty()) {
            std::cerr << "answer " << body << std::endl;
        } else if (method == "textDocument/didOpen" || method == "textDocument/didChange") {
            const std::string uri = stringAfter(body, "uri");
            const std::string version = numberAfter(body, "version");
            const std::string text = stringAfter(body, "text");
            texts[uri] = text;
            std::cerr << (method == "textDocument/didOpen" ? "open " : "change ") << uri << ' ' << version << ' ' << text
                      << std::endl;
            if (variant == "crash")
                return 3;
            sendFrame("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\"" + uri +
                      "\",\"version\":" + version + ",\"diagnostics\":" + badDiagnostics(text) + "}}");
        } else if (method == "textDocument/completion") {
            const std::size_t at = body.find("\"position\"");
            const int line = intAfter(body, "line", at), character = intAfter(body, "character", at);
            std::cerr << "complete " << stringAfter(body, "uri") << ' ' << line << ' ' << character << " '"
                      << stringAfter(body, "triggerCharacter") << "'" << std::endl;
            sendFrame("{\"jsonrpc\":\"2.0\",\"id\":" + id +
                      R"(,"result":{"isIncomplete":false,"items":[{"label":"zebra","sortText":"2","detail":"a word"},)"
                      R"j({"label":"\\fad","sortText":"1","insertTextFormat":2,"textEdit":{"newText":"\\fad(${1:200})","range":)j" +
                      range(line, character - 1, line, character) + R"(}},{"label":"far","textEdit":{"newText":"far","range":)" +
                      range(line + 1, 0, line + 1, 0) + "}}]}}");
        } else if (method == "textDocument/hover") {
            const std::size_t at = body.find("\"position\"");
            const int line = intAfter(body, "line", at), character = intAfter(body, "character", at);
            sendFrame("{\"jsonrpc\":\"2.0\",\"id\":" + id + R"(,"result":{"contents":{"kind":"markdown","value":"hover at )" +
                      std::to_string(character) + R"("},"range":)" + range(line, 0, line, 3) + "}}");
        } else if (method == "textDocument/codeAction") {
            const std::string uri = stringAfter(body, "uri");
            const int line = intAfter(body, "line", body.rfind("\"range\""));
            std::cerr << "actions " << uri << ' ' << line << std::endl;
            sendFrame("{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":" + codeActions(uri, texts[uri], line) + "}");
        } else if (method == "textDocument/didClose") {
            std::cerr << "close " << stringAfter(body, "uri") << std::endl;
        } else if (method == "shutdown") {
            std::cerr << "shutdown" << std::endl;
            sendFrame("{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":null}");
        } else if (method == "exit") {
            return 0;
        }
    }
    return 0;
}

}

int main(int argc, char **argv)
{
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "echo") {
        std::cout << std::string(std::istreambuf_iterator<char>(std::cin), {});
        std::cerr << "a warning\n";
        return 3;
    }
    if (mode == "args") {
        for (int i = 2; i < argc; ++i)
            std::cout << argv[i] << '\n';
        const char *temp = std::getenv("TEMP");
        std::cout << "TEMP=" << (temp ? temp : "") << '\n';
        std::cout << "SECRET=" << (std::getenv("HIKARI_TEST_SECRET") ? "set" : "unset") << '\n';
        return 0;
    }
    if (mode == "lines") {
        for (std::string line; std::getline(std::cin, line);)
            std::cout << "got " << line << std::endl;
        return 0;
    }
    if (mode == "read" && argc > 2) {
        std::ifstream file(argv[2], std::ios::binary);
        if (!file)
            return 2;
        std::cout << std::string(std::istreambuf_iterator<char>(file), {});
        return 0;
    }
    if (mode == "flood") {
        const std::string chunk(1 << 16, 'x');
        for (int i = 0; i < 17 * 16 + 1; ++i)
            std::cout << chunk;
        return 0;
    }
    if (mode == "noisy") {
        for (int i = 0; i < 30; ++i)
            std::cerr << "line " << i << '\n';
        return 0;
    }
    if (mode == "rpc")
        return rpc(argc > 2 ? argv[2] : "");
    if (mode == "lsp")
        return lsp(argc > 2 ? argv[2] : "");
    if (mode == "sleep") {
        std::this_thread::sleep_for(std::chrono::hours(1));
        return 0;
    }
    return 1;
}
