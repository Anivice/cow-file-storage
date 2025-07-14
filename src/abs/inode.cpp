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
#include <random>
#include "service.h"
#include "helper/cpp_assert.h"
#include "helper/log.h"

#define MAX_CACHE_INODE_SIZE (65535)
#define auto_clean(map) if ((map).size() > MAX_CACHE_INODE_SIZE) { (map).clear(); }

filesystem::inode_t::inode_header_t filesystem::inode_t::unblocked_get_header()
{
    inode_header_t header{};
    fs.read_block(inode_id, &header, sizeof(header), 0);
    return header;
}

void filesystem::inode_t::unblocked_save_header(const inode_header_t header)
{
    fs.write_block(inode_id, &header, sizeof(header), 0, true);
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
    fs.write_block(inode_id, block_pointers.data(), block_pointers.size() * sizeof(uint64_t), sizeof(inode_header_t), true);
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

filesystem::inode_t::inode_header_t filesystem::inode_t::get_header() {
    return unblocked_get_header();
}

void filesystem::inode_t::save_header(const filesystem::inode_t::inode_header_t & header) {
    if (const auto attr = fs.get_attr(inode_id); attr.frozen) return;
    unblocked_save_header(header);
}

void filesystem::inode_t::resize(const uint64_t new_size)
{
    auto header = unblocked_get_header();
    header.attributes.st_mtim = get_current_time();
    unblocked_save_header(header);
    unblocked_resize(new_size);
}

void filesystem::inode_t::unlink_self()
{
    const auto level2_blocks = linearized_level2_pointers();
    const auto level3_blocks = linearized_level3_pointers();
    auto unlink_block = [&](const uint64_t block_id)
    {
        fs.delink_block(block_id);
        const auto attr = fs.block_manager->get_attr(block_id);
        if (attr.frozen) {
            return;
        }

        if (attr.links == 0) {
            fs.deallocate_block(block_id);
        }
    };

    auto unlink_blocks = [&](const std::vector < uint64_t > & block_ids) {
        for (const auto block_id : block_ids) {
            unlink_block(block_id);
        }
    };

    unlink_blocks(level2_blocks);
    unlink_blocks(level3_blocks);
    unlink_block(inode_id);
}

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

    if constexpr (DEBUG) ret.assert();
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
    fs.write_block(data_field_block_id, block_pointers.data(), block_pointers.size() * sizeof(uint64_t), 0, true);
}

