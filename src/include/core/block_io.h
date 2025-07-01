#ifndef BLOCK_IO_H
#define BLOCK_IO_H

#include <atomic>
#include <map>
#include <vector>
#include "core/basic_io.h"
#include "core/cfs.h"

class block_io_t {
public:
    class block_data_t;

private:
    class block_data_ptr_t {
        block_data_t * ptr;
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
        std::mutex mutex{};
        std::vector<uint8_t> data_;
        const uint64_t block_sector_start;
        const uint64_t block_sector_end;
        basic_io_t & io;
        explicit block_data_t(const uint64_t block_size, const uint64_t block_sector_start_,
            const uint64_t block_sector_end_, basic_io_t & io_)
            : block_sector_start(block_sector_start_), block_sector_end(block_sector_end_), io(io_)
        { data_.resize(block_size); }
        ~block_data_t() { sync(); } // sync on destruction

    public:
        size_t size() const { return data_.size(); }
        void get(uint8_t * buf, size_t sz, uint64_t in_blk_off);
        void update(const uint8_t * new_data, size_t new_size, uint64_t in_block_offset);
        void sync();
        friend class block_io_t;
    };

private:
    basic_io_t & io;
    cfs_head_t cfs_head;
    std::atomic_bool filesystem_dirty_on_mount_;
    std::map < uint64_t /* block id */, block_data_ptr_t > block_cache;
    std::atomic < uint64_t > max_cached_block_number;
    std::mutex mutex; // sync lock

    void filesystem_verification();

public:
    explicit block_io_t(basic_io_t & io);
    [[nodiscard]] bool filesystem_dirty_on_mount() const { return filesystem_dirty_on_mount_; }
    void sync_header();
    void sync();
    ~block_io_t();

    block_data_t & at(uint64_t);
    block_data_t & operator[](const uint64_t index) { return at(index); }
};

#endif //BLOCK_IO_H
