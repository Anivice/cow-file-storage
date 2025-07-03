#include "core/blk_manager.h"
#include "helper/log.h"

cfs_head_t blk_manager::get_header()
{
    cfs_head_t head{};
    auto & head_blk = blk_mapping.at(0);
    head_blk.get((uint8_t*)&head, sizeof(head), 0);
    return head;
}

void blk_manager::bitset(const uint64_t index, const bool value)
{
    uint16_t val = bitget(index);
    val <<= 8;
    val |= value;

    journal->push_action(actions::ACTION_MODIFY_BITMAP,
        ((uint16_t*)&index)[0], ((uint16_t*)&index)[1], ((uint16_t*)&index)[2], ((uint16_t*)&index)[3], val);

    block_bitmap->set(index, value);
    block_bitmap_mirror->set(index, value);

    journal->push_action(actions::ACTION_DONE);
}

blk_manager::blk_manager(block_io_t & block_io)
    : blk_mapping(block_io)
{
    auto header = get_header();
    journal = std::make_unique<journaling>(block_io);
    *(uint64_t*)&blk_count = header.static_info.data_table_end - header.static_info.data_table_start;
    *(uint64_t*)&block_size = header.static_info.block_size;
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
    block_attr = std::make_unique<block_attr_t>(
        block_io,
        header.static_info.block_size,
        header.static_info.data_block_attribute_table_start,
        header.static_info.data_block_attribute_table_end,
        blk_count);

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
    uint64_t bitmap_hash_before = header.runtime_info.data_bitmap_checksum;
    update_bitmap_hash(header);
    uint64_t bitmap_hash_after = header.runtime_info.data_bitmap_checksum;

    journal->push_action(actions::ACTION_UPDATE_BITMAP_HASH,
        ((uint16_t*)&bitmap_hash_before)[0], ((uint16_t*)&bitmap_hash_before)[1], ((uint16_t*)&bitmap_hash_before)[2], ((uint16_t*)&bitmap_hash_before)[3],
        ((uint16_t*)&bitmap_hash_after)[0], ((uint16_t*)&bitmap_hash_after)[1], ((uint16_t*)&bitmap_hash_after)[2], ((uint16_t*)&bitmap_hash_after)[3]);
    blk_mapping.update_runtime_info(header);
    journal->push_action(actions::ACTION_DONE);

    return last_alloc_blk;
}

void blk_manager::free_block(const uint64_t block)
{
    std::lock_guard<std::mutex> guard(mutex);
    if (bitget(block)) {
        bitset(block, false);

        auto header = get_header();
        header.runtime_info.allocated_blocks--;
        uint64_t bitmap_hash_before = header.runtime_info.data_bitmap_checksum;
        update_bitmap_hash(header);
        uint64_t bitmap_hash_after = header.runtime_info.data_bitmap_checksum;

        journal->push_action(actions::ACTION_UPDATE_BITMAP_HASH,
            ((uint16_t*)&bitmap_hash_before)[0], ((uint16_t*)&bitmap_hash_before)[1], ((uint16_t*)&bitmap_hash_before)[2], ((uint16_t*)&bitmap_hash_before)[3],
            ((uint16_t*)&bitmap_hash_after)[0], ((uint16_t*)&bitmap_hash_after)[1], ((uint16_t*)&bitmap_hash_after)[2], ((uint16_t*)&bitmap_hash_after)[3]);
        blk_mapping.update_runtime_info(header);
        journal->push_action(actions::ACTION_DONE);
    }
}

cfs_blk_attr_t blk_manager::get_attr(const uint64_t index)
{
    std::lock_guard lock(mutex);
    auto ret = block_attr->get(index);
    return *reinterpret_cast<cfs_blk_attr_t *>(&ret);
}

void blk_manager::set_attr(const uint64_t index, const cfs_blk_attr_t val)
{
    std::lock_guard lock(mutex);
    const auto before = block_attr->get(index);
    journal->push_action(actions::ACTION_MODIFY_BLOCK_ATTRIBUTES,
        ((uint16_t*)&index)[0], ((uint16_t*)&index)[1], ((uint16_t*)&index)[2], ((uint16_t*)&index)[3],
        before, *(uint16_t*)&val);
    block_attr->set(index, *(uint16_t*)&val);
    journal->push_action(actions::ACTION_DONE);
}

void blk_manager::hash_block(const uint64_t block)
{
    std::lock_guard<std::mutex> guard(mutex);
    assert_short(block < blk_count);
    if (bitget(block))
    {
        auto attr = block_attr->get(block);
        auto before = attr;
        auto & blk = blk_mapping.at(block);
        auto & battr = *reinterpret_cast<cfs_blk_attr_t*>(attr);
        battr.quick_hash = 0xFF;
        std::vector<uint8_t> blk_data;
        blk_data.resize(block_size);
        blk.get(blk_data.data(), block_size, 0);
        for (const auto c : blk_data) {
            battr.quick_hash ^= c;
        }
        journal->push_action(actions::ACTION_MODIFY_BLOCK_ATTRIBUTES,
            ((uint16_t*)&block)[0], ((uint16_t*)&block)[1], ((uint16_t*)&block)[2], ((uint16_t*)&block)[3],
            before, attr);
        block_attr->set(block, attr);
        journal->push_action(actions::ACTION_DONE);
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
