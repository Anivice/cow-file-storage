#include "core/block_io.h"
#include "helper/log.h"
#include "helper/cpp_assert.h"
#include "core/crc64sum.h"

void block_io_t::filesystem_verification()
{
    sector_data_t header_sector;
    io.read(header_sector, 0);
    cfs_head = *reinterpret_cast<cfs_head_t *>(header_sector.data());
    assert_short(cfs_head.magick == cfs_head.magick_ && cfs_head.magick == cfs_magick_number);
    assert_short(cfs_head.info_table_checksum == cfs_head.info_table_checksum_ && cfs_head.info_table_checksum == hashcrc64(cfs_head.static_info));
    if (!cfs_head.runtime_info.flags.clean) {
        filesystem_dirty_on_mount_ = true;
        warning_log("Filesystem dirty, automatic filesystem check");
    } else {
        filesystem_dirty_on_mount_ = false;
    }

    cfs_head.runtime_info.flags.clean = false;
}

void block_io_t::sync_header()
{
    std::lock_guard<std::mutex> lock(mutex);
    sector_data_t header_sector;
    std::memcpy(header_sector.data(), &cfs_head, sizeof(cfs_head_t));
    io.write(header_sector, 0);
    io.write(header_sector, cfs_head.static_info.sectors - 1);
}

block_io_t::block_io_t(basic_io_t & io_) : io(io_)
{
    filesystem_verification();
    cfs_head.runtime_info.mount_timestamp = get_timestamp();
    sync_header();
    max_cached_block_number = 8; // TODO: proper cache size
}

block_io_t::~block_io_t()
{
    if (cfs_head.runtime_info.flags.clean) {
        return;
    }

    cfs_head.runtime_info.flags.clean = true;
    sync_header();
    sync();
}

void block_io_t::sync()
{
    std::lock_guard<std::mutex> lock(mutex);
    block_cache.clear();
}

block_io_t::block_data_t & block_io_t::at(const uint64_t index)
{
    assert_short(index < cfs_head.static_info.blocks);
    if (block_cache.contains(index)) {
        auto & ret = *block_cache.at(index);
        if (index == 0 || index == cfs_head.static_info.blocks - 1) {
            ret->read_only = true;
        }
        return *ret;
    }

    if (block_cache.size() == max_cached_block_number) {
        block_cache.clear();
    }

    sector_data_t data_sector;
    block_cache.emplace(index,
        std::make_unique<block_data_ptr_t>(
            cfs_head.static_info.block_size,
            index * cfs_head.static_info.block_over_sector,
            (index + 1) * cfs_head.static_info.block_over_sector,
            io));

    const auto & block_data = *block_cache.at(index);
    block_data->data_.resize(cfs_head.static_info.block_size);
    for (uint64_t i = 0; i < cfs_head.static_info.block_over_sector; i++)
    {
        io.read(data_sector, index * cfs_head.static_info.block_over_sector + i);
        std::memcpy(block_data->data_.data() + i * SECTOR_SIZE, data_sector.data(), SECTOR_SIZE);
    }

    if (index == 0 || index == cfs_head.static_info.blocks - 1) {
        block_data->read_only = true;
    }

    return *block_data;
}

void block_io_t::block_data_t::get(uint8_t *buf, const size_t sz, const uint64_t in_blk_off)
{
    std::lock_guard<std::mutex> lock(mutex);
    assert_short(in_blk_off + sz <= data_.size());
    std::memcpy(buf, data_.data() + in_blk_off, sz);
}

void block_io_t::block_data_t::update(const uint8_t * new_data, const size_t new_size, const uint64_t in_block_offset)
{
    std::lock_guard<std::mutex> lock(mutex);
    assert_short(!read_only);
    assert_short(in_block_offset + new_size <= data_.size());
    std::memcpy(data_.data() + in_block_offset, new_data, new_size);
}

void block_io_t::block_data_t::sync()
{
    std::lock_guard<std::mutex> lock(mutex);
    if (read_only) return;
    for (uint64_t i = block_sector_start; i < block_sector_end; i++)
    {
        sector_data_t data_sector;
        std::memcpy(data_sector.data(), data_.data() + SECTOR_SIZE * (i - block_sector_start), SECTOR_SIZE);
        io.write(data_sector, i);
    }
}
