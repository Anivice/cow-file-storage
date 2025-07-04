#ifndef BLOCK_IO_H
#define BLOCK_IO_H

#include <atomic>
#include <map>
#include <vector>
#include "core/basic_io.h"
#include "core/cfs.h"

/*!
 * @brief block_io_t is an abstraction layer operating on block instead of 512 byte sectors, and offer a in-memory cache
 */
class block_io_t {
public:
    /// public data block pointer, this element points to a specific block, and write to disk(sync) on deleting.
    class block_data_t;

private:
    /// block_data_t construction helper, to conseal details within the class member
    class block_data_ptr_t {
        block_data_t * ptr; /// block pointer
    public:
        explicit block_data_ptr_t(const uint64_t blk_sz, const uint64_t block_sector_start_,
            const uint64_t block_sector_end_, basic_io_t & io_)
            { ptr = new block_data_t(blk_sz, block_sector_start_, block_sector_end_, io_); }
        ~block_data_ptr_t() { delete ptr; }
        block_data_t * operator->() const { return ptr; }
        block_data_t & operator*() const { return *ptr; }
    };

public:
    class block_data_t
    {
        std::mutex mutex{};                 /// R/W mutes
        std::vector<uint8_t> data_;         /// in memory cache data
        const uint64_t block_sector_start;  /// on-disk sector start [start, end)
        const uint64_t block_sector_end;    /// on-disk sector end [start, end)
        bool read_only{false};              /// disable alteration to this block
        bool out_of_sync = false;           /// if data changed in memory but not reflected onto file
        bool in_use{false};                 /// block is in use, set after being thrown out by at, needs manual cleaning. cache won't delete in-use blocks
        basic_io_t & io;                    /// basic IO
        explicit block_data_t(const uint64_t block_size, const uint64_t block_sector_start_,
            const uint64_t block_sector_end_, basic_io_t & io_)
            : block_sector_start(block_sector_start_), block_sector_end(block_sector_end_), io(io_)
        { data_.resize(block_size); }
        ~block_data_t() { sync(); }         /// sync on destruction

    public:
        [[nodiscard]] size_t size() const { return data_.size(); }  /// data size, should always be block size
        void get(uint8_t * buf, size_t sz, uint64_t in_blk_off);    /// read data
        void update(const uint8_t * new_data, size_t new_size, uint64_t in_block_offset);   /// update cache
        void sync();                        /// write to disk
        bool is_out_of_sync() const { return out_of_sync; } /// check if update() is called
        void not_in_use() { std::lock_guard lock(mutex); in_use = false; }
        friend class block_io_t;
    };

    class safe_block_t {
        block_data_t & block;

    public:
        block_data_t & get() const { return block; }
        block_data_t * operator->() const { return &block; }
        explicit safe_block_t(block_data_t & block_) : block(block_) {}
        ~safe_block_t() { block.not_in_use(); }
    };

private:
    basic_io_t & io;                        /// basic IO
    cfs_head_t cfs_head{};                  /// in memory head
    std::atomic_bool filesystem_dirty_on_mount_;    /// if filesystem is dirty on mount
    std::map < uint64_t /* block id */, std::unique_ptr < block_data_ptr_t > > block_cache; /// cache
    std::atomic < uint64_t > max_cached_block_number;   /// max cached block allowed in memory
    std::mutex mutex;

    void filesystem_verification();         /// filesystem basic health check
    void unblocked_sync_header();           /// sync head to disk

public:
    explicit block_io_t(basic_io_t & io);
    [[nodiscard]] bool filesystem_dirty_on_mount() const { return filesystem_dirty_on_mount_; } /// is filesystem dirty?
    void sync();                            /// sync
    ~block_io_t();

private:
    block_data_t & unblocked_at(uint64_t);  /// generate a block pointer (no mutex)

    /*!
     * @brief generate a block pointer
     * @param index Block index (while disk)
     * @return block_data_t, block pointer
     */
    block_data_t & at(uint64_t index);

public:

    safe_block_t safe_at(const uint64_t index) { auto & blk = at(index); return safe_block_t(blk); }

    /*!
     * update runtime info in header, static info will be ignored
     * @param head New header
     */
    void update_runtime_info(cfs_head_t head);
};

#endif //BLOCK_IO_H
