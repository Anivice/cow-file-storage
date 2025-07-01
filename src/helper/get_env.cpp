#include <cstdlib>
#include <algorithm>
#include <regex>
#include "helper/get_env.h"

std::string get_env(const std::string & name)
{
    const char * ptr = std::getenv(name.c_str());
    if (ptr == nullptr)
    {
        return "";
    }

    return ptr;
}

bool true_false_helper(std::string val)
{
    std::ranges::transform(val, val.begin(), ::tolower);
    if (val == "true") {
        return true;
    } else if (val == "false") {
        return false;
    } else {
        try {
            return std::stoi(val) != 0;
        } catch (...) {
            return false;
        }
    }
}

std::string replace_all(
    std::string & original,
    const std::string & target,
    const std::string & replacement)
{
    if (target.empty()) return original; // Avoid infinite loop if target is empty

    if (target.size() == 1 && replacement.empty()) {
        std::erase_if(original, [&target](const char c) { return c == target[0]; });
        return original;
    }

    size_t pos = 0;
    while ((pos = original.find(target, pos)) != std::string::npos) {
        original.replace(pos, target.length(), replacement);
        pos += replacement.length(); // Move past the replacement to avoid infinite loop
    }
    return original;
}

std::string regex_replace_all(std::string & original, const std::string & pattern, const std::function<std::string(const std::string &)>& replacement)
{
    std::vector < std::string > replace_list;
    const std::regex pattern_rgx(pattern);
    const auto matches_begin = std::sregex_iterator(begin(original), end(original), pattern_rgx);
    const auto matches_end = std::sregex_iterator();
    for (std::sregex_iterator i = matches_begin; i != matches_end; ++i)
    {
        const auto match = i->str();
        replace_list.emplace_back(match);
    }

    for (const auto & word : replace_list)
    {
        replace_all(original, word, replacement(word));
    }

    return original;
}
