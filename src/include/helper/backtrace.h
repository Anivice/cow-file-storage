#ifndef BACKTRACE_H
#define BACKTRACE_H

#include <string>
#include <atomic>

namespace debug {
    std::string backtrace();
    extern std::atomic_int g_pre_defined_level;
    extern std::atomic_bool g_trim_symbol;
    bool true_false_helper(std::string val);
    std::string demangle(const char* mangled);
}

#endif //BACKTRACE_H
