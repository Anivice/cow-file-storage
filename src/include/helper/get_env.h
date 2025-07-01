#ifndef GET_ENV_H
#define GET_ENV_H

// env variables
#define BACKTRACE_LEVEL "BACKTRACE_LEVEL"
#define COLOR           "COLOR"
#define TRIM_SYMBOL     "TRIM_SYMBOL"

#include <string>
#include <functional>

std::string get_env(const std::string & name);
bool true_false_helper(std::string val);
std::string replace_all(std::string & original, const std::string & target, const std::string & replacement);
std::string regex_replace_all(std::string & original, const std::string & pattern, const std::function<std::string(const std::string &)>& replacement);

template <typename IntType>
IntType get_variable(const std::string & name)
{
    const auto var = get_env(name);
    if (var.empty())
    {
        return 0;
    }

    return static_cast<IntType>(strtoll(var.c_str(), nullptr, 10));
}

#endif //GET_ENV_H