void filesystem::inode_t::unblocked_resize(const uint64_t file_length)
{
    auto header = unblocked_get_header();
    if (static_cast<uint64_t>(header.attributes.st_size) == file_length) return;
    std::vector < std::unique_ptr<level3> > level3s_;
    std::vector < std::unique_ptr<level3> > level2s_;
    std::vector < std::unique_ptr<level3> > level1s_;

    const auto abstracted_mapping = pointer_mapping_linear_to_abstracted(
        file_length, inode_level_pointers, block_max_entries, block_size);
    const auto actual_level3s = linearized_level3_pointers();
    const auto actual_level2s = linearized_level2_pointers();
    auto actual_level1s = get_inode_block_pointers();
    std::erase_if(actual_level1s, [](const uint64_t p)->bool{ return p == 0; });
    level3s_.resize(actual_level3s.size());
    level2s_.resize(actual_level2s.size());
    level1s_.resize(actual_level1s.size());

    for (uint64_t i = 0; i < actual_level3s.size(); i++) {
        level3s_[i] = std::make_unique<level3>(actual_level3s[i], fs, true, block_size, true);
    }

    for (uint64_t i = 0; i < actual_level2s.size(); i++) {
        level2s_[i] = std::make_unique<level3>(actual_level2s[i], fs, true, block_size);
    }

    for (uint64_t i = 0; i < actual_level1s.size(); i++) {
        level1s_[i] = std::make_unique<level3>(actual_level1s[i], fs, true, block_size);
    }

    level3s_.resize(abstracted_mapping.level3_pointers);
    level2s_.resize(abstracted_mapping.level2_pointers);
    level1s_.resize(abstracted_mapping.inode_level_pointers);

    for (auto & level3 : level3s_) {
        if (level3 == nullptr) {
            level3 = std::make_unique<class level3>(fs, true, block_size, true);
        }

        level3->control_active = false;
    }

    for (auto & level2 : level2s_) {
        if (level2 == nullptr) {
            level2 = std::make_unique<level3>(fs, true, block_size);
        }

        level2->control_active = false;
    }

    for (auto & level1 : level1s_) {
        if (level1 == nullptr) {
            level1 = std::make_unique<level3>(fs, true, block_size);
        }

        level1->control_active = false;
    }

    // fill in the data
    std::vector < std::pair < uint64_t, std::vector<uint64_t> > > level2_literals;
    std::vector < std::pair < uint64_t, std::vector<uint64_t> > > level1_literals;
    std::vector < uint64_t > level1_pointers;
    level1_pointers.reserve(block_max_entries);
    level2_literals.reserve(abstracted_mapping.level2_pointers);
    level1_literals.reserve(abstracted_mapping.inode_level_pointers);

    uint64_t level2_pointer = 0;
    uint64_t level1_pointer = 0;

    std::vector<uint64_t> level3_pointers;
    std::vector<uint64_t> level2_pointers;
    level3_pointers.reserve(block_max_entries);
    level2_pointers.reserve(block_max_entries);

    for (const auto & level3 : level3s_)
    {
        level3_pointers.push_back(level3->data_field_block_id);
        if (level3_pointers.size() == block_max_entries) {
            level2_literals.emplace_back(level2s_[level2_pointer++]->data_field_block_id, level3_pointers);
            level3_pointers.clear();
        }
    }

    if (!level3_pointers.empty()) {
        level2_literals.emplace_back(level2s_[level2_pointer++]->data_field_block_id, level3_pointers);
    }

    for (const auto & level2 : level2s_)
    {
        level2_pointers.push_back(level2->data_field_block_id);
        if (level2_pointers.size() == block_max_entries) {
            level1_pointers.push_back(level1s_[level1_pointer]->data_field_block_id);
            level1_literals.emplace_back(level1s_[level1_pointer++]->data_field_block_id, level2_pointers);
            level2_pointers.clear();
        }
    }

    if (!level2_pointers.empty()) {
        level1_pointers.push_back(level1s_[level1_pointer]->data_field_block_id);
        level1_literals.emplace_back(level1s_[level1_pointer++]->data_field_block_id, level2_pointers);
    }

    // save level 1 -> level 2
    level1_pointers.resize(inode_level_pointers);
    save_inode_block_pointers(level1_pointers);
    for (auto [block, data] : level1_literals) {
        data.resize(block_max_entries);
        save_pointer_to_block(block, data);
    }

    // save level 2 -> level 3
    for (auto & [block, data] : level2_literals) {
        data.resize(block_max_entries);
        save_pointer_to_block(block, data);
    }

    header.attributes.st_size = static_cast<long>(file_length);
    header.attributes.st_ctim = get_current_time();
    unblocked_save_header(header);
}

