#include <sys/sysinfo.h>
#include "core/block_io.h"
#include "helper/log.h"
#include "helper/cpp_assert.h"
#include "core/crc64sum.h"

void block_io_t::filesystem_verification()
{
    sector_data_t header_sector;
    io.read(header_sector, 0);
    cfs_head = *reinterpret_cast<cfs_head_t *>(header_sector.data());
    // FIXME: request fix
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

void block_io_t::unblocked_sync_header()
{
    if (read_only_fs) {
        throw read_only_filesystem();
    }

    auto & start = unblocked_at(0);
    auto & end = unblocked_at(cfs_head.static_info.blocks - 1);
    start.read_only = false;
    start.update((uint8_t*)&cfs_head, sizeof(cfs_head_t), 0);
    start.sync();
    start.read_only = true;

    end.read_only = false;
    end.update((uint8_t*)&cfs_head, sizeof(cfs_head_t), cfs_head.static_info.block_size - sizeof(cfs_head_t));
    end.sync();
    end.read_only = true;

    start.not_in_use();
    end.not_in_use();
}

block_io_t::block_io_t(basic_io_t & io, const bool read_only_fs) : io(io)
{
    this->read_only_fs = read_only_fs;
    filesystem_verification();
    if (!read_only_fs) {
        cfs_head.runtime_info.mount_timestamp = get_timestamp();
        unblocked_sync_header();
    }

    struct sysinfo info{};
    if (sysinfo(&info) != 0) {
        perror("sysinfo");
        max_cached_block_number = (64 * 1024 * 1024 / cfs_head.static_info.block_size);
    } else {
        max_cached_block_number = static_cast<uint64_t>(static_cast<double>(info.totalram) * 0.10
            / static_cast<double>(cfs_head.static_info.block_size));
        max_cached_block_number = std::min(max_cached_block_number,
            64 * 1024 * 1024 / cfs_head.static_info.block_size);
    }

    // debug_log("Cache size: ", max_cached_block_number, " blocks");
}

block_io_t::~block_io_t()
{
    if (read_only_fs) return;
    cfs_head.runtime_info.flags.clean = true;
    unblocked_sync_header();
    block_cache.clear(); // force free all cached blocks
}

void block_io_t::update_runtime_info(const cfs_head_t head)
{
    if (read_only_fs) throw read_only_filesystem();
    cfs_head.runtime_info = head.runtime_info;
    unblocked_sync_header();
}

void block_io_t::sync()
{
    block_cache.clear();
    if (!read_only_fs) {
        unblocked_sync_header();
    }
}

block_io_t::block_data_t & block_io_t::unblocked_at(const uint64_t index)
{
    assert_short(index < cfs_head.static_info.blocks);
    access_frequencies[index]++;
    if (block_cache.contains(index)) {
        auto & ret = *block_cache.at(index);
        if (index == 0 || index == cfs_head.static_info.blocks - 1) {
            ret->read_only = true;
        } else {
            ret->read_only = false;
        }

        if (read_only_fs) ret->read_only = true;

        ret->in_use = true;
        return *ret;
    }

    if (block_cache.size() >= max_cached_block_number)
    {
        std::vector < std::pair < uint64_t, uint64_t > > pending_for_deletion;
        for (const auto &[id, data] : block_cache) {
            if (!(*data)->in_use) {
                pending_for_deletion.emplace_back(id, access_frequencies[id]);
            }
        }

        std::ranges::sort(pending_for_deletion,
            [](const std::pair < uint64_t, uint64_t > & a, const std::pair < uint64_t, uint64_t > & b)->bool {
            return a.second < b.second;
        });

        pending_for_deletion.resize((pending_for_deletion.size() / 3) * 2);
        for (const auto & cached_block : pending_for_deletion | std::views::keys) {
            block_cache.erase(cached_block);
            access_frequencies.erase(cached_block);
        }
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
    } else {
        block_data->read_only = false;
    }

    if (read_only_fs) block_data->read_only = true;

    block_data->in_use = true;
    return *block_data;
}

block_io_t::block_data_t & block_io_t::at(const uint64_t index)
{
    return unblocked_at(index);
}

void block_io_t::block_data_t::get(uint8_t *buf, const size_t sz, const uint64_t in_blk_off)
{
    assert_short(in_blk_off + sz <= data_.size());
    std::memcpy(buf, data_.data() + in_blk_off, sz);
}

void block_io_t::block_data_t::update(const uint8_t * new_data, const size_t new_size, const uint64_t in_block_offset)
{
    if(read_only) {
        throw runtime_error("Read-only block " +
            std::to_string(this->block_sector_end / (this->block_sector_end - this->block_sector_start + 1)));
    }
    assert_short(in_block_offset + new_size <= data_.size());
    std::memcpy(data_.data() + in_block_offset, new_data, new_size);
    out_of_sync = true;
}

void block_io_t::block_data_t::sync()
{
    if (read_only) return;
    if (!out_of_sync) return;
    for (uint64_t i = block_sector_start; i < block_sector_end; i++)
    {
        sector_data_t data_sector;
        std::memcpy(data_sector.data(), data_.data() + SECTOR_SIZE * (i - block_sector_start), SECTOR_SIZE);
        io.write(data_sector, i);
    }

    // debug_log("Sync block (sector ", block_sector_start, " - ", block_sector_end, ")");
    out_of_sync = false;
}
