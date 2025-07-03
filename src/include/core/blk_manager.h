#ifndef BLK_MANAGER_T_H
#define BLK_MANAGER_T_H

#include <memory>
#include "core/journal.h"
#include "core/crc64sum.h"
#include "core/bitmap.h"
#include "core/block_attr.h"

class blk_manager {
    block_io_t & blk_mapping;
    const uint64_t blk_count = 0;
    std::unique_ptr < journaling > journal;
    std::unique_ptr < bitmap > block_bitmap;
    std::unique_ptr < bitmap > block_bitmap_mirror;
    std::unique_ptr < block_attr_t > block_attr;
    std::mutex mutex;
    cfs_head_t get_header();
    void update_bitmap_hash(cfs_head_t & cfs_head);
    // FIXME: discrepancy between maps autofix
    [[nodiscard]] uint64_t bitget(const uint64_t index) const { return block_bitmap->get(index); }
    void bitset(const uint64_t index, const bool value)
    {
        block_bitmap->set(index, value);
        block_bitmap_mirror->set(index, value);
    }

public:
    class no_space_available final : std::exception { };
    explicit blk_manager(block_io_t & block_io);
    uint64_t allocate_block();
    block_io_t::block_data_t & at(const uint64_t index) { return blk_mapping.at(get_header().static_info.data_table_start + index); }
    cfs_blk_attr_t get_attr(const uint64_t index) {
        std::lock_guard lock(mutex);
        auto ret = block_attr->get(index);
        return *reinterpret_cast<cfs_blk_attr_t *>(&ret);
    }
    void set_attr(const uint64_t index, const cfs_blk_attr_t val) { std::lock_guard lock(mutex); return block_attr->set(index, *(const uint16_t*)&val); }
    uint64_t free_blocks() { const auto hd = get_header(); return blk_count - hd.runtime_info.allocated_blocks; }
    void free_block(uint64_t block);
};

#endif //BLK_MANAGER_T_H