void filesystem::inode_t::redirect_3rd_level_block(const uint64_t old_data_field_block_id, const uint64_t new_data_field_block_id)
{
    auto try_save = [&](uint64_t & pointer, const std::vector<uint64_t> & data)->bool
    {
        // 1. check level 2 block, see if it's frozen
        if (auto this_level_blk_ptr_attr = fs.get_attr(pointer); !this_level_blk_ptr_attr.frozen) {
            save_pointer_to_block(pointer, data);
            return false;
        }
        else
        {
            const auto new_block = fs.allocate_new_block();

            // 1. update block attributes
            this_level_blk_ptr_attr.frozen = 0;
            fs.set_attr(new_block, this_level_blk_ptr_attr);

            // 2. copy data over
            save_pointer_to_block(new_block, data);

            // 3. unlink old block
            fs.delink_block(pointer);

            debug_log("Redirecting immune block pointer ", pointer, " to new pointer block ", new_block);
            // 4. update parent
            pointer = new_block;
            return true;
        }
    };

    auto level1 = get_inode_block_pointers();
    bool level1_pointers_changed = false;

    for (auto & lv1_blk : level1)
    {
        if (lv1_blk != 0)
        {
            auto lv2_blks = get_pointer_by_block(lv1_blk);
            bool level2_pointers_changed = false;
            for (auto & lv2_blk : lv2_blks)
            {
                if (lv2_blk != 0)
                {
                    bool found = false;
                    auto lv3_blks = get_pointer_by_block(lv2_blk);
                    for (auto & lv3_blk : lv3_blks)
                    {
                        if (lv3_blk != 0 && lv3_blk == old_data_field_block_id)
                        {
                            // redirect means the original block will lose one inode link
                            lv3_blk = new_data_field_block_id;
                            fs.delink_block(lv3_blk);
                            found = true;
                            debug_log("Redirect block pointer from ", old_data_field_block_id, " to ", new_data_field_block_id);
                            break;
                        }
                    }

                    if (found) {
                        level2_pointers_changed = try_save(lv2_blk, lv3_blks);
                        break;
                    }
                }
            }

            if (level2_pointers_changed) {
                level1_pointers_changed = try_save(lv1_blk, lv2_blks);
                break;
            }
        }
    }

    if (level1_pointers_changed) {
        save_inode_block_pointers(level1);
    }
}

std::vector<uint64_t> filesystem::inode_t::linearized_level3_pointers()
{
    const auto level1 = get_inode_block_pointers();
    std::vector<uint64_t> block_pointers;
    for (const auto & lv1_blk : level1)
    {
        if (lv1_blk != 0)
        {
            const auto lv2_blks = get_pointer_by_block(lv1_blk);
            for (const auto & lv2_blk : lv2_blks)
            {
                if (lv2_blk != 0)
                {
                    const auto lv3_blks = get_pointer_by_block(lv2_blk);
                    for (const auto & lv3_blk : lv3_blks)
                    {
                        if (lv3_blk != 0) {
                            block_pointers.push_back(lv3_blk);
                        }
                    }
                }
            }
        }
    }

    return block_pointers;
}

std::vector<uint64_t> filesystem::inode_t::linearized_level2_pointers()
{
    const auto level1 = get_inode_block_pointers();
    std::vector<uint64_t> block_pointers;
    for (const auto & lv1_blk : level1)
    {
        if (lv1_blk != 0)
        {
            const auto lv2_blks = get_pointer_by_block(lv1_blk);
            for (const auto & lv2_blk : lv2_blks)
            {
                if (lv2_blk != 0)
                {
                    block_pointers.push_back(lv2_blk);
                }
            }
        }
    }

    return block_pointers;
}

uint64_t filesystem::inode_t::read(void *buff, uint64_t size, const uint64_t offset)
{
    auto header = unblocked_get_header();
    if (header.attributes.st_size == 0) {
        return 0;
    }

    if (offset > static_cast<uint64_t>(header.attributes.st_size)) {
        return 0;
    }

    if ((offset + size) > static_cast<uint64_t>(header.attributes.st_size)) {
        size = header.attributes.st_size - offset;
    }

    const auto level3_blocks = linearized_level3_pointers();

    const uint64_t first_blk_position = offset / block_size;
    const uint64_t first_blk_offset = offset % block_size;
    uint64_t first_blk_read_size = block_size - first_blk_offset;
    if (first_blk_read_size > size) first_blk_read_size = size;
    const uint64_t continuous_blks = (size - first_blk_read_size) / block_size;
    const uint64_t last_blk_position = first_blk_position + continuous_blks + 1;
    const uint64_t last_blk_read_size = (size - first_blk_read_size) % block_size;

    uint64_t g_wr_off = 0;
    // 1. read first block
    fs.read_block(level3_blocks[first_blk_position], buff, first_blk_read_size, first_blk_offset);
    g_wr_off += first_blk_read_size;

    // 2. read continuous blocks
    for (uint64_t i = 0; i < continuous_blks; i++) {
        const uint64_t blk_position = first_blk_position + 1 + i;
        fs.read_block(level3_blocks[blk_position], static_cast<uint8_t *>(buff) + g_wr_off, block_size, 0);
        g_wr_off += block_size;
    }

    if (last_blk_read_size) {
        fs.read_block(level3_blocks[last_blk_position], static_cast<uint8_t *>(buff) + g_wr_off, last_blk_read_size, 0);
        g_wr_off += last_blk_read_size;
    }
    assert_short(g_wr_off == size);
    return g_wr_off;
}

