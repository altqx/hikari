#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>

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
    if (mode == "sleep") {
        std::this_thread::sleep_for(std::chrono::hours(1));
        return 0;
    }
    return 1;
}
