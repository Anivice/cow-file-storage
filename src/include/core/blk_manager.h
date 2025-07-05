#ifndef BLK_MANAGER_T_H
#define BLK_MANAGER_T_H

#include <memory>
#include "core/journal.h"
#include "core/crc64sum.h"
#include "core/bitmap.h"
#include "core/block_attr.h"

/// low level filesystem block manager
class blk_manager
{
    block_io_t & blk_mapping;                       /// block mapping
    const uint64_t blk_count = 0;                   /// block count
    const uint64_t block_size = 0;                  /// block size
    std::unique_ptr < journaling > journal;         /// journaling
    std::unique_ptr < bitmap > block_bitmap;        /// bitmap
    std::unique_ptr < bitmap > block_bitmap_mirror; /// bitmap mirror
    std::unique_ptr < block_attr_t > block_attr;    /// block attributes
    std::mutex mutex;           /// operation mutex
    cfs_head_t get_header();    /// read header from disk

    /// update provided header with freshly calculated bitmap hash
    /// @param cfs_head header
    void update_bitmap_hash(cfs_head_t & cfs_head);

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
    uint64_t free_blocks() { const auto hd = get_header(); return blk_count - hd.runtime_info.allocated_blocks; } /// get how many blocks are free from header into
    /// deallocate a block
    /// @param block Target block
    void free_block(uint64_t block);
    /// hash a block and update the info in attributes
    /// @param block Block to hash
    void hash_block(uint64_t block);

    class block_data_t {
        block_io_t::block_data_t & data_;       /// data block pointer
        const uint64_t block_size;              /// block size
        const uint64_t data_block_id;           /// block id in data field
        std::mutex mutex_;                      /// R/W mutex lock
        explicit block_data_t(block_io_t::block_data_t & data, const uint64_t block_size, const uint64_t data_block_id)
            : data_(data), block_size(block_size), data_block_id(data_block_id) {}

    public:
        void read(void * data, uint64_t size, uint64_t offset);
        void write(const void * data, uint64_t size, uint64_t offset);
        friend class blk_manager;
    };
};

#endif //BLK_MANAGER_T_H
