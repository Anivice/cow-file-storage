#include "core/block_attr.h"
#include "helper/cpp_assert.h"

void block_attr_t::linear_write(const void * data, const uint64_t size, const uint64_t offset)
{
    const uint64_t first_blk_position = offset / block_size;
    const uint64_t first_blk_offset = offset % block_size;
    uint64_t first_blk_write_size = block_size - first_blk_offset;
    if (first_blk_write_size > size) first_blk_write_size = size;
    const uint64_t continuous_blks = (size - first_blk_write_size) / block_size;
    const uint64_t last_blk_position = first_blk_position + continuous_blks + 1;
    const uint64_t last_blk_write_size = (size - first_blk_write_size) % block_size;

    uint64_t g_wr_off = 0;
    // 1. write first block
    auto & first_blk = io.at(first_blk_position + attr_region_start);
    first_blk.update(static_cast<const uint8_t *>(data), first_blk_write_size, first_blk_offset);
    g_wr_off += first_blk_write_size;
    first_blk.sync();

    // 2. write continuous blocks
    for (uint64_t i = 0; i < continuous_blks; i++) {
        const uint64_t blk_position = attr_region_start + first_blk_position + 1 + i;
        auto & blk = io.at(blk_position);
        blk.update(static_cast<const uint8_t *>(data) + g_wr_off, block_size, 0);
        g_wr_off += block_size;
        blk.sync();
    }

    if (last_blk_write_size) {
        auto & last_blk = io.at(attr_region_start + last_blk_position);
        last_blk.update(static_cast<const uint8_t *>(data) + g_wr_off, last_blk_write_size, 0);
        g_wr_off += last_blk_write_size;
        last_blk.sync();
    }

    assert_short(g_wr_off == size);
}

void block_attr_t::linear_read(void * data, const uint64_t size, const uint64_t offset)
{
    const uint64_t first_blk_position = offset / block_size;
    const uint64_t first_blk_offset = offset % block_size;
    uint64_t first_blk_read_size = block_size - first_blk_offset;
    if (first_blk_read_size > size) first_blk_read_size = size;
    const uint64_t continuous_blks = (size - first_blk_read_size) / block_size;
    const uint64_t last_blk_position = first_blk_position + continuous_blks + 1;
    const uint64_t last_blk_read_size = (size - first_blk_read_size) % block_size;

    uint64_t g_wr_off = 0;
    // 1. read first block
    auto & first_blk = io.at(first_blk_position + attr_region_start);
    first_blk.get(static_cast<uint8_t *>(data), first_blk_read_size, first_blk_offset);
    g_wr_off += first_blk_read_size;

    // 2. read continuous blocks
    for (uint64_t i = 0; i < continuous_blks; i++) {
        const uint64_t blk_position = attr_region_start + first_blk_position + 1 + i;
        auto & blk = io.at(blk_position);
        blk.get(static_cast<uint8_t *>(data) + g_wr_off, block_size, 0);
        g_wr_off += block_size;
    }

    if (last_blk_read_size) {
        auto & last_blk = io.at(attr_region_start + last_blk_position);
        last_blk.get(static_cast<uint8_t *>(data) + g_wr_off, last_blk_read_size, 0);
        g_wr_off += last_blk_read_size;
    }
    assert_short(g_wr_off == size);
}


uint16_t block_attr_t::get(const uint64_t index)
{
    assert_short(index < entries);
    std::lock_guard<std::mutex> lock(mutex);
    const uint64_t offset = index * sizeof(uint16_t);
    uint16_t result = 0;
    linear_read(&result, sizeof(uint16_t), offset);
    return result;
}

void block_attr_t::set(const uint64_t index, const uint16_t value)
{
    assert_short(index < entries);
    std::lock_guard<std::mutex> lock(mutex);
    const uint64_t offset = index * sizeof(uint16_t);
    linear_write(&value, sizeof(uint16_t), offset);
}
