#ifndef BLK_MANAGER_T_H
#define BLK_MANAGER_T_H

#include <memory>
#include "core/journal.h"
#include "core/bitmap.h"
#include "core/block_attr.h"

class filesystem;

/// low level filesystem block manager
class blk_manager
{
private:
    block_io_t & blk_mapping;                       /// block mapping

public:
    const uint64_t blk_count = 0;                   /// block count
    const uint64_t block_size = 0;                  /// block size
    const uint64_t data_field_block_start = 0;
    const uint64_t data_field_block_end = 0;

private:
    std::unique_ptr < journaling > journal;         /// journaling
    std::unique_ptr < bitmap > block_bitmap;        /// bitmap
    std::unique_ptr < bitmap > block_bitmap_mirror; /// bitmap mirror
    std::unique_ptr < block_attr_t > block_attr;    /// block attributes

public:
    cfs_head_t get_header();    /// read header from disk

private:
    /*!
     * Get allocation state
     * @param index Data field block id
     * @return Allocation state
     */
    [[nodiscard]] uint64_t bitget(const uint64_t index) const {
        // FIXME: discrepancy between maps autofix
        return block_bitmap->get(index);
    }

    /*!
     * Set Allocation state
     * @param index Data field block id
     * @param value New state
     */
    void bitset(uint64_t index, bool value);

public:
    class no_space_available final : std::exception { };    /// Filesystem is running out of space
    explicit blk_manager(block_io_t & block_io);
    uint64_t allocate_block(); /// allocate block
    cfs_blk_attr_t get_attr(uint64_t index); /// get block attributes
    void set_attr(uint64_t index, cfs_blk_attr_t val); /// set block attributes
    bool block_allocated(const uint64_t index) { return bitget(index); }
    uint64_t free_blocks() { const auto hd = get_header(); return blk_count - hd.runtime_info.allocated_blocks; } /// get how many blocks are free from header into

    /// deallocate a block
    /// @param block Target block
    void free_block(uint64_t block);

    [[nodiscard]] block_io_t::safe_block_t safe_get_block(const uint64_t block)
    {
        if (get_attr(block).frozen) {
            return blk_mapping.safe_at(data_field_block_start + block, true);
        }
        return blk_mapping.safe_at(data_field_block_start + block);
    }

    friend class filesystem;
};

#endif //BLK_MANAGER_T_H
