/* mkfs.cpp
 *
 * Copyright 2025 Anivice Ives
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <atomic>
#include <algorithm>

#include "core/cfs.h"
#include "helper/cpp_assert.h"
#include "helper/log.h"
#include "helper/arg_parser.h"
#include "helper/color.h"
#include "core/crc64sum.h"
#include "core/basic_io.h"

namespace mkfs {
    const arg_parser::parameter_vector Arguments = {
        { .name = "help",       .short_name = 'h', .arg_required = false,   .description = "Prints this help message" },
        { .name = "version",    .short_name = 'v', .arg_required = false,   .description = "Prints version" },
        { .name = "verbose",    .short_name = 'V', .arg_required = false,   .description = "Enable verbose output" },
        { .name = "path",       .short_name = 'p', .arg_required = true,    .description = "Path to disk/file" },
        { .name = "block",      .short_name = 'b', .arg_required = true,    .description = "Block size" },
    };

    void print_help(const std::string & program_name)
    {
        uint64_t max_name_len = 0;
        std::vector< std::pair <std::string, std::string>> output;
        const std::string required_str = " arg";
        for (const auto & [name, short_name, arg_required, description] : Arguments)
        {
            std::string name_str =
                (short_name == '\0' ? "" : "-" + std::string(1, short_name))
                += ",--" + name
                += (arg_required ? required_str : "");

            if (max_name_len < name_str.size())
            {
                max_name_len = name_str.size();
            }

            output.emplace_back(name_str, description);
        }

        std::cout << color::color(5,5,5) << program_name << color::no_color() << color::color(0,2,5) << " [options]" << color::no_color()
                  << std::endl << color::color(1,2,3) << "options:" << color::no_color() << std::endl;
        for (const auto & [name, description] : output)
        {
            std::cout << "    " << color::color(1,5,4) << name << color::no_color()
                      << std::string(max_name_len + 4 - name.size(), ' ')
                      << color::color(4,5,1) << description << color::no_color() << std::endl;
        }
    }
}

long long solveC(long long Count, long long Scale)
{
    if (Scale <= 0) return -1;                      // guard bad input

    long long lo = 0;
    long long hi = Count;                           // f(C) ≥ C, so Count is still an upper bound

    while (lo < hi) {
        long long mid = lo + ((hi - lo) >> 1);      // overflow‑safe midpoint
        long long A   = ceil_div(mid, 4096 * Scale);
        long long B   = ceil_div(mid,  256 * Scale);
        long long cur = 2 * A + B + mid;
        if (cur >= Count)
            hi = mid;
        else
            lo = mid + 1;
    }

    long long A = ceil_div(lo, 4096 * Scale);
    long long B = ceil_div(lo,  256 * Scale);
    return (2 * A + B + lo == Count) ? lo : -1;     // verify exact equality
}

bool is_2_power_of(unsigned long long x)
{
    for (unsigned long long i = 1; i <= sizeof(x) * 8; i++)
    {
        const auto current_bit = x & 0x01;
        if (current_bit) {
            x >>= 1;
            return !x;
        }

        x >>= 1;
    }

    return false;
}

cfs_head_t make_head(const sector_t sectors, const uint64_t block_size)
{
    cfs_head_t head{};
    head.magick = head.magick_ = cfs_magick_number;

    // ────── basic sanity checks ──────
    assert_throw(block_size > SECTOR_SIZE && block_size % SECTOR_SIZE == 0 && is_2_power_of(block_size / SECTOR_SIZE),
            "Block size not aligned");

    head.static_info.block_over_sector = block_size / SECTOR_SIZE;
    head.static_info.block_size        = block_size;
    head.static_info.sectors           = sectors;
    head.static_info.blocks            = sectors / head.static_info.block_over_sector;

    const uint64_t body_size              = head.static_info.blocks - 2;     // head & tail
    const uint64_t journaling_section_size= std::max<uint64_t>(body_size / 10, 32);
    assert_throw(body_size > journaling_section_size, "Not enough space");

    const uint64_t left_over  = body_size - journaling_section_size;
    const uint64_t scale      = head.static_info.block_over_sector;

    uint64_t k = UINT64_MAX;
    uint64_t offset = 0;
    while (k == UINT64_MAX && offset < left_over) {
        const long long candidate = solveC(left_over - offset, scale);
        if (candidate != -1)
            k = static_cast<uint64_t>(candidate);
        else
            ++offset;
    }
    assert_throw(offset < left_over, "No solution for disk space division");

    const uint64_t data_blocks = k;

    // ────── compute bitmap & attribute sizes with correct precedence ──────
    const uint64_t bytes_per_block = scale * SECTOR_SIZE;
    const uint64_t bits_per_block  = bytes_per_block * 8ULL;

    const uint64_t data_block_bitmap = ceil_div(data_blocks, bits_per_block);
    const uint64_t data_block_attribute_region = ceil_div(data_blocks * 2ULL, bytes_per_block);

    // ────── carve the regions ──────
    uint64_t block_offset = 1;                 // block 0 is the head itself

    head.static_info.data_bitmap_start = block_offset;
    head.static_info.data_bitmap_end   = block_offset + data_block_bitmap;
    block_offset += data_block_bitmap;

    head.static_info.data_bitmap_backup_start = block_offset;
    head.static_info.data_bitmap_backup_end   = block_offset + data_block_bitmap;
    block_offset += data_block_bitmap;

    head.static_info.data_block_attribute_table_start = block_offset;
    head.static_info.data_block_attribute_table_end   = block_offset + data_block_attribute_region;
    block_offset += data_block_attribute_region;

    head.static_info.data_table_start = block_offset;
    head.static_info.data_table_end   = block_offset + data_blocks;
    block_offset += data_blocks;

    head.static_info.journal_start = block_offset;
    head.static_info.journal_end   = block_offset + journaling_section_size;
    block_offset += journaling_section_size;

    // ────── checksum & timestamps ──────
    head.info_table_checksum = head.info_table_checksum_ = hashcrc64(head.static_info);

    const auto now = get_timestamp();
    head.runtime_info.mount_timestamp = head.runtime_info.last_check_timestamp = now;

    auto region_gen = [](const uint64_t start, const uint64_t end) {
        return color::color(1,5,4) + "[" + std::to_string(start) + ", " + std::to_string(end) + ")" + color::color(3,3,3)
        + " (" + std::to_string(end - start) + " block<s>)" + color::no_color();
    };

    const auto data_block_attribute = region_gen(head.static_info.data_block_attribute_table_start, head.static_info.data_block_attribute_table_end);
    const auto data_bitmap_region = region_gen(head.static_info.data_bitmap_start, head.static_info.data_bitmap_end);
    const auto data_bitmap_backup_region = region_gen(head.static_info.data_bitmap_backup_start, head.static_info.data_bitmap_backup_end);
    const auto data_region = region_gen(head.static_info.data_table_start, head.static_info.data_table_end);
    const auto journal_region = region_gen(head.static_info.journal_start, head.static_info.journal_end);

    verbose_log("============================================ Disk Overview ============================================");
    verbose_log(" Disk size:    ", head.static_info.sectors, " sectors");
    verbose_log("               ", head.static_info.blocks, " blocks (addressable region: ", region_gen(0, head.static_info.blocks), ")");
    verbose_log(" Block size:   ", head.static_info.block_size, " bytes (", head.static_info.block_over_sector, " sectors)");
    verbose_log("  ─────────────────────────────┬───────────────────────────────────────────────────────────────────────");
    verbose_log(color::color(5,5,5), "              FILE SYSTEM HEAD │ BLOCK: ", region_gen(0, 1));
    verbose_log("  ─────────────────────────────┼───────────────────────────────────────────────────────────────────────");
    verbose_log(color::color(5,5,5), "            DATA REGION BITMAP │ BLOCK: ", data_bitmap_region);
    verbose_log("  ─────────────────────────────┼───────────────────────────────────────────────────────────────────────");
    verbose_log(color::color(5,5,5), "        DATA BITMAP BACKUP MAP │ BLOCK: ", data_bitmap_backup_region);
    verbose_log("  ─────────────────────────────┼───────────────────────────────────────────────────────────────────────");
    verbose_log(color::color(5,5,5), "          DATA BLOCK ATTRIBUTE │ BLOCK: ", data_block_attribute);
    verbose_log("  ─────────────────────────────┼───────────────────────────────────────────────────────────────────────");
    verbose_log(color::color(5,5,5), "                    DATA BLOCK │ BLOCK: ", data_region);
    verbose_log("  ─────────────────────────────┼───────────────────────────────────────────────────────────────────────");
    verbose_log(color::color(5,5,5), "                JOURNAL REGION │ BLOCK: ", journal_region);
    verbose_log("  ─────────────────────────────┼───────────────────────────────────────────────────────────────────────");
    verbose_log(color::color(5,5,5), "       FILE SYSTEM HEAD BACKUP │ BLOCK: ", region_gen(block_offset, block_offset + 1));
    verbose_log("  ─────────────────────────────┴───────────────────────────────────────────────────────────────────────");
    verbose_log("=======================================================================================================");

    return head;
}

void clear_entries(basic_io_t & io, cfs_head_t & head)
{
    auto clear_region = [&](const uint64_t start, const uint64_t end)->uint64_t
    {
        CRC64 hash_empty;
        sector_data_t data{};
        for (uint64_t i = start; i < end; ++i) {
            for (uint64_t j = 0; j < head.static_info.block_over_sector; j++) {
                // debug_log("Scrubbing sector ", j + i * head.static_info.block_over_sector);
                io.write(data, j + i * head.static_info.block_over_sector);
                hash_empty.update(data.data(), data.size());
            }
        }

        return hash_empty.get_checksum();
    };

    clear_region(0, 1);
    clear_region(head.static_info.blocks - 1, head.static_info.blocks);
    head.runtime_info.data_bitmap_checksum = clear_region(head.static_info.data_bitmap_start, head.static_info.data_bitmap_end);
    clear_region(head.static_info.data_bitmap_backup_start, head.static_info.data_bitmap_backup_end);
    clear_region(head.static_info.data_block_attribute_table_start, head.static_info.data_block_attribute_table_end);
    clear_region(head.static_info.journal_start, head.static_info.journal_end);
}

int mkfs_main(int argc, char **argv)
{
    try
    {
        arg_parser args(argc, argv, mkfs::Arguments);
        auto contains = [&args](const std::string & name, std::string & val)->bool
        {
            const auto it = std::ranges::find_if(args,
                [&name](const std::pair<std::string, std::string> & p)->bool{ return p.first == name; });
            if (it != args.end())
            {
                val = it->second;
                return true;
            }

            return false;
        };

        std::string arg_val;
        if (contains("help", arg_val)) // GNU compliance, help must be processed first if it appears and ignore all other arguments
        {
            mkfs::print_help(argv[0]);
            return EXIT_SUCCESS;
        }

        if (contains("version", arg_val))
        {
            std::cout << color::color(5,5,5) << argv[0] << color::no_color()
                << color::color(0,3,3) << " core version " << color::color(0,5,5) << CORE_VERSION
                << color::color(0,3,3) << " backend version " << color::color(0,5,5) << BACKEND_VERSION
                << color::no_color() << std::endl;
            return EXIT_SUCCESS;
        }

        if (contains("verbose", arg_val))
        {
            debug::verbose = true;
            verbose_log("Verbose mode enabled");
        }

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        uint64_t block_size = 4096;
        if (contains("block", arg_val)) {
            try {
                block_size = std::strtoull(arg_val.c_str(), nullptr, 10);
            } catch (const std::exception &) {
                throw runtime_error("Invalid block size: " + arg_val);
            }
        }

        if (contains("path", arg_val))
        {
            verbose_log("Formatting disk ", arg_val);
            basic_io_t io;
            io.open(arg_val.c_str());
            auto head = make_head(io.get_file_sectors(), block_size);
            verbose_log("Clearing entries");
            clear_entries(io, head);
            head.runtime_info.last_check_timestamp = get_timestamp();
            sector_data_t data{};
            std::memcpy(data.data(), &head, sizeof(head));
            verbose_log("Writing filesystem head");
            io.write(data, 0);
            io.write(data, head.static_info.sectors - 1);
            io.close();
            verbose_log("done");
            return EXIT_SUCCESS;
        }

        throw runtime_error("No path specified");
    } catch (const std::exception &e) {
        error_log(e.what());
        return EXIT_FAILURE;
    }
}
