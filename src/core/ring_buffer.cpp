#include "core/ring_buffer.h"
#include "helper/cpp_assert.h"

void ring_buffer::linear_write(const void * data, const uint64_t size, const uint64_t offset)
{
    const uint64_t first_blk_position = offset / blk_size;
    const uint64_t first_blk_offset = offset % blk_size;
    uint64_t first_blk_write_size = blk_size - first_blk_offset;
    if (first_blk_write_size > size) first_blk_write_size = size;
    const uint64_t continuous_blks = (size - first_blk_write_size) / blk_size;
    const uint64_t last_blk_position = first_blk_position + continuous_blks + 1;
    const uint64_t last_blk_write_size = (size - first_blk_write_size) % blk_size;

    uint64_t g_wr_off = 0;
    // 1. write first block
    auto & first_blk = io.at(first_blk_position + map_start);
    first_blk.update(static_cast<const uint8_t *>(data), first_blk_write_size, first_blk_offset);
    g_wr_off += first_blk_write_size;
    first_blk.sync();

    // 2. write continuous blocks
    for (uint64_t i = 0; i < continuous_blks; i++) {
        const uint64_t blk_position = map_start + first_blk_position + 1 + i;
        auto & blk = io.at(blk_position);
        blk.update(static_cast<const uint8_t *>(data) + g_wr_off, blk_size, 0);
        g_wr_off += blk_size;
        blk.sync();
    }

    if (last_blk_write_size) {
        auto & last_blk = io.at(map_start + last_blk_position);
        last_blk.update(static_cast<const uint8_t *>(data) + g_wr_off, last_blk_write_size, 0);
        g_wr_off += last_blk_write_size;
        last_blk.sync();
    }

    assert_short(g_wr_off == size);
}

void ring_buffer::linear_read(void * data, const uint64_t size, const uint64_t offset)
{
    const uint64_t first_blk_position = offset / blk_size;
    const uint64_t first_blk_offset = offset % blk_size;
    uint64_t first_blk_read_size = blk_size - first_blk_offset;
    if (first_blk_read_size > size) first_blk_read_size = size;
    const uint64_t continuous_blks = (size - first_blk_read_size) / blk_size;
    const uint64_t last_blk_position = first_blk_position + continuous_blks + 1;
    const uint64_t last_blk_read_size = (size - first_blk_read_size) % blk_size;

    uint64_t g_wr_off = 0;
    // 1. read first block
    auto & first_blk = io.at(first_blk_position + map_start);
    first_blk.get(static_cast<uint8_t *>(data), first_blk_read_size, first_blk_offset);
    g_wr_off += first_blk_read_size;

    // 2. read continuous blocks
    for (uint64_t i = 0; i < continuous_blks; i++) {
        const uint64_t blk_position = map_start + first_blk_position + 1 + i;
        auto & blk = io.at(blk_position);
        blk.get(static_cast<uint8_t *>(data) + g_wr_off, blk_size, 0);
        g_wr_off += blk_size;
    }

    if (last_blk_read_size) {
        auto & last_blk = io.at(map_start + last_blk_position);
        last_blk.get(static_cast<uint8_t *>(data) + g_wr_off, last_blk_read_size, 0);
        g_wr_off += last_blk_read_size;
    }
    assert_short(g_wr_off == size);
}

void ring_buffer::get_attributes(uint64_t & rd_off, uint64_t & wr_off, flags_t & flags)
{
    auto & block_1 = io.at(map_start);
    block_1.get(reinterpret_cast<uint8_t *>(&rd_off), sizeof(uint64_t), 0);
    block_1.get(reinterpret_cast<uint8_t *>(&wr_off), sizeof(uint64_t), sizeof(uint64_t));
    block_1.get(reinterpret_cast<uint8_t *>(&flags), sizeof(flags_t), sizeof(uint64_t) * 2);
}

void ring_buffer::save_attributes(uint64_t rd_off, uint64_t wr_off, flags_t flags)
{
    auto & block_1 = io.at(map_start);
    block_1.update(reinterpret_cast<uint8_t *>(&rd_off), sizeof(uint64_t), 0);
    block_1.update(reinterpret_cast<uint8_t *>(&wr_off), sizeof(uint64_t), sizeof(uint64_t));
    block_1.update(reinterpret_cast<uint8_t *>(&flags), sizeof(flags_t), sizeof(uint64_t) * 2);
    block_1.sync();
}

inline std::uint64_t contiguous_space(const std::uint64_t from, const std::uint64_t until, const std::uint64_t cap)
{
    return from < until ? until - from        // flipped mode
                        : cap - from;        // straight mode
}

void ring_buffer::write(std::uint8_t *src, std::uint64_t len)
{
    std::lock_guard<std::mutex> lock(mutex);

    /* 1. Load current state */
    std::uint64_t rd_off, wr_off;
    flags_t flags{};
    get_attributes(rd_off, wr_off, flags);

    /* 2. Free space */
    const std::uint64_t free_bytes =
        flags.flipped ? rd_off - wr_off
                       : buffer_length - wr_off + rd_off;
    if (len > free_bytes) len = free_bytes;          // trim to fit

    /* 3. First slice limited by contiguous space */
    const std::uint64_t cont = contiguous_space(wr_off, rd_off, buffer_length);
    const std::uint64_t first = std::min(len, cont); // GUARANTEES first ≤ len

    linear_write(src, first, meta_size + wr_off);
    wr_off += first;

    /* 4. Wrap if we reached physical end */
    if (wr_off == buffer_length) {
        wr_off = 0;
        flags.flipped = !flags.flipped;
    }

    /* 5. Second slice, if any */
    const std::uint64_t remaining = len - first;     // guaranteed non‑negative
    if (remaining) {
        linear_write(src + first, remaining, meta_size + wr_off);
        wr_off += remaining;
    }

    /* 6. Persist */
    save_attributes(rd_off, wr_off, flags);
}

std::uint64_t ring_buffer::read(std::uint8_t *dst, std::uint64_t len, bool shadow_read)
{
    std::lock_guard<std::mutex> lock(mutex);

    /* 1. Load state */
    std::uint64_t rd_off, wr_off;
    flags_t flags{};
    get_attributes(rd_off, wr_off, flags);

    /* 2. Available bytes */
    const std::uint64_t avail =
        flags.flipped ? (buffer_length - rd_off) + wr_off
                       : (wr_off - rd_off);
    if (len > avail) len = avail;                      // clip to available

    /* 3. First slice */
    const std::uint64_t cont = contiguous_space(rd_off, wr_off, buffer_length);
    const std::uint64_t first = std::min(len, cont);   // first ≤ len

    linear_read(dst, first, meta_size + rd_off);
    rd_off += first;

    /* 4. Un‑flip when we cross the boundary */
    if (rd_off == buffer_length) {
        rd_off = 0;
        flags.flipped = !flags.flipped;
    }

    /* 5. Second slice */
    const std::uint64_t remaining = len - first;
    if (remaining) {
        linear_read(dst + first, remaining, meta_size + rd_off);
        rd_off += remaining;
    }

    /* 6. Persist and return */
    if (!shadow_read) {
        save_attributes(rd_off, wr_off, flags);
    }
    return len;
}

uint64_t ring_buffer::available_buffer()
{
    /* 1. Load state */
    std::uint64_t rd_off, wr_off;
    flags_t flags{};
    get_attributes(rd_off, wr_off, flags);

    /* 2. Available bytes */
    const std::uint64_t avail =
        flags.flipped ? (buffer_length - rd_off) + wr_off
                       : (wr_off - rd_off);
    return avail;
}
