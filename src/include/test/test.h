#ifndef TEST_H
#define TEST_H

#include <map>
#include <string>

namespace test {

class unit_t {
public:
    virtual ~unit_t() = default;
    unit_t() = default;
    virtual bool run() = 0;
    virtual std::string success() = 0;
    virtual std::string failure() = 0;
    virtual std::string name() = 0;
};

extern std::map < std::string, void * > unit_tests;
} // test

#endif //TEST_H
