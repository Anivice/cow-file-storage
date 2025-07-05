/* test.cpp
 *
 * Copyright 2025 Anivice Ives
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#if DEBUG
#include <thread>
#include <cstring>
#include <vector>
#include <filesystem>
#include "helper/cpp_assert.h"
#include "helper/lz4.h"
#include "test/test.h"

#include "core/basic_io.h"
#include "core/block_io.h"
#include "core/bitmap.h"
#include "core/ring_buffer.h"
#include "core/block_attr.h"

#define RANDOM (static_cast<int>(test::fast_rand64()) & 0x7FFFFFFF)

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

class basic_io_test_ final : test::unit_t {
    std::string name() override {
        return "BasicIO test";
    }

    std::string success() override {
        return "BasicIO test succeeded";
    }


    std::string reason;

    std::string failure() override {
        return "BasicIO test failed: " + reason;
    }

    bool run() override
    {
        try {
            if (std::filesystem::exists("/tmp/.disk_img")) {
                std::filesystem::remove("/tmp/.disk_img");
            }
            std::filesystem::copy_file(SOURCE_DIR "/data/disk.img", "/tmp/.disk_img");
            basic_io_t basic_io;
            basic_io.open("/tmp/.disk_img");
        } catch (const std::exception & e) {
            try { std::filesystem::remove("/tmp/.disk_img"); } catch (...) { }
            reason = e.what();
            reason = reason.substr(0, reason.find_first_of('\n') == std::string::npos ? reason.length() : reason.find_first_of('\n') - 1);
            return false;
        }

        return true;
    }
} basic_io_test;

class block_io_test_ final : test::unit_t {
    std::string name() override {
        return "BlockIO test";
    }

    std::string success() override {
        return "BlockIO test succeeded";
    }


    std::string reason;

    std::string failure() override {
        return "BlockIO test failed: " + reason;
    }

    bool run() override
    {
        try {
            if (std::filesystem::exists("/tmp/.disk_img")) {
                std::filesystem::remove("/tmp/.disk_img");
            }
            std::filesystem::copy_file(SOURCE_DIR "/data/disk.img", "/tmp/.disk_img");
            basic_io_t basic_io;
            basic_io.open("/tmp/.disk_img");
            {
                block_io_t block_io(basic_io);
                auto blk = block_io.safe_at(0);
                std::vector<uint8_t> data;
                data.reserve(block_io.get_block_size());
                blk->get(data.data(), block_io.get_block_size(), 0);
            }
            basic_io.close();
            std::filesystem::remove("/tmp/.disk_img");
        } catch (const std::exception & e) {
            try { std::filesystem::remove("/tmp/.disk_img"); } catch (...) { }
            reason = e.what();
            return false;
        }

        return true;
    }
} block_io_test;

class bitmap_test_ final : test::unit_t {
    std::string name() override {
        return "Bitmap test";
    }

    std::string success() override {
        return "Bitmap test succeeded";
    }


    std::string reason;

    std::string failure() override {
        return "Bitmap test failed: " + reason;
    }

    bool run() override
    {
        try {
            if (std::filesystem::exists("/tmp/.disk_img")) {
                std::filesystem::remove("/tmp/.disk_img");
            }
            std::filesystem::copy_file(SOURCE_DIR "/data/disk.img", "/tmp/.disk_img");
            basic_io_t basic_io;
            basic_io.open("/tmp/.disk_img");
            {
                block_io_t block_io(basic_io);
                auto blk = block_io.safe_at(0);
                cfs_head_t cfs_head{};
                auto head = block_io.safe_at(0);
                auto data = block_io.safe_at(1);
                head->get((uint8_t*)&cfs_head, sizeof(cfs_head), 0);
                bitmap bmap(block_io, 1, 4, 12288, cfs_head.static_info.block_size);
                for (int i = 0; i < 12288 * 15; i++)
                {
                    int index = RANDOM % 12288;
                    bool val;
                    for (int j = 0; j < 8; j++) {
                        val = RANDOM % 2; bmap.set(index, val); assert_short(bmap.get(index) == val);
                    }
                    bmap.set(index, true); assert_short(bmap.get(index));
                    bmap.set(index, false); assert_short(!bmap.get(index));
                }

                for (int i = 0; i < 12288; i++) {
                    assert_short(!bmap.get(i));
                }
            }
            basic_io.close();
            std::filesystem::remove("/tmp/.disk_img");
        } catch (const std::exception & e) {
            try { std::filesystem::remove("/tmp/.disk_img"); } catch (...) { }
            reason = e.what();
            return false;
        }

        return true;
    }
} bitmap_test;

class ringbuffer_test_ final : test::unit_t {
    std::string name() override {
        return "Ring buffer test";
    }

    std::string success() override {
        return "Ring buffer test succeeded";
    }


    std::string reason;

    std::string failure() override {
        return "Ring buffer test failed: " + reason;
    }

    bool run() override
    {
        try {
            if (std::filesystem::exists("/tmp/.disk_img")) {
                std::filesystem::remove("/tmp/.disk_img");
            }
            std::filesystem::copy_file(SOURCE_DIR "/data/disk.img", "/tmp/.disk_img");
            basic_io_t basic_io;
            basic_io.open("/tmp/.disk_img");
            {
                block_io_t block_io(basic_io);
                auto blk = block_io.safe_at(0);
                cfs_head_t cfs_head{};
                auto head = block_io.safe_at(0);
                head->get((uint8_t*)&cfs_head, sizeof(cfs_head), 0);
                ring_buffer buffer(block_io, cfs_head.static_info.block_size, 1, 5);
                std::vector<uint8_t> data, data2;
                data.resize(338);
                data2.resize(338);

                for (int k = 0; k < 510; k++)
                {
                    for (int i = 0; i < 128; i++)
                    {
                        for (auto & c : data) {
                            c = RANDOM % 255;
                        }

                        buffer.write(data.data(), data.size());
                    }

                    while (buffer.read(data2.data(), data2.size()));
                }

                for (int i = 0; i < 32*1024*7; i++)
                {
                    uint8_t byte1 = RANDOM % 255, byte2 = RANDOM % 255, byte3 = RANDOM % 255, byte_r;
                    buffer.write(&byte1, 1);
                    buffer.write(&byte2, 1);
                    buffer.write(&byte3, 1);
                    buffer.read(&byte_r, 1);
                    buffer.read(&byte_r, 1);
                    buffer.read(&byte_r, 1);
                    assert_short(byte3 == byte_r);
                }
            }
            basic_io.close();
            std::filesystem::remove("/tmp/.disk_img");
        } catch (const std::exception & e) {
            try { std::filesystem::remove("/tmp/.disk_img"); } catch (...) { }
            reason = e.what();
            return false;
        }

        return true;
    }
} ringbuffer_test;

class block_attr_test_ final : test::unit_t {
    std::string name() override {
        return "Block attributes test";
    }

    std::string success() override {
        return "Block attributes test succeeded";
    }


    std::string reason;

    std::string failure() override {
        return "Block attributes test failed: " + reason;
    }

    bool run() override
    {
        try {
            if (std::filesystem::exists("/tmp/.disk_img")) {
                std::filesystem::remove("/tmp/.disk_img");
            }
            std::filesystem::copy_file(SOURCE_DIR "/data/disk.img", "/tmp/.disk_img");
            basic_io_t basic_io;
            basic_io.open("/tmp/.disk_img");
            {
                block_io_t block_io(basic_io);
                auto blk = block_io.safe_at(0);
                cfs_head_t cfs_head{};
                auto head = block_io.safe_at(0);
                auto data = block_io.safe_at(1);
                head->get((uint8_t*)&cfs_head, sizeof(cfs_head), 0);
                block_attr_t battr(block_io, cfs_head.static_info.block_size, 1, 5, 12288);
                for (int i = 0; i < 12288 * 15; i++)
                {
                    int index = RANDOM % 12288;
                    uint16_t val;
                    for (int j = 0; j < 8; j++) {
                        val = RANDOM % 65535; battr.set(index, val); assert_short(battr.get(index) == val);
                    }
                    battr.set(index, 1); assert_short(battr.get(index));
                    battr.set(index, 0); assert_short(!battr.get(index));
                }

                for (int i = 0; i < 12288; i++) {
                    assert_short(!battr.get(i));
                }
            }
            basic_io.close();
            std::filesystem::remove("/tmp/.disk_img");
        } catch (const std::exception & e) {
            try { std::filesystem::remove("/tmp/.disk_img"); } catch (...) { }
            reason = e.what();
            return false;
        }

        return true;
    }
} block_attr;

std::map < std::string, void * > test::unit_tests = {
    // unit test dummies
    { "@@__delay_faulty__", &failed_delay_test },
    { "@@__faulty__", &failed_unit_test },
    { "@@__simple__", &simple_unit_test },
    { "@@__delay_simple__", &delay_unit_test },
    // unit test dummies end

    { "LZ4", &lz4_test }, // utilities
    {"BasicIO", &basic_io_test }, { "BlockIO", &block_io_test }, // Basic filesystem IO
    { "Bitmap", &bitmap_test }, { "RingBuffer", &ringbuffer_test }, { "BlockAttr", &block_attr }
};

#endif
