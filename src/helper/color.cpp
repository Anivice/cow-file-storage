#include <string>
#include <algorithm>
#include <sys/stat.h>
#include <unistd.h>
#include <atomic>
#include "helper/cpp_assert.h"
#include "helper/get_env.h"
#include "helper/color.h"

std::atomic_bool color::g_no_color;

bool is_no_color()
{
    static int is_no_color_cache = -1;
    if (is_no_color_cache != -1) {
        return is_no_color_cache;
    }

    auto color_env = get_env(COLOR);
    std::ranges::transform(color_env, color_env.begin(), ::tolower);

    if (color_env == "always")
    {
        is_no_color_cache = 0;
        return false;
    }

    const bool no_color_from_env = color_env == "never" || color_env == "none" || color_env == "off"
                            || color_env == "no" || color_env == "n" || color_env == "0" || color_env == "false";
    bool is_terminal = false;
    struct stat st{};
    assert_short(fstat(STDOUT_FILENO, &st) != -1);
    if (isatty(STDOUT_FILENO))
    {
        is_terminal = true;
    }

    is_no_color_cache = no_color_from_env || !is_terminal || color::g_no_color;
    return is_no_color_cache;
}

std::string color::no_color()
{
    if (!is_no_color())
    {
        return "\033[0m";
    }

    return "";
}

std::string color::color(const int r, const int g, const int b, const int br, const int bg, const int bb)
{
    if (is_no_color())
    {
        return "";
    }

    return color(r, g, b) + bg_color(br, bg, bb);
}

std::string color::color(int r, int g, int b)
{
    if (is_no_color())
    {
        return "";
    }

    auto constrain = [](int var, int min, int max)->int
    {
        var = std::max(var, min);
        var = std::min(var, max);
        return var;
    };

    r = constrain(r, 0, 5);
    g = constrain(g, 0, 5);
    b = constrain(b, 0, 5);

    const int scale = 16 + 36 * r + 6 * g + b;
    return "\033[38;5;" + std::to_string(scale) + "m";
}

std::string color::bg_color(int r, int g, int b)
{
    if (is_no_color())
    {
        return "";
    }

    auto constrain = [](int var, int min, int max)->int
    {
        var = std::max(var, min);
        var = std::min(var, max);
        return var;
    };

    r = constrain(r, 0, 5);
    g = constrain(g, 0, 5);
    b = constrain(b, 0, 5);

    const int scale = 16 + 36 * r + 6 * g + b;
    return "\033[48;5;" + std::to_string(scale) + "m";
}
