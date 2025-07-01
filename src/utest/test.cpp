#if DEBUG

#include <thread>
#include <cstring>
#include <vector>
#include "test/test.h"
#include "helper/lz4.h"

class simple_unit_test_ final : test::unit_t {
public:
    std::string name() override {
        return "Simple test";
    }

    std::string success() override {
        return "NORMAL UNIT TEST";
    }

    std::string failure() override {
        return "NORMAL UNIT TEST FAILED";
    }

    bool run() override {
        return true;
    }
} simple_unit_test;

class delay_unit_test_ final : test::unit_t {
public:
    std::string name() override {
        return "Simple test (delayed)";
    }

    std::string success() override {
        return "NORMAL UNIT TEST (delayed)";
    }

    std::string failure() override {
        return "NORMAL UNIT TEST FAILED (delayed)";
    }

    bool run() override {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return true;
    }
} delay_unit_test;

class failed_unit_test_ final : test::unit_t {
public:
    std::string name() override {
        return "Unit test (designed to fail)";
    }

    std::string success() override {
        exit(EXIT_FAILURE);
    }

    std::string failure() override {
        return "DESIGNED FAILURE";
    }

    bool run() override {
        return false;
    }
} failed_unit_test;

class failed_delay_test_ final : test::unit_t {
public:
    std::string name() override {
        return "Unit test (delayed, designed to fail)";
    }

    std::string success() override {
        exit(EXIT_FAILURE);
    }

    std::string failure() override {
        return "DESIGNED FAILURE (delayed)";
    }

    bool run() override {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        return false;
    }
} failed_delay_test;

class lz4_test_ final : test::unit_t {
public:
    std::string name() override {
        return "LZ4 test";
    }

    std::string success() override {
        return "LZ4 test succeeded";
    }

    std::string failure() override {
        return "LZ4 test failed";
    }

    bool test_one_time(const std::vector<char> & src)
    {
        const int srcSize = static_cast<int>(src.size());
        const int maxDst = LZ4_compressBound(srcSize);          /* worst-case size */
        std::vector<char> compressed(maxDst);
        const int cSize = LZ4_compress_default(src.data(), compressed.data(), srcSize, maxDst); /* returns bytes */
        std::vector<char> restored(srcSize);
        const int dSize = LZ4_decompress_safe(compressed.data(), restored.data(), cSize, srcSize);
        return dSize == srcSize && !memcmp(src.data(), restored.data(), srcSize);
    }

    bool run() override
    {
        const char *msg =
            "QWhoaGhoaCBNaWthIGlzIHRoZSBjdXRlc3QgY2F0Ym95IEkgaGF2ZSBldmVyIHNlZW4gYWhoaGho\n"
            "aGhoIHNvIGN1dGUgc28gY3V0ZSBJIHdhbnQgdG8ga2lzcyBoaXMgZmFjZSBhaGhoaGgK\n";
        if (const auto instance = std::string(msg);
            !test_one_time({instance.begin(), instance.end()}))
        {
            return false;
        }

        std::vector<char> src(1024 * 32);
        for (int i = 0; i < 8192; i++)
        {
            for (auto & c : src) {
                c = static_cast<char>(rand());
            }

            if (!test_one_time(src)) {
                return false;
            }
        }

        return true;
    }
} lz4_test;

std::map < std::string, void * > test::unit_tests = {
    // unit test dummies
    { "@@__delay_faulty__", &failed_delay_test },
    { "@@__faulty__", &failed_unit_test },
    { "@@__simple__", &simple_unit_test },
    { "@@__delay_simple__", &delay_unit_test },
    // unit test dummies end

    { "LZ4", &lz4_test }, // utilities
};

#endif
