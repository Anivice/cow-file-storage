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

#include "../include/core/cfs.h"
#include "../include/helper/cpp_assert.h"
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
    long long lo = 0, hi = Count;             // f(C) ≥ C, so C ≤ Count
    auto ceil_div = [](long long x, long long d){
        return (x + d - 1) / d;               // idiom  ⟂cite turn0search0,turn0search8⟂
    };

    while (lo < hi){
        long long mid  = (lo + hi) >> 1;
        long long A    = ceil_div(mid, 4096 * Scale);
        long long B    = ceil_div(mid,  256 * Scale);
        long long cur  = 2*A + B + mid;       // equation (★)
        if (cur >= Count) hi = mid; else lo = mid + 1;
    }
    // verification step
    long long A = (lo + 4096*Scale - 1) / (4096*Scale);
    long long B = (lo + 256*Scale  - 1) / (256*Scale);
    return (2*A + B + lo == Count) ? lo : -1; // −1 ⇒ no solution
}


void make_head(const sector_t sectors, const uint64_t block_size)
{
    cfs_head_t head{};
    head.magick = head.magick_ = cfs_magick_number;
    assert_throw(block_size > 512 && block_size % 512 == 0 && (block_size / 512) % 2 == 0 , "Block size not aligned");
    head.static_info.block_over_sector = block_size / 512;
    head.static_info.block_size = block_size;
    head.static_info.sectors = sectors;
    head.static_info.blocks = sectors / head.static_info.block_over_sector;
    auto body_size = head.static_info.blocks - 2; // head and tail
    const auto journaling_section_size = std::max(body_size / 10, 32ul);
    assert_throw(body_size > journaling_section_size, "Not enough space");
    const auto left_over = body_size - journaling_section_size;
    long long k = -1;
    int offset = 0;
    while (k ==  -1 && offset != left_over) {
        k = solveC(left_over - offset++, head.static_info.block_over_sector);
    }
    assert_throw(offset != left_over, "No solution for disk space division");
    const auto data_blocks = k;
    const auto data_block_bitmap = (data_blocks / 8 + (data_blocks % 8 == 0 ? 0 : 1)) / (512 * head.static_info.block_over_sector)
        + (data_blocks / 8 + ((data_blocks % 8 == 0 ? 0 : 1)) % (512 * head.static_info.block_over_sector) == 0 ? 0 : 1);
    const auto data_block_attribute_region = 2 * k / (512 * head.static_info.block_over_sector) + (2 * k % (512 * head.static_info.block_over_sector) == 0 ? 0 : 1);
    const auto pile_region_size = data_block_bitmap * 2 + data_block_attribute_region + data_blocks;

    uint64_t block_offset = 1;
    head.static_info.data_bitmap_start = block_offset;
    head.static_info.data_bitmap_end = data_block_bitmap + block_offset;
    block_offset += data_block_bitmap;
    head.static_info.data_bitmap_backup_start = block_offset;
    head.static_info.data_bitmap_backup_end = data_block_bitmap + block_offset;
    block_offset += data_block_bitmap;

    head.static_info.data_block_attribute_table_start = block_offset;
    head.static_info.data_block_attribute_table_end = data_block_attribute_region + block_offset;
    block_offset += data_block_attribute_region;

    head.static_info.data_table_start = block_offset;
    head.static_info.data_table_end = data_blocks + block_offset;
    block_offset += data_blocks;

    head.static_info.journal_start = block_offset;
    head.static_info.journal_end = journaling_section_size + block_offset;
    block_offset += journaling_section_size;

    CRC64 checksum;
    checksum.update(reinterpret_cast<uint8_t *>(&head.static_info), sizeof(head.static_info));
    head.info_table_checksum = head.info_table_checksum_ = checksum.get_checksum();
    head.runtime_info.mount_timestamp = head.runtime_info.last_check_timestamp
        = std::chrono::high_resolution_clock::now().time_since_epoch().count();

    auto region_gen = [](const uint64_t start, const uint64_t end) {
        return "[" + std::to_string(start) + ", " + std::to_string(end) + ") (" + std::to_string(end - start) + " blocks)";
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
    verbose_log("              FILE SYSTEM HEAD │ BLOCK: ", region_gen(0, 1));
    verbose_log("  ─────────────────────────────┼───────────────────────────────────────────────────────────────────────");
    verbose_log("            DATA REGION BITMAP │ BLOCK: ", data_bitmap_region);
    verbose_log("  ─────────────────────────────┼───────────────────────────────────────────────────────────────────────");
    verbose_log("        DATA BITMAP BACKUP MAP │ BLOCK: ", data_bitmap_backup_region);
    verbose_log("  ─────────────────────────────┼───────────────────────────────────────────────────────────────────────");
    verbose_log("          DATA BLOCK ATTRIBUTE │ BLOCK: ", data_block_attribute);
    verbose_log("  ─────────────────────────────┼───────────────────────────────────────────────────────────────────────");
    verbose_log("                    DATA BLOCK │ BLOCK: ", data_region);
    verbose_log("  ─────────────────────────────┼───────────────────────────────────────────────────────────────────────");
    verbose_log("                JOURNAL REGION │ BLOCK: ", journal_region);
    verbose_log("  ─────────────────────────────┼───────────────────────────────────────────────────────────────────────");
    verbose_log("       FILE SYSTEM HEAD BACKUP │ BLOCK: ", region_gen(block_offset, block_offset + 1));
    verbose_log("  ─────────────────────────────┴───────────────────────────────────────────────────────────────────────");
    verbose_log("=======================================================================================================");
}

int mkfs_main(int argc, char **argv)
{
    try
    {
        make_head(7919*7, 2048);
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
    }
    catch (const std::exception & e)
    {
        error_log(e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
