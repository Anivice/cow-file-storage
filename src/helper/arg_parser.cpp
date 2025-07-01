#include <algorithm>
#include <map>
#include <stdexcept>
#include "helper/err_type.h"
#include "helper/cpp_assert.h"
#include "helper/arg_parser.h"

arg_parser::arg_parser(const int argc, char ** argv, const parameter_vector & parameters)
{
    auto contains = [&parameters](const std::string & name)->bool
    {
        return std::ranges::any_of(parameters, [&name](const parameter_t & p)->bool
        {
            return (p.name == name || (name.size() == 1 && p.short_name == name[0]));
        });
    };

    auto get_short_name = [](const std::string & name)->char
    {
        if (name.size() == 2 && name[0] == '-' && name[1] != '-')
        {
            return name[1];
        }

        return '\0';
    };

    auto get_long_name = [](const std::string & name)->std::string
    {
        if (name.size() > 2 && name[0] == '-' && name[1] == '-')
        {
            return name.substr(2);
        }

        return "";
    };

    auto is_param = [](const std::string & name)->bool
    {
        return name.size() > 1 && name[0] == '-';
    };

    auto find = [&parameters](const std::string & name)->parameter_t
    {
        const auto it =
            std::ranges::find_if(parameters, [&name](const parameter_t & p)->bool
            {
                return (p.name == name || (name.size() == 1 && p.short_name == name[0]));
            });
        if (it == parameters.end())
        {
            throw std::runtime_error("Unknown parameter: " + name);
        }

        return *it;
    };

    // sanity check
    std::vector < std::string > dup1;
    std::vector < char > dup2;
    dup1.reserve(parameters.size());
    dup2.reserve(parameters.size());
    for (const auto & p : parameters)
    {
        assert_throw(!p.name.empty(), "Full name cannot be empty");
        assert_throw(std::ranges::find(dup1, p.name) == std::ranges::end(dup1), "Duplicated argument in initialization list");
        dup1.push_back(p.name);
        assert_throw(p.short_name == 0 ? true : (std::ranges::find(dup2, p.short_name) == std::ranges::end(dup2)),
            "Duplicated short name in initialization list");
        if (p.short_name != 0) dup2.push_back(p.short_name);
    }

    std::string current_arg;
    std::vector<std::string> bare;
    std::map <std::string, std::string> non_bare_args;
    for (int i = 1; i < argc; ++i)
    {
        if (const std::string arg = argv[i];
            is_param(arg) && bare.empty())
        {
            if (current_arg.empty())
            {
                const char short_name = get_short_name(arg);
                const std::string long_name = get_long_name(arg);
                const std::string name = short_name != '\0' ? std::string(1, short_name) : long_name;
                assert_throw(contains(name), "Unknown parameter: " + arg);
                const auto param_info = find(name);
                if (param_info.arg_required) {
                    current_arg = param_info.name;
                } else {
                    non_bare_args.emplace(param_info.name, "");
                }
            }
            else
            {
                throw runtime_error("Parameter `" + current_arg + "` needs an argument");
            }
        }
        else if (current_arg.empty())
        {
            bare.push_back(arg);
        }
        else
        {
            non_bare_args.emplace(current_arg, arg);
            current_arg.clear();
        }
    }

    if (!current_arg.empty() && find(current_arg).arg_required)
    {
        throw runtime_error("Parameter `" + current_arg + "` needs an argument");
    }

    for (const auto & [name, value] : non_bare_args)
    {
        args.emplace_back(name, value);
    }

    for (const auto & arg : bare)
    {
        args.emplace_back("", arg);
    }
}