uint64_t filesystem::inode_t::write(const void * buff, uint64_t size, const uint64_t offset)
{
    auto header = unblocked_get_header();

    if (header.attributes.st_size == 0) {
        return 0;
    }

    if (offset > static_cast<uint64_t>(header.attributes.st_size)) {
        return 0;
    }

    if (offset + size > static_cast<uint64_t>(header.attributes.st_size)) {
        size = header.attributes.st_size - offset;
    }

    header.attributes.st_atim = header.attributes.st_ctim = header.attributes.st_mtim = get_current_time();
    unblocked_save_header(header);

    const auto level3_blocks = linearized_level3_pointers();

    const uint64_t first_blk_position = offset / block_size;
    const uint64_t first_blk_offset = offset % block_size;
    uint64_t first_blk_write_size = block_size - first_blk_offset;
    if (first_blk_write_size > size) first_blk_write_size = size;
    const uint64_t continuous_blks = (size - first_blk_write_size) / block_size;
    const uint64_t last_blk_position = first_blk_position + continuous_blks + 1;
    const uint64_t last_blk_write_size = (size - first_blk_write_size) % block_size;

    auto block_redirect = [&](const uint64_t block_id)->uint64_t
    {
        auto attr = fs.get_attr(block_id);
        auto new_attr = attr;
        // copy attributes
        const uint64_t target_first_block = fs.allocate_new_block();
        new_attr.frozen = 0;
        new_attr.links = 0;
        new_attr.cow_refresh_count = 0;
        new_attr.newly_allocated_thus_no_cow = 1;
        fs.set_attr(target_first_block, new_attr);

        // copy block data
        std::vector<uint8_t> data;
        data.resize(block_size);
        fs.read_block(block_id, data.data(), block_size, 0);
        fs.write_block(target_first_block, data.data(), block_size, 0, false);

        // redirect block
        redirect_3rd_level_block(block_id, target_first_block);
        if (!attr.frozen) // not frozen? set original as a COW block
        {
            attr.type_backup = attr.type;
            attr.type = COW_REDUNDANCY_TYPE;
            fs.set_attr(block_id, attr);
        }

        return target_first_block;
    };

    uint64_t g_wr_off = 0;
    // 1. write the first block
    const uint64_t target_first_block = block_redirect(level3_blocks[first_blk_position]);
    fs.write_block(target_first_block, buff, first_blk_write_size, first_blk_offset, false);
    g_wr_off += first_blk_write_size;

    // 2. write continuous blocks
    for (uint64_t i = 0; i < continuous_blks; i++) {
        const uint64_t blk_position = block_redirect(level3_blocks[first_blk_position + 1 + i]);
        fs.write_block(blk_position, static_cast<const uint8_t *>(buff) + g_wr_off, block_size, 0, false);
        g_wr_off += block_size;
    }

    if (last_blk_write_size) {
        fs.write_block(block_redirect(level3_blocks[last_blk_position]), static_cast<const uint8_t *>(buff) + g_wr_off, last_blk_write_size, 0, false);
        g_wr_off += last_blk_write_size;
    }

    assert_short(g_wr_off == size);
    return g_wr_off;
}

void filesystem::inode_t::level3::safe_delete(uint64_t & block_id)
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
}

