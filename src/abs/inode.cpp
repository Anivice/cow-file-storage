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

filesystem::inode_t::inode_header_t filesystem::inode_t::unblocked_get_header()
{
    inode_header_t header{};
    fs.read_block(inode_id, &header, sizeof(header), 0);
    return header;
}

void filesystem::inode_t::unblocked_save_header(const inode_header_t header)
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

filesystem::inode_t::inode_header_t filesystem::inode_t::get_header() {
    std::lock_guard<std::mutex> guard(mutex);
    return unblocked_get_header();
}

void filesystem::inode_t::save_header(const filesystem::inode_t::inode_header_t & header) {
    std::lock_guard<std::mutex> guard(mutex);
    unblocked_save_header(header);
}

void filesystem::inode_t::resize(const uint64_t new_size) {
    std::lock_guard<std::mutex> guard(mutex);
    unblocked_resize(new_size);
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
    fs.write_block(data_field_block_id, block_pointers.data(), block_pointers.size() * sizeof(uint64_t), 0);
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
        level3s_[i] = std::make_unique<level3>(actual_level3s[i], fs, true, block_size);
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
            level3 = std::make_unique<class level3>(fs, true, block_size);
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
                    bool found = false;
                    auto lv3_blks = get_pointer_by_block(lv2_blk);
                    for (auto & lv3_blk : lv3_blks)
                    {
                        if (lv3_blk != 0 && lv3_blk == old_data_field_block_id) {
                            lv3_blk = new_data_field_block_id;
                            found = true;
                            break;
                        }
                    }

                    if (found) {
                        save_pointer_to_block(lv2_blk, lv3_blks);
                        return;
                    }
                }
            }
        }
    }

    debug_log("ERROR: Redirect block pointer from ", old_data_field_block_id, " to ", new_data_field_block_id,
        " failed: No such block pointer in third level pointer map.");
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
    std::lock_guard<std::mutex> guard(mutex);
    auto header = unblocked_get_header();
    if (offset > static_cast<uint64_t>(header.attributes.st_size)) {
        return 0;
    }

    if ((offset + size) > static_cast<uint64_t>(header.attributes.st_size)) {
        size = header.attributes.st_size - offset;
    }

    header.attributes.st_atim = get_current_time();
    unblocked_save_header(header);

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
    std::lock_guard<std::mutex> guard(mutex);
    auto header = unblocked_get_header();
    if (offset > static_cast<uint64_t>(header.attributes.st_size)) {
        return 0;
    }

    if (offset + size > static_cast<uint64_t>(header.attributes.st_size)) {
        size = header.attributes.st_size - offset;
    }

    header.attributes.st_mtim = get_current_time();
    header.attributes.st_atim = header.attributes.st_mtim;
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
        uint64_t target_first_block = block_id;
        if (attr.frozen)
        {
            // copy attributes
            target_first_block = fs.allocate_new_block();
            attr.frozen = 0;
            attr.links = 0;
            attr.cow_refresh_count = 0;
            fs.set_attr(target_first_block, attr);

            // copy block data
            std::vector<uint8_t> data;
            data.resize(block_size);
            fs.read_block(block_id, data.data(), block_size, 0);
            fs.write_block(target_first_block, data.data(), block_size, 0);

            // redirect block
            redirect_3rd_level_block(block_id, target_first_block);
        }

        return target_first_block;
    };

    uint64_t g_wr_off = 0;
    // 1. write first block
    const uint64_t target_first_block = block_redirect(level3_blocks[first_blk_position]);
    fs.write_block(target_first_block, buff, first_blk_write_size, first_blk_offset);
    g_wr_off += first_blk_write_size;

    // 2. write continuous blocks
    for (uint64_t i = 0; i < continuous_blks; i++) {
        const uint64_t blk_position = block_redirect(level3_blocks[first_blk_position + 1 + i]);
        fs.write_block(blk_position, static_cast<const uint8_t *>(buff) + g_wr_off, block_size, 0);
        g_wr_off += block_size;
    }

    if (last_blk_write_size) {
        fs.write_block(block_redirect(level3_blocks[last_blk_position]), static_cast<const uint8_t *>(buff) + g_wr_off, last_blk_write_size, 0);
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

uint64_t filesystem::inode_t::level3::mkblk()
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
}

std::map < std::string, uint64_t > filesystem::directory_t::list_dentries()
{
    static_assert(sizeof(dentry_t) == CFS_MAX_FILENAME_LENGTH + sizeof(uint64_t));
    const auto header = get_header();
    const uint64_t dentry_count = header.attributes.st_size / sizeof(dentry_t);
    std::map < std::string, uint64_t > ret;
    for (uint64_t i = 0; i < dentry_count; i++) {
        dentry_t dentry{};
        inode_t::read(&dentry, sizeof(dentry), i * sizeof(dentry));
        ret.emplace(dentry.name, dentry.inode_id);
    }

    return ret;
}

void filesystem::directory_t::save_dentries(const std::map < std::string, uint64_t > & dentries)
{
    resize(dentries.size() * sizeof(dentry_t));
    uint64_t offset = 0;
    for (const auto & [name, inode] : dentries) {
        dentry_t dentry{};
        std::strncpy(dentry.name, name.c_str(), sizeof(dentry.name) - 1);
        dentry.inode_id = inode;
        offset += inode_t::write(&dentry, sizeof(dentry), offset);
    }
}

filesystem::inode_t filesystem::directory_t::get_inode(const std::string & name)
{
    try {
        return inode_t{fs, list_dentries()[name], fs.block_manager->block_size};
    } catch (const std::out_of_range &) {
        throw fs_error::no_such_file_or_directory("");
    }
}

filesystem::inode_t filesystem::directory_t::create_dentry(const std::string & name, const mode_t mode)
{
    dentry_t dentry{};
    std::strncpy(dentry.name, name.c_str(), CFS_MAX_FILENAME_LENGTH - 1);
    dentry.inode_id = fs.allocate_new_block();

    // create new attribute
    constexpr cfs_blk_attr_t attr = {
        .frozen = 0,
        .type = INDEX_TYPE,
        .type_backup = 0,
        .cow_refresh_count = 0,
        .links = 1,
    };
    fs.set_attr(dentry.inode_id, attr);

    // clear inode area
    std::vector<uint8_t> data;
    data.resize(fs.block_manager->block_size);
    std::memset(data.data(), 0, fs.block_manager->block_size);
    fs.write_block(dentry.inode_id, data.data(), fs.block_manager->block_size, 0);

    // create a new inode header
    inode_header_t inode_header {};
    std::strncpy(inode_header.name, name.c_str(), CFS_MAX_FILENAME_LENGTH - 1);
    inode_header.attributes.st_atim = inode_header.attributes.st_ctim = inode_header.attributes.st_mtim = get_current_time();
    inode_header.attributes.st_blksize = static_cast<long>(fs.block_manager->block_size);
    inode_header.attributes.st_nlink = 1;
    inode_header.attributes.st_mode = mode;
    save_header(inode_header);

    // add new dentry to dentry list
    auto dir = list_dentries();
    dir.emplace(dentry.name, dentry.inode_id);
    save_dentries(dir);

    return inode_t{fs, dentry.inode_id, fs.block_manager->block_size};
}
