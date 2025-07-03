#include "core/blk_manager.h"
#include "helper/log.h"

cfs_head_t blk_manager::get_header()
{
    cfs_head_t head{};
    auto & head_blk = blk_mapping.at(0);
    head_blk.get((uint8_t*)&head, sizeof(head), 0);
    return head;
}

blk_manager::blk_manager(block_io_t & block_io)
    : blk_mapping(block_io)
{
    auto header = get_header();
    journal = std::make_unique<journaling>(block_io);
    *(uint64_t*)&blk_count = header.static_info.data_table_end - header.static_info.data_table_start;
    block_bitmap = std::make_unique<bitmap>(
        block_io,
        header.static_info.data_bitmap_start, header.static_info.data_bitmap_end,
        blk_count,
        header.static_info.block_size);
    block_bitmap_mirror = std::make_unique<bitmap>(
        block_io,
        header.static_info.data_bitmap_backup_start, header.static_info.data_bitmap_backup_end,
        blk_count,
        header.static_info.block_size);

    auto bitmap_hash = [this, &header](const uint64_t start, const uint64_t end)->uint64_t {
        CRC64 hash;
        for (auto i = start; i < end; i++) {
            auto & blk = blk_mapping.at(i);
            std::vector<uint8_t> data; data.resize(header.static_info.block_size);
            blk.get(data.data(), header.static_info.block_size, 0);
            hash.update(data.data(), header.static_info.block_size);
        }

        return hash.get_checksum();
    };

    const uint64_t non_mirror = bitmap_hash(header.static_info.data_bitmap_start, header.static_info.data_bitmap_end);
    const uint64_t mirror = bitmap_hash(header.static_info.data_bitmap_backup_start, header.static_info.data_bitmap_backup_end);
    assert_short(non_mirror == mirror);
    assert_short(non_mirror == header.runtime_info.data_bitmap_checksum);
}

uint64_t blk_manager::allocate_block()
{
    std::lock_guard<std::mutex> guard(mutex);
    auto header = get_header();
    if (header.runtime_info.allocated_blocks == blk_count) {
        throw no_space_available();
    }
    auto last_alloc_blk = header.runtime_info.last_allocated_block;
    header.runtime_info.last_allocated_block = last_alloc_blk;
    bool searched_through = false;
    while (bitget(last_alloc_blk)) {
        last_alloc_blk++;
        if (last_alloc_blk >= blk_count && !searched_through) {
            last_alloc_blk = 0;
            searched_through = true;
        } else if (/* last_alloc_blk >= blk_count && */ searched_through) {
            throw no_space_available();
        }
    }

    bitset(last_alloc_blk, true);
    header.runtime_info.last_allocated_block = last_alloc_blk;
    header.runtime_info.allocated_blocks++;
    update_bitmap_hash(header);
    blk_mapping.update_runtime_info(header);
    return last_alloc_blk;
}

void blk_manager::free_block(const uint64_t block)
{
    std::lock_guard<std::mutex> guard(mutex);
    if (bitget(block)) {
        auto header = get_header();
        bitset(block, false);
        header.runtime_info.allocated_blocks--;
        update_bitmap_hash(header);
        blk_mapping.update_runtime_info(header);
    }
}

void blk_manager::update_bitmap_hash(cfs_head_t & cfs_head)
{
    auto bitmap_hash = [this, &cfs_head](const uint64_t start, const uint64_t end)->uint64_t {
        CRC64 hash;
        for (auto i = start; i < end; i++) {
            auto & blk = blk_mapping.at(i);
            std::vector<uint8_t> data; data.resize(cfs_head.static_info.block_size);
            blk.get(data.data(), cfs_head.static_info.block_size, 0);
            hash.update(data.data(), cfs_head.static_info.block_size);
        }

        return hash.get_checksum();
    };

    const uint64_t non_mirror = bitmap_hash(cfs_head.static_info.data_bitmap_start, cfs_head.static_info.data_bitmap_end);
    const uint64_t mirror = bitmap_hash(cfs_head.static_info.data_bitmap_backup_start, cfs_head.static_info.data_bitmap_backup_end);
    assert_short(non_mirror == mirror);
    cfs_head.runtime_info.data_bitmap_checksum = mirror;
}
