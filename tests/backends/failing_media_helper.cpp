// V3: a stand-in media helper whose Open fails at the stage its file's name
// gives, with protocol 8's stage and text (media_protocol.h), so the stages
// FFMS2 rarely reaches from a real file (legacy ProviderFFMS2::Init's
// "Indexing error occurred: %s", "Cannot create VideoSource.", "Cannot
// convert video to RGBA", ProviderFFMS2.cpp:164-388) can be tested end to end:
//   indexer.*   InvalidInput at the indexer (FFMS_CreateIndexer)
//   indexing.*  Failed while indexing (FFMS_DoIndexing2)
//   source.*    Failed making the video source (FFMS_CreateVideoSource)
//   convert.*   Failed setting the output format (FFMS_SetOutputFormatV2)
// The text is "fake <stage> error". Every other request is Unsupported.

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
        // the helper's Stage numbering (media_helper.cpp)
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