uint64_t filesystem::inode_t::level3::mkblk(const bool storage)
{
    const auto new_block_id = fs.allocate_new_block();
    const cfs_blk_attr_t pointer_attributes = {
        .frozen = 0,
        .type = (storage ? STORAGE_TYPE : POINTER_TYPE),
        .type_backup = 0,
        .cow_refresh_count = 0,
        .newly_allocated_thus_no_cow = 1,
        .links = 1,
    };
    std::vector<uint8_t> block_data_empty;
    block_data_empty.resize(block_size);
    std::memset(block_data_empty.data(), 0, block_size);
    fs.write_block(new_block_id, block_data_empty.data(), block_size, 0, false);
    fs.set_attr(new_block_id, pointer_attributes); // update (still) as no-COW on new
    return new_block_id;
}

std::map < std::string, uint64_t > filesystem::directory_t::list_dentries()
{
    static_assert(sizeof(dentry_t) == CFS_MAX_FILENAME_LENGTH + sizeof(uint64_t));
    const auto [ attributes ] = get_header();
    const uint64_t dentry_count = attributes.st_size / sizeof(dentry_t);
    std::map < std::string, uint64_t > ret;
    for (uint64_t i = 0; i < dentry_count; i++) {
        dentry_t dentry{};
        inode_t::read(&dentry, sizeof(dentry), i * sizeof(dentry));
        ret.emplace(dentry.name, dentry.inode_id);
        // more often than not, you are reading attributes as well
        (void)fs.make_inode<inode_t>(dentry.inode_id).get_header();
    }

    return ret;
}

void filesystem::directory_t::save_dentries(const std::map < std::string, uint64_t > & dentries)
{
    auto original_dentries = list_dentries();
    // easy diff, detect appending
    bool appending = true;
    for (const auto & [name, inode] : original_dentries) {
        if (auto it = dentries.find(name); !(it != dentries.end() && it->second == inode)) {
            appending = false;
            break;
        }
    }

    resize(dentries.size() * sizeof(dentry_t));
    if (appending)
    {
        // figure out what is being appended
        std::vector<std::pair<const std::string, uint64_t>> new_dentries;
        for (const auto & [name, inode] : dentries) {
            if (auto it = original_dentries.find(name); it == original_dentries.end()) { // original cannot find this dentry
                new_dentries.emplace_back(name, inode);
            }
        }

        // only write new ones
        uint64_t offset = original_dentries.size() * sizeof(dentry_t);
        for (const auto & [name, inode] : new_dentries) {
            dentry_t dentry{};
            std::strncpy(dentry.name, name.c_str(), sizeof(dentry.name) - 1);
            dentry.inode_id = inode;
            offset += inode_t::write(&dentry, sizeof(dentry), offset);
        }
    }
    else // not appending? sorry, ganna refresh the whole thing
    {
        uint64_t offset = 0;
        for (const auto & [name, inode] : dentries) {
            dentry_t dentry{};
            std::strncpy(dentry.name, name.c_str(), sizeof(dentry.name) - 1);
            dentry.inode_id = inode;
            offset += inode_t::write(&dentry, sizeof(dentry), offset);
        }
    }
}

uint64_t filesystem::directory_t::get_inode(const std::string & name)
{
    try
    {
        auto children = list_dentries();
        if (const auto my_attr = get_inode_blk_attr(); !my_attr.frozen)
        {
            for (auto &child: children | std::views::values)
            {
                if (auto child_attr = fs.get_attr(child); child_attr.frozen && child_attr.frozen != 2) // duplicate frozen inode for all non-snapshots
                {
                    // copy data
                    std::vector<uint8_t> old_inode_data;
                    old_inode_data.resize(block_size);
                    fs.read_block(child, old_inode_data.data(), block_size, 0);
                    const auto new_inode = fs.allocate_new_block();
                    // update inode number
                    reinterpret_cast<inode_header_t *>(old_inode_data.data())->attributes.st_ino = new_inode;
                    fs.write_block(new_inode, old_inode_data.data(), block_size, 0, true);

                    // copy attribute
                    child_attr.frozen = 0;
                    fs.set_attr(new_inode, child_attr);

                    // increase link count for all blocks associated with the old inode
                    auto old_inode = fs.make_inode<inode_t>(child);
                    std::vector<uint64_t> blocks = old_inode.linearized_level2_pointers();
                    blocks.insert_range(blocks.end(), old_inode.linearized_level3_pointers());

                    for (const auto & block : blocks) {
                        auto attr = fs.block_manager->get_attr(block);
                        if (attr.links < 127) attr.links++;
                        fs.block_manager->set_attr(block, attr);
                    }

                    debug_log("Inode duplicated due to frozen inode, inode ", child, ", new inode ", new_inode, ", parent ", get_header().attributes.st_ino);
                    child = new_inode;
                    // update dentry
                }
            }

            save_dentries(children);
        }
        return children.at(name);
    } catch (const std::out_of_range &) {
        throw fs_error::no_such_file_or_directory("");
    }
}

