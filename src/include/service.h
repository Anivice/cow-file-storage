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
#include <sys/statvfs.h>
#include "service.h"
#include "helper/log.h"
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
    MAKE_ERROR_TYPE(not_a_directory);
    MAKE_ERROR_TYPE(is_a_directory);
    MAKE_ERROR_TYPE(operation_bot_permitted);
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
    void clear_frozen_all();
    void revert_transaction();
    void delink_block(uint64_t data_field_block_id);
    void unblocked_delink_block(uint64_t data_field_block_id);

public:
    class inode_t
    {
    protected:
        filesystem & fs;
        const uint64_t inode_id; // data block id for inode
        const uint64_t block_size;
        const uint64_t inode_level_pointers;
        const uint64_t block_max_entries;

    public:
        static timespec get_current_time()
        {
            timespec ts{};
            timespec_get(&ts, TIME_UTC);
            return ts;
        }

        [[nodiscard]] cfs_blk_attr_t get_inode_blk_attr() const { return fs.get_attr(inode_id); }

    private:
        class level3 {
        public:
            filesystem & fs;
            uint64_t data_field_block_id;
            bool control_active = false;
            const uint64_t block_size;
            bool newly_created = false;

            void safe_delete(uint64_t & block_id);
            uint64_t mkblk(const bool storage = false);

            explicit level3(const uint64_t data_field_block_id,
                filesystem & fs,
                const bool control_active,
                const uint64_t block_size, const bool storage = false)
                : fs(fs), data_field_block_id(data_field_block_id), control_active(control_active), block_size(block_size)
            {
                if (control_active && fs.get_attr(data_field_block_id).frozen)
                {
                    // auto create new pointer
                    const auto new_ptr_id = mkblk(storage);
                    std::vector<uint8_t> ptr_data;
                    ptr_data.resize(block_size);
                    fs.read_block(data_field_block_id, ptr_data.data(), block_size, 0);
                    fs.write_block(new_ptr_id, ptr_data.data(), block_size, 0);
                    this->data_field_block_id = new_ptr_id;
                }
            }

            explicit level3(filesystem & fs, const bool control_active, const uint64_t block_size, const bool storage = false)
                : fs(fs), control_active(control_active), block_size(block_size)
            {
                if (control_active) {
                    data_field_block_id = mkblk(storage);
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

    public:
        struct inode_header_t {
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

    public:
        std::vector<uint64_t> linearized_level3_pointers();
        std::vector<uint64_t> linearized_level2_pointers();
        explicit inode_t(filesystem & fs, uint64_t inode_id, uint64_t block_size);
        uint64_t read(void *buff, uint64_t size, uint64_t offset);
        uint64_t write(const void * buff, uint64_t size, uint64_t offset);
        [[nodiscard]] inode_header_t get_header();
        void save_header(const inode_header_t & header);
        void resize(uint64_t new_size);
        void unlink_self();
    };

    class file_t final : public inode_t {
    public:
        explicit file_t(filesystem & fs, const uint64_t inode_id, const uint64_t block_size) : inode_t(fs, inode_id, block_size) { }
    };

    class directory_t final : public inode_t {
    private:
        struct dentry_t {
            char name[CFS_MAX_FILENAME_LENGTH];
            uint64_t inode_id;
        };

    public:
        explicit directory_t(filesystem & fs, const uint64_t inode_id, const uint64_t block_size) : inode_t(fs, inode_id, block_size)
        {
            if (const auto &[attributes] = get_header();
                !(attributes.st_mode & S_IFDIR))
            {
                throw fs_error::not_a_directory("");
            }
        }

        std::map < std::string, uint64_t > list_dentries();
        void save_dentries(const std::map < std::string, uint64_t > & dentries);
        inode_t create_dentry(const std::string & name, mode_t mode);
        uint64_t get_inode(const std::string & name);
        void unlink_inode(const std::string & name);
        void snapshot(const std::string & name);
    };

    std::unique_ptr < directory_t > get_root() {
        return std::make_unique<directory_t>(*this, 0, block_manager->block_size);
    }

    template < typename InodeType >
    requires (std::is_same_v<InodeType, directory_t> || std::is_same_v<InodeType, inode_t> || std::is_same_v<InodeType, file_t>)
    InodeType make_inode(const uint64_t data_field_block_id)
    {
        if (block_manager->block_allocated(data_field_block_id)) {
            if (const auto attr = block_manager->get_attr(data_field_block_id); attr.type == INDEX_TYPE) {
                return InodeType{*this, data_field_block_id, block_manager->block_size};
            }
        }

        throw runtime_error("Invalid block query " + std::to_string(data_field_block_id));
    }

    void sync();
    struct statvfs fstat();
    void release_all_frozen_blocks();
    explicit filesystem(const char * location);
    ~filesystem();
};

#endif //SERVICE_H
