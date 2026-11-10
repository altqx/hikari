#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

namespace {

struct State {
    std::string version;
    std::string action;
};

State read(const std::string &file)
{
    State state;
    std::ifstream in(file);
    std::getline(in, state.version);
    std::getline(in, state.action);
    return state;
}

}

int main(int argc, char **argv)
{
    const char *file = std::getenv("HIKARI_FAKE_CLI_STATE");
    if (!file || argc < 2)
        return 64;
    State state = read(file);
    const std::string command = argv[1];
    if (command == "--version") {
        std::cout << state.version << " (fake agent)" << std::endl;
        return 0;
    }
    if (command != "update")
        return 64;
    std::cout << "Checking for updates" << std::endl;
    if (state.action.rfind("bump ", 0) == 0) {
        std::ofstream(file) << state.action.substr(5) << "\nsame\n";
        std::cout << "Updated to " << state.action.substr(5) << std::endl;
        return 0;
    }
    if (state.action == "fail") {
        std::cout << "error: the download failed" << std::endl;
        return 1;
    }
    if (state.action == "hang")
        std::this_thread::sleep_for(std::chrono::seconds(60));
    std::cout << "Up to date" << std::endl;
    return 0;
}
