/* ch64sum.h
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

#ifndef CRC64SUM_H
#define CRC64SUM_H

#include <cstdint>
#include <vector>

#ifdef __unix__
# undef LITTLE_ENDIAN
# undef BIG_ENDIAN
#endif // __unix__

enum endian_t { LITTLE_ENDIAN, BIG_ENDIAN };

class CRC64 {
public:
    CRC64();
    void update(const uint8_t* data, size_t length);

    [[nodiscard]] uint64_t get_checksum(endian_t endian = BIG_ENDIAN
        /* CRC64 tools like 7ZIP display in BIG_ENDIAN */) const;

private:
    uint64_t crc64_value{};
    uint64_t table[256] {};

    void init_crc64();
    static uint64_t reverse_bytes(uint64_t x);
};

[[nodiscard]] inline
uint64_t hashcrc64(const std::vector<uint8_t> & data) {
    CRC64 hash;
    hash.update(data.data(), data.size());
    return hash.get_checksum();
}

template < typename Type >
concept PODType = std::is_standard_layout_v<Type> && std::is_trivial_v<Type>;

template < PODType Type >
[[nodiscard]] uint64_t hashcrc64(const Type & data) {
    CRC64 hash;
    hash.update((uint8_t*)&data, sizeof(data));
    return hash.get_checksum();
}

#endif //CRC64SUM_H
