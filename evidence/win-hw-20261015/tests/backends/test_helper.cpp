// A helper process for the H1 tests. Requests are text commands:
//   echo:<text>   reply with <text>, then Ok
//   progress      three progress events, then Ok
//   crash         exit abruptly mid-request
//   wait          loop until cancelled, then Cancelled
//   duplicate     two Terminal frames (the second must be rejected by the host)
//   service:<x>   a synchronous host call with <x>; reply with the answer, then
//                 Ok, or end with the call's failure outcome
//   noisy         write junk to stdout and stderr, then Ok (the protocol must
//                 survive; the junk lands in the host's diagnostics)
// "--version N" announces protocol version N instead of 1.

#include "hikari/backends/helper_endpoint.h"

#include <chrono>
#include <cstdio>
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
            sendUncheckedFrame(Frame{Kind::Terminal, 0, request.session, request.run, request.request, {}});
        } else if (command.starts_with("service:")) {
            const auto answer = r.call(bytesOf(command.substr(8)));
            if (!answer) {
                r.terminal(answer.error());
                return;
            }
            r.reply(*answer);
            r.terminal(Outcome::Ok);
        } else if (command == "noisy") {
            std::printf("HKRI junk on stdout\n");
            std::fflush(stdout);
            std::fprintf(stderr, "junk on stderr\n");
            r.terminal(Outcome::Ok);
        } else {
            r.terminal(Outcome::Unsupported);
        }
    });
}
