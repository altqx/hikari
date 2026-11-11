#include "hikari/api/json.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

using hikari::api::Json;

namespace {

void print(const std::string &line)
{
    std::fputs((line + "\n").c_str(), stdout);
    std::fflush(stdout);
}

std::string env(const char *name)
{
    const char *value = std::getenv(name);
    return value ? value : "";
}

}

// Reports its arguments and three variables, then echoes each line; a message whose params hold `emit` prints that
// instead, and one whose params hold `exit` ends with that code.
int main(int argc, char **argv)
{
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    Json args = Json::array();
    for (int i = 1; i < argc; ++i)
        args.push(std::string(argv[i]));
    print(Json::object({{"started", Json::object({{"args", std::move(args)},
                                                  {"entry", env("CLAUDE_CODE_ENTRYPOINT")},
                                                  {"secret", env("HIKARI_TEST_SECRET")},
                                                  {"mcpToken", env("HIKARI_MCP_TOKEN")}})}})
              .dump());
    std::string line;
    while (std::getline(std::cin, line)) {
        const auto parsed = Json::parse(line);
        const Json *params = parsed ? parsed->find("params") : nullptr;
        if (const Json *emit = params ? params->find("emit") : nullptr)
            print(emit->dump());
        else if (const Json *code = params ? params->find("exit") : nullptr)
            return static_cast<int>(code->asInt());
        else
            print(line);
    }
    return 0;
}
