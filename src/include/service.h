/* service.h
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

#ifndef SERVICE_H
#define SERVICE_H

#include <memory>
#include <sys/stat.h>
#include "core/blk_manager.h"
#if DEBUG
# include "helper/err_type.h"
#endif

#if DEBUG
# define MAKE_ERROR_TYPE(name)                                              \
class name final                                                            \
    : public runtime_error                                                  \
{                                                                           \
    public:                                                                 \
    explicit name(const std::string & init_msg_)                            \
        : runtime_error(std::string(#name) + " " + init_msg_)               \
    {                                                                       \
    }                                                                       \
};
#else
# define MAKE_ERROR_TYPE(name)                                              \
class name final                                                            \
    : public std::exception                                                 \
{                                                                           \
    std::string what_;                                                      \
    public:                                                                 \
    explicit name(...)                                                      \
    {                                                                       \
        what_ = #name;                                                      \
    }                                                                       \
                                                                            \
    const char * what() const noexcept {                                    \
        return what_.c_str();                                               \
    }                                                                       \
};
#endif

namespace fs_error {
    MAKE_ERROR_TYPE(cannot_open_disk);
    MAKE_ERROR_TYPE(filesystem_block_mapping_init_error);
    MAKE_ERROR_TYPE(filesystem_block_manager_init_error);
    MAKE_ERROR_TYPE(filesystem_space_depleted);
    MAKE_ERROR_TYPE(filesystem_frozen_block_protection);
    MAKE_ERROR_TYPE(unknown_error);
}

#undef MAKE_ERROR_TYPE

#define CFS_MAX_FILENAME_LENGTH 128

class filesystem
{
    basic_io_t basic_io;
    std::unique_ptr < block_io_t > block_io;
    std::unique_ptr < blk_manager > block_manager;
    std::mutex mutex;

    uint64_t unblocked_allocate_new_block();
    void unblocked_deallocate_block(uint64_t data_field_block_id);
    uint64_t unblocked_read_block(uint64_t data_field_block_id, void * buff, uint64_t size, uint64_t offset);
    uint64_t unblocked_write_block(uint64_t data_field_block_id, const void * buff, uint64_t size, uint64_t offset);

    uint64_t allocate_new_block() {
        std::lock_guard<std::mutex> lock(mutex);
        return unblocked_allocate_new_block();
    }

    void deallocate_block(const uint64_t data_field_block_id) {
        std::lock_guard<std::mutex> lock(mutex);
        unblocked_deallocate_block(data_field_block_id);
    }

    uint64_t read_block(const uint64_t data_field_block_id, void * buff, const uint64_t size, const uint64_t offset) {
        std::lock_guard<std::mutex> lock(mutex);
        return unblocked_read_block(data_field_block_id, buff, size, offset);
    }

    uint64_t write_block(uint64_t data_field_block_id, const void * buff, const uint64_t size, const uint64_t offset) {
        std::lock_guard<std::mutex> lock(mutex);
        return unblocked_write_block(data_field_block_id, buff, size, offset);
    }

    cfs_blk_attr_t get_attr(uint64_t data_field_block_id);
    void set_attr(uint64_t data_field_block_id, cfs_blk_attr_t attr);
    void freeze_block();
    void clear_frozen_but_1();
    void clear_frozen_all();
    void revert_transaction();

    class inode_t
    {
        filesystem & fs;
        const uint64_t inode_id; // data block id for inode
        const uint64_t block_size;
        std::mutex mutex;

        struct inode_header_t {
            char name[CFS_MAX_FILENAME_LENGTH];
            struct stat attributes;
        };

        inode_header_t get_header();
        void save_header(inode_header_t);
        std::vector < uint64_t > get_block_pointers();
        void save_block_pointers(const std::vector < uint64_t > & block_pointers);

    public:
        explicit inode_t(filesystem & fs, uint64_t inode_id, uint64_t block_size);
        void resize(uint64_t block_size);
        void read(void *buff, uint64_t offset, uint64_t size);
        void write(const void * buff, uint64_t offset, uint64_t size);
        void rename(const char * new_name);
        void remove(); // remove current inode, works for empty dir and other file types
    };

public:

    inode_t get_inode_by_path(const std::string & path);
    explicit filesystem(const char * location);
    ~filesystem();
};

#endif //SERVICE_H