void filesystem::directory_t::unlink_inode(const std::string & name)
{
    auto list = list_dentries();
    const auto inode_id = get_inode(name);
    list.erase(name);
    save_dentries(list);
    auto inode = fs.make_inode<inode_t>(inode_id);
    inode.unlink_self();
}

void filesystem::directory_t::reset_as(const std::string & name)
{
    if (inode_id != 0) {
        throw fs_error::operation_bot_permitted("Cannot recover snapshots on non-root inodes");
    }

    if (auto fs_header = fs.block_manager->get_header();
        fs_header.runtime_info.snapshot_number == 0)
    {
        throw fs_error::operation_bot_permitted("No snapshots found");
    }

    auto snapshot_root_id = get_inode(name);
    auto snapshot_inode = fs.make_inode<inode_t>(snapshot_root_id);

    // get all dentries
    std::vector < std::pair < std::string, uint64_t > > snapshot_roots;
    // check which ones are snapshots
    for (auto dentries = list_dentries();
        const auto & [name, inode] : dentries)
    {
        if (fs.get_attr(inode).frozen == 2) {
            snapshot_roots.emplace_back(name, inode);
        }
    }

    // free all non-redundancy blocks without COW
    fs.reset();

    // copy over
    std::vector<uint8_t> snapshot_inode_data;
    snapshot_inode_data.resize(block_size);
    fs.read_block(snapshot_root_id, snapshot_inode_data.data(), block_size, 0);
    fs.write_block(0, snapshot_inode_data.data(), block_size, 0, false);

    // update volume root inode header with proper info
    auto [ root_inode_from_snapshot_data ] = snapshot_inode.get_header();
    root_inode_from_snapshot_data.st_ino = 0;
    root_inode_from_snapshot_data.st_mode |= 0x80; // recover write attributes
    save_header(inode_header_t{ .attributes = root_inode_from_snapshot_data });

    // recover previous snapshot roots, should name conflict, rename it
    auto new_root_dentries = this->list_dentries(); // now that data are reverted to previous inode, dentries will be updated
    for (auto & [name, inode] : snapshot_roots)
    {
        if (new_root_dentries.contains(name) && new_root_dentries.at(name) != inode) { // dentry exists but different inode
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> distrib(0, INT32_MAX);
            for (uint64_t i = 0; i < 4096; i++)
            {
                uint64_t seed = distrib(gen);
                uint64_t certy = hashcrc64(seed);
                std::stringstream ss;
                ss << "_" << std::setw(16) << std::setfill('0') << std::hex << certy << "_" << std::dec << inode;
                const std::string new_name = name + ss.str();
                if (!new_root_dentries.contains(new_name)) {
                    new_root_dentries.emplace(new_name, inode); // renamed inode
                    break;
                }
            }
        }
        else if (!new_root_dentries.contains(name)) { // no such entry
            new_root_dentries.emplace(name, inode); // add it
        }
    }
    this->save_dentries(new_root_dentries);

    // set new root block attributes
    auto snapshot_block_attr = fs.get_attr(snapshot_root_id);
    snapshot_block_attr.frozen = 0;
    snapshot_block_attr.links = 1;
    fs.set_attr(0, snapshot_block_attr);
}

