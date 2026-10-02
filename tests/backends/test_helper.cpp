// A helper process for the H1 tests. Requests are text commands:
//   echo:<text>   reply with <text>, then Ok
//   progress      three progress events, then Ok
//   crash         exit abruptly mid-request
//   wait          loop until cancelled, then Cancelled
//   duplicate     two Terminal frames (the second must be rejected by the host)
// "--version N" announces protocol version N instead of 1.

#include "hikari/backends/helper_endpoint.h"

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

using namespace hikari::backends::helper;

int main(int argc, char **argv)
{
    std::uint32_t version = 1;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--version")
            version = static_cast<std::uint32_t>(std::stoul(argv[i + 1]));
    return runHelper("test-helper", version, [](const Frame &request, Responder &r) {
        const std::string command = textOf(request.payload);
        if (command.starts_with("echo:")) {
            r.reply(bytesOf(command.substr(5)));
            r.terminal(Outcome::Ok);
        } else if (command == "progress") {
            for (int i = 1; i <= 3; ++i)
                r.progress(bytesOf(std::to_string(i)));
            r.terminal(Outcome::Ok);
        } else if (command == "crash") {
            std::_Exit(42);
        } else if (command == "wait") {
            while (!r.cancelled())
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            r.terminal(Outcome::Cancelled);
        } else if (command == "duplicate") {
            r.terminal(Outcome::Ok);
            // Bypass the responder's guard: a misbehaving helper.
            const auto bytes = encode(Frame{Kind::Terminal, 0, request.session, request.run, request.request, {}});
            std::fwrite(bytes.data(), 1, bytes.size(), stdout);
            std::fflush(stdout);
        } else {
            r.terminal(Outcome::Unsupported);
        }
    });
}
