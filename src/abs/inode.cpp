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

#include <ranges>
#include <functional>
#include "service.h"
#include "helper/cpp_assert.h"
#include "helper/log.h"

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

std::vector < uint64_t > filesystem::inode_t::get_inode_block_pointers()
{
    std::vector < uint64_t > block_pointers;
    block_pointers.resize((block_size - sizeof(inode_header_t)) / sizeof(uint64_t));
    fs.read_block(inode_id, block_pointers.data(), block_pointers.size() * sizeof(uint64_t), sizeof(inode_header_t));
    return block_pointers;
}

void filesystem::inode_t::save_inode_block_pointers(const std::vector < uint64_t > & block_pointers)
{
    fs.write_block(inode_id, block_pointers.data(), block_pointers.size() * sizeof(uint64_t), sizeof(inode_header_t));
}

filesystem::inode_t::inode_t(filesystem & fs, const uint64_t inode_id, const uint64_t block_size)
    : fs(fs), inode_id(inode_id), block_size(block_size),
        inode_level_pointers((block_size - sizeof(inode_header_t)) / sizeof(uint64_t)),
        block_max_entries(block_size / sizeof(uint64_t))
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

struct block_mapping_tail_t {
    uint64_t inode_level_pointers; // i.e., level 1 pointers
    uint64_t level2_pointers; // level 2 pointers reside in level 1 pointed blocks, whose number is given by inode_level_pointers
    uint64_t level3_pointers; // level 3 pointers reside in level 2 pointed blocks, whose number is given by level2_pointers
    uint64_t last_level2_pointer_block_has_this_many_pointers;
    uint64_t last_level3_pointer_block_has_this_many_pointers;

    // (last_level3_pointer_block_has_this_many_pointers == 0 ?
    // ((last_level2_pointer_block_has_this_many_pointers == 0 ? inode_level_pointers : (inode_level_pointers - 1)) * 512 + last_level2_pointer_block_has_this_many_pointers)
    // : ((last_level2_pointer_block_has_this_many_pointers == 0 ? inode_level_pointers : (inode_level_pointers - 1)) * 512 + last_level2_pointer_block_has_this_many_pointers) - 1)
    // * 512 + last_level3_pointer_block_has_this_many_pointers

    void assert() const
    {
        const auto level2_pointers_ver = (last_level2_pointer_block_has_this_many_pointers == 0 ?
            inode_level_pointers : (inode_level_pointers - 1)) * 512 + last_level2_pointer_block_has_this_many_pointers;
        const auto level3_pointers_ver = (last_level3_pointer_block_has_this_many_pointers == 0 ?
            level2_pointers_ver : level2_pointers_ver - 1) * 512 + last_level3_pointer_block_has_this_many_pointers;
        assert_short(level3_pointers_ver == level3_pointers);
    }
};

block_mapping_tail_t pointer_mapping_linear_to_abstracted(
    const uint64_t file_length,
    const uint64_t inode_level1_pointers,
    const uint64_t level2_pointers_per_block,
    const uint64_t block_size)
{
    const uint64_t max_file_size = inode_level1_pointers * level2_pointers_per_block * level2_pointers_per_block * block_size;

    if (file_length > max_file_size) {
        throw fs_error::filesystem_space_depleted("Exceeding max file size");
    }

    const uint64_t required_blocks = ceil_div(file_length, block_size); // i.e., level 3 pointers
    const uint64_t required_level2_pointers = ceil_div(required_blocks, level2_pointers_per_block);
    const uint64_t required_level1_pointers = ceil_div(required_level2_pointers, level2_pointers_per_block);

    const block_mapping_tail_t ret = {
        .inode_level_pointers = required_level1_pointers,
        .level2_pointers = required_level2_pointers,
        .level3_pointers = required_blocks,
        .last_level2_pointer_block_has_this_many_pointers = required_level2_pointers % level2_pointers_per_block,
        .last_level3_pointer_block_has_this_many_pointers = required_blocks % level2_pointers_per_block,
    };

    if (DEBUG) ret.assert();
    return ret;
}

std::vector < uint64_t > filesystem::inode_t::get_pointer_by_block(const uint64_t data_field_block_id)
{
    std::vector < uint64_t > block_pointers;
    block_pointers.resize(block_max_entries);
    fs.read_block(data_field_block_id, block_pointers.data(), block_pointers.size() * sizeof(uint64_t), 0);
    return block_pointers;
}

void filesystem::inode_t::save_pointer_to_block(const uint64_t data_field_block_id, const std::vector < uint64_t > & block_pointers)
{
    fs.write_block(data_field_block_id, block_pointers.data(), block_pointers.size() * sizeof(uint64_t), 0);
}

