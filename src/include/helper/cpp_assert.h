#ifndef CPP_ASSERT_H
#define CPP_ASSERT_H

#include "err_type.h"

inline void assert_throw(const bool condition, const std::string & message)
{
    if (!condition)
    {
        throw runtime_error(message);
    }
}

#define _line_str(x) #x
#define line_str(x) _line_str(x)
#define assert_short(condition) assert_throw(condition, __FILE__ ":" line_str(__LINE__) ": " #condition)

#endif //CPP_ASSERT_H
