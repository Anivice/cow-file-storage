#ifndef SERVICE_H
#define SERVICE_H

#include <memory>
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
    uint64_t unblocked_write_block(uint64_t data_field_block_id, void * buff, uint64_t size, uint64_t offset);

public:
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

    uint64_t write_block(uint64_t data_field_block_id, void * buff, const uint64_t size, const uint64_t offset) {
        std::lock_guard<std::mutex> lock(mutex);
        return unblocked_write_block(data_field_block_id, buff, size, offset);
    }

    cfs_blk_attr_t get_attr(uint64_t data_field_block_id);
    void set_attr(uint64_t data_field_block_id, cfs_blk_attr_t attr);
    void freeze_block();
    void clear_frozen_but_1();
    void clear_frozen_all();

    class inode_t {
        blk_manager & block_mgr;
        const uint64_t inode_block_number;

    public:
        explicit inode_t(blk_manager & block_mgr_, const uint64_t inode_block_number_)
            : block_mgr(block_mgr_), inode_block_number(inode_block_number_) { }
    };

    void revert_transaction();
    explicit filesystem(const char * location);
    ~filesystem();
};

#endif //SERVICE_H