void filesystem::inode_t::unblocked_resize(const uint64_t file_length)
{
    const auto abstracted_mapping = pointer_mapping_linear_to_abstracted(
        file_length,
        inode_level_pointers,
        block_max_entries,
        block_size);
    auto level1_pointers = get_inode_block_pointers();
    assert_short(level1_pointers.size() >= abstracted_mapping.inode_level_pointers);

    auto mkblk = [&]->uint64_t
    {
        const auto new_block_id = fs.allocate_new_block();
        constexpr cfs_blk_attr_t pointer_attributes = {
            .frozen = 0,
            .type = POINTER_TYPE,
            .type_backup = 0,
            .cow_refresh_count = 0,
            .links = 1,
        };
        fs.set_attr(new_block_id, pointer_attributes);
        std::vector<uint8_t> block_data_empty;
        block_data_empty.resize(block_size);
        std::memset(block_data_empty.data(), 0, block_size);
        fs.write_block(new_block_id, block_data_empty.data(), block_size, 0);
        return new_block_id;
    };

    class chain_level2 {
    public:
        std::unique_ptr<chain_level2> next;
        std::vector < std::pair < bool, std::vector<bool> > > block_pointers;

        explicit chain_level2(const uint64_t limit)
        {
            block_pointers.resize(limit);
            for (auto & level3 : block_pointers | std::views::values) {
                level3.resize(limit);
            }

            next = nullptr;
        }

        void add_block(const uint64_t pointers_for_level2, const uint64_t pointers_for_level3)
        {
            const uint64_t this_block_count = pointers_for_level2 > block_pointers.size() ? block_pointers.size() : pointers_for_level2;
            const uint64_t next_block_count = pointers_for_level2 - this_block_count;
            uint64_t level3_pointer_allocated_this_level = 0;
            for (uint64_t i = 0; i < this_block_count; ++i)
            {
                if (!block_pointers[i].first)
                {
                    block_pointers[i].first = true;
                    const uint64_t level3_allocation_this_step = std::min(pointers_for_level3 - level3_pointer_allocated_this_level,
                        block_pointers.size());
                    for (uint64_t j = 0; j < level3_allocation_this_step; ++j) {
                        block_pointers[i].second[j] = true;
                        level3_pointer_allocated_this_level++;
                    }
                }
            }

            if (next_block_count > 0) {
                next = std::make_unique<chain_level2>(block_pointers.size());
                next->add_block(next_block_count, pointers_for_level3 - level3_pointer_allocated_this_level);
            }
        }
    };

    chain_level2 chain(block_max_entries);
    chain.add_block(abstracted_mapping.level2_pointers, abstracted_mapping.level3_pointers);

    // normalize
    std::function<void(chain_level2 *)> recursive_mapping;
    std::vector<std::vector<std::vector<bool>>> block_pointers_hierarchy;
    recursive_mapping = [&](chain_level2 * this_chain)
    {
        if (this_chain) {
            std::vector<std::vector<bool>> level2_and_3;
            level2_and_3.resize(block_max_entries);
            for (auto & level3 : level2_and_3) {
                level3.resize(block_max_entries);
            }

            for (uint64_t i = 0; i < block_max_entries; ++i) {
                for (uint64_t j = 0; j < block_max_entries; ++j) {
                    level2_and_3[i][j] = this_chain->block_pointers[i].second[j];
                }
            }

            block_pointers_hierarchy.emplace_back(level2_and_3);

            recursive_mapping(this_chain->next.get());
        }
    };
    recursive_mapping(&chain);

    struct position_t {
        uint64_t level1_position;
        uint64_t level2_position;
        uint64_t level3_position;
    };

    auto linear_to_segments = [&](const uint64_t linear_position) {
        const uint64_t level2_position = linear_position / block_max_entries;
        return (position_t){
            .level1_position = level2_position / block_max_entries,
            .level2_position = level2_position % block_max_entries,
            .level3_position = linear_position % block_max_entries,
        };
    };

    auto safe_delete = [&](uint64_t & block_id)
    {
        assert_short(block_id != 0);
        const auto attr = fs.get_attr(block_id);
        if (attr.frozen) {
            return;
        }

        if (attr.links > 1) {
            fs.delink_block(block_id);
        } else {
            fs.deallocate_block(block_id);
        }

        block_id = 0;
    };

    uint64_t block_count = 0;
    for (uint64_t block_offset = 0; block_offset < abstracted_mapping.level3_pointers; ++block_offset) {
        auto segmentations = linear_to_segments(block_offset);
        if (block_pointers_hierarchy[segmentations.level1_position][segmentations.level2_position][segmentations.level3_position]) {
            block_count++;
        }
    }

    auto index_node_pointers = get_inode_block_pointers();
    assert_short(index_node_pointers.size() >= block_pointers_hierarchy.size());
    // std::vector<uint64_t> pending_for_deletion;
    for (uint64_t i = 0; i < index_node_pointers.size(); ++i)
    {
        if (i < block_pointers_hierarchy.size())
        {
            if (index_node_pointers[i] == 0) index_node_pointers[i] = mkblk();
            auto pointer_level2 = get_pointer_by_block(index_node_pointers[i]);
            for (uint64_t j = 0; j < block_max_entries; ++j)
            {
                if (!block_pointers_hierarchy[i][j][0]) break;

                if (pointer_level2[j] == 0) pointer_level2[j] = mkblk();
                auto pointer_level3 = get_pointer_by_block(pointer_level2[j]);
                for (uint64_t k = 0; k < block_max_entries; ++k) {
                    if (block_pointers_hierarchy[i][j][k]) {
                        if (pointer_level3[k] == 0) pointer_level3[k] = mkblk();
                    } else {
                        if (pointer_level3[k] != 0) safe_delete(pointer_level3[k]);
                    }
                }
                save_pointer_to_block(pointer_level2[j], pointer_level3);
            }
            save_pointer_to_block(index_node_pointers[i], pointer_level2);
        }
        else if (i > block_pointers_hierarchy.size() && index_node_pointers[i] != 0)
        {
            for (uint64_t j = 0; j < block_max_entries; ++j)
            {
                auto pointer_level2 = get_pointer_by_block(index_node_pointers[i]);
                if (pointer_level2[j] != 0)
                {
                    for (uint64_t k = 0; k < block_max_entries; ++k)
                    {
                        auto pointer_level3 = get_pointer_by_block(pointer_level2[j]);
                        if (pointer_level3[k] != 0) {
                            safe_delete(pointer_level3[k]);
                        }
                    }
                }
            }

            safe_delete(index_node_pointers[i]);
            index_node_pointers[i] = 0;
        }
        else if (i >= block_pointers_hierarchy.size() && index_node_pointers[i] == 0) {
            break;
        }

        save_inode_block_pointers(index_node_pointers);
    }
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
