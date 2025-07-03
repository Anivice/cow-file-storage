#ifndef BLOCK_ATTR_H
#define BLOCK_ATTR_H

#include "core/block_io.h"

struct cfs_blk_attr_t
{
    // cow status
    uint16_t frozen:1; // is snapshot frozen
    // block type
    uint16_t type:2; // 1 -> index, 2 -> pointer, 3 -> storage
    uint16_t quick_hash:8; // xor hash, to fast check integrity
    uint16_t _reserved:5; // unused
};
static_assert(sizeof(cfs_blk_attr_t) == 2);

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
