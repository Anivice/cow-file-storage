/* ch64sum.cpp
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

#include <cstdint>
#include "core/crc64sum.h"

#ifdef __unix__
# undef LITTLE_ENDIAN
# undef BIG_ENDIAN
#endif // __unix__

CRC64::CRC64() {
    init_crc64();
}

void CRC64::update(const uint8_t* data, const size_t length) {
    for (size_t i = 0; i < length; ++i) {
        crc64_value = table[(crc64_value ^ data[i]) & 0xFF] ^ (crc64_value >> 8);
    }
}

[[nodiscard]] uint64_t CRC64::get_checksum(const endian_t endian
    /* CRC64 tools like 7ZIP display in BIG_ENDIAN */) const
{
    // add the final complement that ECMA‑182 requires
    return (endian == BIG_ENDIAN
        ? reverse_bytes(crc64_value ^ 0xFFFFFFFFFFFFFFFFULL)
        : (crc64_value ^ 0xFFFFFFFFFFFFFFFFULL));
}

void CRC64::init_crc64()
{
    crc64_value = 0xFFFFFFFFFFFFFFFF;
    for (uint64_t i = 0; i < 256; ++i) {
        uint64_t crc = i;
        for (uint64_t j = 8; j--; ) {
        if (crc & 1)
            crc = (crc >> 1) ^ 0xC96C5795D7870F42;  // Standard CRC-64 polynomial
        else
            crc >>= 1;
        }
        table[i] = crc;
    }
}

uint64_t CRC64::reverse_bytes(uint64_t x)
{
    x = ((x & 0x00000000FFFFFFFFULL) << 32) | ((x & 0xFFFFFFFF00000000ULL) >> 32);
    x = ((x & 0x0000FFFF0000FFFFULL) << 16) | ((x & 0xFFFF0000FFFF0000ULL) >> 16);
    x = ((x & 0x00FF00FF00FF00FFULL) << 8)  | ((x & 0xFF00FF00FF00FF00ULL) >> 8);
    return x;
}
