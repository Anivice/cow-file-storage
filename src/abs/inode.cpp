/* inode.cpp
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

#include "service.h"

filesystem::inode_t::inode_header_t filesystem::inode_t::get_header()
{
    inode_header_t header{};
    fs.read_block(inode_id, &header, sizeof(header), 0);
    return header;
}

void filesystem::inode_t::save_header(const inode_header_t header)
{
    fs.write_block(inode_id, &header, sizeof(header), 0);
}

std::vector < uint64_t > filesystem::inode_t::get_block_pointers()
{
    std::vector < uint64_t > block_pointers;
    std::vector < uint8_t > block_data;
    block_data.resize(block_size);
    block_pointers.resize((block_size - sizeof(inode_header_t)) / sizeof(uint64_t));

    fs.read_block(inode_id, block_data.data(), block_size, 0);
    std::memcpy(block_pointers.data(), block_data.data() + sizeof(inode_header_t), block_pointers.size() * 8);
    return block_pointers;
}

void filesystem::inode_t::save_block_pointers(const std::vector < uint64_t > & block_pointers)
{
    fs.write_block(inode_id, block_pointers.data(), block_pointers.size() * 8, sizeof(inode_header_t));
}

filesystem::inode_t::inode_t(filesystem & fs, const uint64_t inode_id, const uint64_t block_size)
    : fs(fs), inode_id(inode_id), block_size(block_size)
{
}

/*
 * INDEX NODE -> [L1 PTR 1], [L1 PTR 2], [L1 PTR 3], ..., [L1 PTR n]
 *               |
 *               -> [L2 PTR 1], [L2 PTR 2], [L2 PTR 3], ..., [L2 PTR (BLOCK SIZE / 8 <POINTER SIZE>)]
 *                  |
 *                  -> [L3 PTR 1], [L3 PTR 2], [L3 PTR 3], ..., [L3 PTR (BLOCK SIZE / 8 <POINTER SIZE>)]
 *                      |           |           |               |
 *                      |           |           |               -> [STORAGE BLOCK (BLOCK SIZE / 8 <POINTER SIZE>)]
 *                      |           |           |
 *                      |           |           -> [STORAGE BLOCK 3]
 *                      |           -> [STORAGE BLOCK 2]
 *                      -> [STORAGE BLOCK 1]
 */

// struct block_mapping_tail_t {
//     uint64_t inode_level_pointers; // i.e., level 1 pointers
//     uint64_t level2_pointers; // level 2 pointers reside in level 1 pointed blocks, whose number is given by inode_level_pointers
//     uint64_t level3_pointers; // level 3 pointers reside in level 2 pointed blocks, whose number is given by level2_pointers
// };
//
// block_mapping_tail_t pointer_mapping_linear_to_abstracted(
//     const uint64_t file_location,
//     const uint64_t inode_level1_pointers,
//     const uint64_t level2_pointers_per_block,
//     const uint64_t block_size)
// {
//     const uint64_t max_file_size = inode_level1_pointers * level2_pointers_per_block * level2_pointers_per_block * block_size;
//     const uint64_t level3_pointers_in_level2 = inode_level1_pointers * level2_pointers_per_block * level2_pointers_per_block;
//     const uint64_t level2_pointers_in_level1 = inode_level1_pointers * level2_pointers_per_block;
//
//     if (file_location > max_file_size) {
//         throw fs_error::filesystem_space_depleted("Exceeding max file size");
//     }
//
//     const uint64_t required_blocks = ceil_div(file_location, block_size); // i.e., level 3 pointers
//     const uint64_t required_level2_pointers = ceil_div(required_blocks, level3_pointers_in_level2);
//     const uint64_t required_level1_pointers = ceil_div(required_blocks, level2_pointers_in_level1);
//
//     return {
//         .inode_level_pointers = inode_level1_pointers,
//         .level2_pointers = required_level2_pointers,
//         .level3_pointers = required_level1_pointers,
//     };
// }

void filesystem::inode_t::resize(uint64_t block_size)
{
    std::lock_guard<std::mutex> guard(mutex);
}

void filesystem::inode_t::read(void *buff, uint64_t offset, uint64_t size)
{
    std::lock_guard<std::mutex> guard(mutex);
}

void filesystem::inode_t::write(const void * buff, uint64_t offset, uint64_t size)
{
    std::lock_guard<std::mutex> guard(mutex);
}

void filesystem::inode_t::rename(const char * new_name)
{
    std::lock_guard<std::mutex> guard(mutex);
}

void filesystem::inode_t::remove()
{
    std::lock_guard<std::mutex> guard(mutex);
}
