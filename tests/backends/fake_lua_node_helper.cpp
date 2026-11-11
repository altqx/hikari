#include "hikari/backends/helper_endpoint.h"
#include "hikari/backends/lua_protocol.h"

#include <chrono>
#include <cstdlib>
#include <thread>

using namespace hikari::backends;
using namespace hikari::backends::helper;

// The node's code picks what it does: "hang", "crash", "fail", "rows" (each output is the batch's
// row count), "slow" (echoes after 200 ms) or anything else (each output repeats the input at its index).
int main()
{
    return runHelper(lua::kHelperName, lua::kProtocolVersion, [](const Frame &request, Responder &r) {
        Reader in(request.payload);
        if (static_cast<lua::Command>(in.i32()) != lua::Command::RunNodes)
            return r.terminal(Outcome::Unsupported);
        const auto call = lua::decodeNodeCall(in, request.payload.size());
        if (!call)
            return r.terminal(Outcome::InvalidInput);
        if (call->code == "hang")
            for (;;)
                std::this_thread::sleep_for(std::chrono::seconds(1));
        if (call->code == "crash")
            std::_Exit(3);
        if (call->code == "slow")
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (call->code == "fail")
            return r.terminal(Outcome::Failed, bytesOf("code:1: fake failure"));
        lua::NodeRows rows;
        for (const auto &row : call->rows) {
            std::vector<hikari::application::NodeValue> out;
            for (std::size_t k = 0; k < call->outputs.size(); ++k)
                out.push_back(call->code == "rows" ? hikari::application::NodeValue(double(call->rows.size())) : row[k]);
            rows.push_back(std::move(out));
        }
        r.terminal(Outcome::Ok, lua::encodeNodeRows(rows, call->outputs));
    });
}
