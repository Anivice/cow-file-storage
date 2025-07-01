#include "helper/log.h"

std::mutex debug::log_mutex;
std::atomic_bool debug::verbose = false;
int debug::caller_max_size = 0;
bool debug::do_i_show_caller_next_time = true;
std::string debug::_strip_name_(const std::string & name)
{
    const std::regex pattern(R"([\w]+ (.*)\(.*\))");
    if (std::smatch matches; std::regex_match(name, matches, pattern) && matches.size() > 1) {
        return matches[1];
    }

    return name;
}
