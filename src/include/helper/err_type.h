#ifndef ERR_TYPE_H
#define ERR_TYPE_H

#include <stdexcept>
#include "backtrace.h"
#include "color.h"

class runtime_error final : public std::runtime_error
{
    std::string additional;
public:
    explicit runtime_error(const std::string& what_arg) : std::runtime_error(what_arg)
    {
        additional = color::color(5,0,0) + what_arg + color::no_color();
        if (const std::string bt = debug::backtrace(); !bt.empty())
        {
            additional += "\n";
            additional += bt;
        }
        else
        {
            additional += color::color(2,2,0) + "\nSet BACKTRACE_LEVEL=1 or 2 to see detailed backtrace information\n" + color::no_color();
        }
    }

    [[nodiscard]] const char* what() const noexcept override
    {
        return additional.c_str();
    }
};

#endif //ERR_TYPE_H
