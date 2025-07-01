#ifndef COLOR_H
#define COLOR_H

#include <atomic>
#include <string>

namespace color {
    extern std::atomic_bool g_no_color;
    std::string no_color();
    std::string color(int r, int g, int b, int br, int bg, int bb);
    std::string color(int r, int g, int b);
    std::string bg_color(int r, int g, int b);
}

#endif //COLOR_H
