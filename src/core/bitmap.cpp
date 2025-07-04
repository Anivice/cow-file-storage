/* bitmap.cpp
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

#include "core/bitmap.h"
#include "core/crc64sum.h"
#include "helper/cpp_assert.h"
#include "helper/log.h"

bitmap::bitmap(block_io_t & block_mapping_, const uint64_t map_start_, const uint64_t map_end_, const uint64_t boundary_, const uint64_t block_size_)
    : block_mapping(block_mapping_), map_start(map_start_), map_end(map_end_), boundary(boundary_), blk_size(block_size_)
{
    assert_short(map_start_ < map_end_);
}

bool bitmap::get(const uint64_t index)
{
    assert_short(index < boundary);
    const uint64_t byte_offset = index / 8;
    const uint64_t bit_offset = index % 8;
    const uint64_t block_offset = byte_offset / blk_size;
    const uint64_t byte_in_block = byte_offset % blk_size;
    assert_short(block_offset < (map_end - map_start));

    std::lock_guard lock(mutex);
    // debug_log("Block: ", block_offset, ", Byte: ", byte_in_block, ", Bit: ", bit_offset);
    auto desired_block = block_mapping.safe_at(block_offset + map_start);
    uint8_t data;
    desired_block->get(&data, 1, byte_in_block);
    data >>= bit_offset;
    const auto result = data & 0x01;
    return result;
}

void bitmap::set(const uint64_t index, const bool val)
{
    assert_short(index < boundary);
    const uint64_t byte_offset = index / 8;
    const uint64_t bit_offset = index % 8;
    const uint64_t block_offset = byte_offset / blk_size;
    const uint64_t byte_in_block = byte_offset % blk_size;
    assert_short(block_offset < (map_end - map_start));

    std::lock_guard lock(mutex);
    // debug_log("Block: ", block_offset, ", Byte: ", byte_in_block, ", Bit: ", bit_offset);
    auto desired_block = block_mapping.safe_at(block_offset + map_start);
    uint8_t data;
    desired_block->get(&data, 1, byte_in_block);
    uint8_t comp = 0x01;
    comp <<= bit_offset;
    if (val) {
        data |= comp;
    } else {
        data &= ~comp;
    }
    desired_block->update(&data, 1, byte_in_block);
    // desired_block.sync();
}

uint64_t bitmap::hash()
{
    CRC64 hash;
    std::vector<uint8_t> data(blk_size);

    std::lock_guard lock(mutex);
    for (uint64_t i = map_start; i < map_end; i++) {
        auto desired_block = block_mapping.safe_at(i);
        desired_block->get(data.data(), blk_size, 0);
        hash.update(data.data(), blk_size);
    }

    return hash.get_checksum();
}
