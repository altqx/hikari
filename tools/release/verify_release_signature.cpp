// The app's release-signature check, for the release job:
//
//   verify-release-signature <file> <file.minisig> <tag> [<public key file>]
//
// Without a public key file it trusts the keys the app ships. Exits 0 when the signature is accepted.

#include "hikari/application/update_signature.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::optional<std::string> readFile(const char *path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return std::nullopt;
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

}

int main(int argc, char **argv)
{
    using namespace hikari;
    if (argc != 4 && argc != 5) {
        std::fprintf(stderr, "usage: %s <file> <file.minisig> <tag> [<public key file>]\n", argv[0]);
        return 2;
    }
    const auto bytes = readFile(argv[1]);
    if (!bytes) {
        std::fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }
    std::vector<minisign::PublicKey> keys = application::releaseKeys();
    if (argc == 5) {
        const auto text = readFile(argv[4]);
        const auto key = text ? minisign::parsePublicKey(*text) : std::nullopt;
        if (!key) {
            std::fprintf(stderr, "%s is not a minisign public key\n", argv[4]);
            return 2;
        }
        keys = {*key};
    }
    const std::string file = std::filesystem::path(argv[1]).filename().string();
    const auto problem = application::releaseSignatureProblem(*bytes, readFile(argv[2]), argv[3], file, keys);
    if (problem) {
        const auto text = application::signatureProblemText(*problem);
        std::fprintf(stderr, "%s: %.*s\n", file.c_str(), int(text.size()), text.data());
        return 1;
    }
    std::printf("%s: signed for %s\n", file.c_str(), argv[3]);
    return 0;
}
