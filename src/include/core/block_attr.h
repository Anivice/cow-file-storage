#ifndef BLOCK_ATTR_H
#define BLOCK_ATTR_H

#include "core/block_io.h"

#define INDEX_TYPE          (1)
#define POINTER_TYPE        (2)
#define STORAGE_TYPE        (3)
#define COW_REDUNDANCY_TYPE (0)

struct cfs_blk_attr_t
{
    // cow status
    uint16_t frozen:1;      // is snapshot frozen
    // block type
    uint16_t type:2;        // 1 -> index, 2 -> pointer, 3 -> storage, 0 -> copy-on-write redundancy
    uint16_t quick_hash:8;  // xor hash, to fast check integrity
    uint16_t type_backup:2; // old type before cow
    uint16_t cow_refresh_count:3; // refresh count, if filesystem is out of block, the block with the lowest cow_refresh_count will be deallocated first
};
static_assert(sizeof(cfs_blk_attr_t) == 2);

inline uint16_t cfs_blk_attr_t_to_uint16(cfs_blk_attr_t attr) {
    const uint16_t ret = *(uint16_t*)&attr;
    return ret;
}

class block_attr_t {
    block_io_t & io;
    const uint64_t block_size;
    const uint64_t attr_region_start;
    const uint64_t attr_region_end;
    const uint64_t entries;
    std::mutex mutex;
    void linear_write(const void * data, uint64_t size, uint64_t offset);
    void linear_read(void * data, uint64_t size, uint64_t offset);

public:
    explicit block_attr_t(block_io_t & io, const uint64_t block_size,
        const uint64_t attr_region_start, const uint64_t attr_region_end, const uint64_t entries)
        : io(io),
        block_size(block_size),
        attr_region_start(attr_region_start),
        attr_region_end(attr_region_end),
        entries(entries) {}
    uint16_t get(uint64_t index);
    void set(uint64_t index, uint16_t value);
};

#endif //BLOCK_ATTR_H
