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

#include <filesystem>
#include <memory>
#include <sys/stat.h>
#include "core/blk_manager.h"
#include "helper/cpp_assert.h"
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
    MAKE_ERROR_TYPE(no_such_file_or_directory);
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

    uint64_t write_block(uint64_t data_field_block_id, const void * buff, uint64_t size, const uint64_t offset)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return unblocked_write_block(data_field_block_id, buff, size, offset);
    }

    cfs_blk_attr_t get_attr(uint64_t data_field_block_id);
    void set_attr(uint64_t data_field_block_id, cfs_blk_attr_t attr);
    void freeze_block();
    void clear_frozen_but_1();
    void clear_frozen_all();
    void revert_transaction();
    void delink_block(uint64_t data_field_block_id);

    class inode_t
    {
    protected:
        filesystem & fs;

    private:
        const uint64_t inode_id; // data block id for inode
        const uint64_t block_size;
        const uint64_t inode_level_pointers;
        const uint64_t block_max_entries;
        std::vector<uint64_t> block_pointers;

    protected:
        static timespec get_current_time()
        {
            timespec ts{};
            timespec_get(&ts, TIME_UTC);
            return ts;
        }

    private:
        class level3 {
        public:
            filesystem & fs;
            uint64_t data_field_block_id;
            bool control_active = false;
            const uint64_t block_size;
            bool newly_created = false;

            void safe_delete(uint64_t & block_id);
            uint64_t mkblk();

            explicit level3(const uint64_t data_field_block_id, filesystem & fs, const bool control_active, const uint64_t block_size)
                : fs(fs), data_field_block_id(data_field_block_id), control_active(control_active), block_size(block_size) { }
            explicit level3(filesystem & fs, const bool control_active, const uint64_t block_size)
                : fs(fs), control_active(control_active), block_size(block_size)
            {
                if (control_active) {
                    data_field_block_id = mkblk();
                }
            }

            ~level3()
            {
                if (control_active) {
                    safe_delete(data_field_block_id);
                }
            }
        };

        std::mutex mutex;

    protected:
        struct inode_header_t {
            char name[CFS_MAX_FILENAME_LENGTH];
            struct stat attributes;
        };

    private:
        inode_header_t unblocked_get_header();
        void unblocked_save_header(inode_header_t);
        std::vector < uint64_t > get_inode_block_pointers(); /// get pointers inside inode (level 1 pointers)
        void save_inode_block_pointers(const std::vector < uint64_t > & block_pointers); /// save pointers to inode (level 1 pointers)
        std::vector < uint64_t > get_pointer_by_block(uint64_t data_field_block_id);
        void save_pointer_to_block(uint64_t data_field_block_id, const std::vector < uint64_t > & block_pointers);

        void unblocked_resize(uint64_t file_length);
        void redirect_3rd_level_block(uint64_t old_data_field_block_id, uint64_t new_data_field_block_id);
        std::vector<uint64_t> linearized_level3_pointers();
        std::vector<uint64_t> linearized_level2_pointers();

    public:
        explicit inode_t(filesystem & fs, uint64_t inode_id, uint64_t block_size);
        uint64_t read(void *buff, uint64_t size, uint64_t offset);
        uint64_t write(const void * buff, uint64_t size, uint64_t offset);
        inode_header_t get_header();
        void save_header(const inode_header_t & header);
        void resize(uint64_t new_size);
    };

public:
    class file_t : public inode_t {
    public:
        explicit file_t(filesystem & fs, const uint64_t inode_id, const uint64_t block_size) : inode_t(fs, inode_id, block_size) { }
    };

    class directory_t : private inode_t {
    private:
        struct dentry_t {
            char name[CFS_MAX_FILENAME_LENGTH];
            uint64_t inode_id;
        };

    public:
        explicit directory_t(filesystem & fs, const uint64_t inode_id, const uint64_t block_size) : inode_t(fs, inode_id, block_size) { }
        std::map < std::string, uint64_t > list_dentries();
        void save_dentries(const std::map < std::string, uint64_t > & dentries);
        inode_t create_dentry(const std::string & name, const mode_t mode);
        inode_t get_inode(const std::string & name);
    };

    directory_t get_root() {
        return directory_t{*this, 0, block_manager->block_size};
    }

    explicit filesystem(const char * location);
    ~filesystem();
};

#endif //SERVICE_H
