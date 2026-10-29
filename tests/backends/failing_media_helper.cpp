#include "hikari/backends/helper_endpoint.h"
#include "hikari/backends/media_protocol.h"

#include <cstdint>
#include <string>

using namespace hikari::backends;
using namespace hikari::backends::helper;

int main()
{
    return runHelper(media::kHelperName, media::kProtocolVersion, [](const Frame &request, Responder &r) {
        Reader in(request.payload);
        if (static_cast<media::Command>(in.u8()) != media::Command::Open)
            return r.terminal(Outcome::Unsupported, bytesOf("unsupported"));
        std::string name = in.str();
        if (const auto slash = name.find_last_of("/\\"); slash != std::string::npos)
            name.erase(0, slash + 1);
        name = name.substr(0, name.find('.'));
        const struct {
            const char *name;
            Outcome outcome;
            std::uint8_t stage;
        } stages[] = {{"indexer", Outcome::InvalidInput, 0},
                      {"indexing", Outcome::Failed, 1},
                      {"source", Outcome::Failed, 2},
                      {"convert", Outcome::Failed, 3}};
        for (const auto &s : stages)
            if (name == s.name)
                return r.terminal(s.outcome,
                                  Writer().u8(s.stage).str("fake " + std::string(s.name) + " error").take());
        r.terminal(Outcome::Failed, Writer().u8(5).str("no such stage").take());
    });
}