void filesystem::directory_t::snapshot(const std::string & name)
{
    if (inode_id != 0) {
        throw fs_error::operation_bot_permitted("Creating snapshot on non-root inode");
    }

    if (list_dentries().contains(name)) {
        throw fs_error::inode_exists("name exists");
    }

    auto fs_header = fs.block_manager->get_header();
    if (fs_header.runtime_info.snapshot_number >= 127) {
        throw fs_error::operation_bot_permitted("Max snapshot volume number reached");
    }

    // create a new root
    auto new_snapshot_vol = create_dentry(name, S_IFDIR | 0555);
    auto [ new_root_inode_header ] = new_snapshot_vol.get_header();
    const auto new_root_inode_id = new_root_inode_header.st_ino;
    auto new_root = fs.make_inode<directory_t>(new_root_inode_id);

    // copy over
    std::vector<uint8_t> old_inode_data;
    old_inode_data.resize(block_size);
    fs.read_block(0, old_inode_data.data(), block_size, 0);
    fs.write_block(new_root_inode_id, old_inode_data.data(), block_size, 0, false);

    // update volume root inode header with proper info
    auto [ old_root_inode_header ] = this->get_header();
    new_root_inode_header = old_root_inode_header;
    new_root_inode_header.st_ino = new_root_inode_id;
    new_root_inode_header.st_mode &= 0xFFFFFF6D; // strip write attributes
    new_root.save_header(inode_header_t{ .attributes = new_root_inode_header });

    // set new root block attributes
    auto new_snapshot_vol_attr = fs.get_attr(new_root_inode_id);
    new_snapshot_vol_attr.frozen = 2;
    fs.set_attr(new_root_inode_id, new_snapshot_vol_attr);

    // update filesystem header
    fs_header.runtime_info.snapshot_number++;
    fs_header.runtime_info.snapshot_number_dup = fs_header.runtime_info.snapshot_number_dup2 = fs_header.runtime_info.snapshot_number_dup3;
    fs.block_io->update_runtime_info(fs_header);

    // freeze
    fs.freeze_block();
}

filesystem::inode_t filesystem::directory_t::create_dentry(const std::string & name, const mode_t mode)
{
    if (list_dentries().contains(name)) {
        throw fs_error::inode_exists("name exists");
    }

    dentry_t dentry{};
    std::strncpy(dentry.name, name.c_str(), CFS_MAX_FILENAME_LENGTH - 1);
    dentry.inode_id = fs.allocate_new_block();

    // create new attribute
    constexpr cfs_blk_attr_t attr = {
        .frozen = 0,
        .type = INDEX_TYPE,
        .type_backup = 0,
        .cow_refresh_count = 0,
        .newly_allocated_thus_no_cow = 1,
        .links = 1,
    };
    fs.set_attr(dentry.inode_id, attr);

    // clear inode area
    std::vector<uint8_t> data;
    data.resize(fs.block_manager->block_size);
    std::memset(data.data(), 0, fs.block_manager->block_size);
    fs.write_block(dentry.inode_id, data.data(), fs.block_manager->block_size, 0, false);

    auto new_inode = fs.make_inode<inode_t>(dentry.inode_id);
    // create a new inode header
    inode_header_t inode_header {};
    inode_header.attributes.st_atim = inode_header.attributes.st_ctim = inode_header.attributes.st_mtim = get_current_time();
    inode_header.attributes.st_blksize = static_cast<long>(fs.block_manager->block_size);
    inode_header.attributes.st_nlink = 1;
    inode_header.attributes.st_mode = mode;
    inode_header.attributes.st_gid = getgid();
    inode_header.attributes.st_uid = getuid();
    inode_header.attributes.st_ino = dentry.inode_id;
    new_inode.save_header(inode_header);

    debug_log("Index node created at inode ID ", dentry.inode_id, ", name ", dentry.name, ", under ", get_header().attributes.st_ino);

    // add new dentry to dentry list
    auto dir = list_dentries();
    dir.emplace(dentry.name, dentry.inode_id);
    save_dentries(dir);
    return inode_t{fs, dentry.inode_id, fs.block_manager->block_size};
}
