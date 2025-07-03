#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include "core/block_io.h"

class ring_buffer
{
    block_io_t & io;
    const uint64_t blk_size;
    std::mutex mutex;
    const uint64_t map_start;
    const uint64_t map_end;
    struct flags_t {
        uint8_t flipped:1;
    };
    const uint64_t meta_size = sizeof(uint64_t) * 2 + sizeof(flags_t);
    const uint64_t buffer_length = (map_end - map_start) * blk_size - meta_size;

    void linear_read(void * data, uint64_t size, uint64_t offset);
    void linear_write(const void * data, uint64_t size, uint64_t offset);
    void get_attributes(uint64_t & rd_off, uint64_t & wr_off, flags_t & flags);
    void save_attributes(uint64_t rd_off, uint64_t wr_off, flags_t flags);

public:
    explicit ring_buffer(block_io_t & io, const uint64_t blk_size_, const uint64_t map_start_, const uint64_t map_end_)
        : io(io), blk_size(blk_size_), map_start(map_start_), map_end(map_end_) {}
    void write(uint8_t *, uint64_t);
    uint64_t read(uint8_t *, uint64_t, bool shadow_read = false);
    uint64_t available_buffer();
};

#endif //RING_BUFFER_H
