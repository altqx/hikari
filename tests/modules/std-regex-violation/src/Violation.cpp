#include <regex>

bool matches(const std::string &text)
{
    return std::regex_search(text, std::regex("a+"));
}
