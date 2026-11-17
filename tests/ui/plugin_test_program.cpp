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

int main(int argc, char **argv)
{
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
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
    if (mode == "sleep") {
        std::this_thread::sleep_for(std::chrono::hours(1));
        return 0;
    }
    return 1;
}
