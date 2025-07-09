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
#include "journal_hd.h"
#include "core/journal.h"
#include "core/cfs.h"
#include "helper/cpp_assert.h"
#include "helper/log.h"
#include "helper/arg_parser.h"
#include "helper/color.h"
#include "core/basic_io.h"
#include "core/bitmap.h"
#include "core/block_attr.h"


namespace fsck {
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

cfs_head_t print_head(cfs_head_t head)
{
    auto region_gen = [](const uint64_t start, const uint64_t end) {
        return color::color(1,5,4) + "[" + std::to_string(start) + ", " + std::to_string(end) + ")" + color::color(3,3,3)
        + " (" + std::to_string(end - start) + " block<s>)" + color::no_color();
    };

    const auto data_block_attribute = region_gen(head.static_info.data_block_attribute_table_start, head.static_info.data_block_attribute_table_end);
    const auto data_bitmap_region = region_gen(head.static_info.data_bitmap_start, head.static_info.data_bitmap_end);
    const auto data_bitmap_backup_region = region_gen(head.static_info.data_bitmap_backup_start, head.static_info.data_bitmap_backup_end);
    const auto data_region = region_gen(head.static_info.data_table_start, head.static_info.data_table_end);
    const auto journal_region = region_gen(head.static_info.journal_start, head.static_info.journal_end);

    console_log("============================================ Disk Overview ============================================");
    console_log(" Disk size:    ", head.static_info.sectors, " sectors");
    console_log("               ", head.static_info.blocks, " blocks (addressable region: ", region_gen(0, head.static_info.blocks), ")");
    console_log(" Block size:   ", head.static_info.block_size, " bytes (", head.static_info.block_over_sector, " sectors)");
    console_log("  ─────────────────────────────┬───────────────────────────────────────────────────────────────────────");
    console_log(color::color(5,5,5), "              FILE SYSTEM HEAD │ BLOCK: ", region_gen(0, 1));
    console_log("  ─────────────────────────────┼───────────────────────────────────────────────────────────────────────");
    console_log(color::color(5,5,5), "            DATA REGION BITMAP │ BLOCK: ", data_bitmap_region);
    console_log("  ─────────────────────────────┼───────────────────────────────────────────────────────────────────────");
    console_log(color::color(5,5,5), "        DATA BITMAP BACKUP MAP │ BLOCK: ", data_bitmap_backup_region);
    console_log("  ─────────────────────────────┼───────────────────────────────────────────────────────────────────────");
    console_log(color::color(5,5,5), "          DATA BLOCK ATTRIBUTE │ BLOCK: ", data_block_attribute);
    console_log("  ─────────────────────────────┼───────────────────────────────────────────────────────────────────────");
    console_log(color::color(5,5,5), "                    DATA BLOCK │ BLOCK: ", data_region);
    console_log("  ─────────────────────────────┼───────────────────────────────────────────────────────────────────────");
    console_log(color::color(5,5,5), "                JOURNAL REGION │ BLOCK: ", journal_region);
    console_log("  ─────────────────────────────┼───────────────────────────────────────────────────────────────────────");
    console_log(color::color(5,5,5), "       FILE SYSTEM HEAD BACKUP │ BLOCK: ", region_gen(head.static_info.blocks - 1, head.static_info.blocks));
    console_log("  ─────────────────────────────┴───────────────────────────────────────────────────────────────────────");
    console_log(color::color(5,5,5), std::hex, "Filesystem Bitmap Hash: ", head.runtime_info.data_bitmap_checksum);
    console_log(color::color(5,5,5), std::dec, "Filesystem Allocated Blocks: ", head.runtime_info.allocated_blocks);
    console_log(color::color(5,5,5), std::dec, "Filesystem Last Allocated Block: ", head.runtime_info.last_allocated_block);
    console_log("=======================================================================================================");

    return head;
}

int fsck_main(int argc, char **argv)
{
    try
    {
        arg_parser args(argc, argv, fsck::Arguments);
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
            fsck::print_help(argv[0]);
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
        if (contains("path", arg_val))
        {
            basic_io_t io;
            io.open(arg_val.c_str());
            {
                block_io_t block_io(io, true);
                sector_data_t data;
                io.read(data, 0);
                cfs_head_t head{};
                std::memcpy(&head, data.data(), sizeof(head));
                print_head(head);

                // allocation bitmap
                bitmap bmap(block_io, head.static_info.data_bitmap_start, head.static_info.data_bitmap_end,
                    head.static_info.data_table_end - head.static_info.data_table_start,
                    head.static_info.block_size);
                block_attr_t block_attr(block_io, head.static_info.block_size,
                    head.static_info.data_block_attribute_table_start, head.static_info.data_block_attribute_table_end,
                    head.static_info.data_table_end - head.static_info.data_table_start);
                std::cout << "Block Allocation Bitmap:\n      ";
                constexpr uint64_t line_max_entries = 140;
                for (uint64_t i = 0; i < head.static_info.data_table_end - head.static_info.data_table_start; i++)
                {
                    if (i % line_max_entries == 0 && i >= line_max_entries) {
                        std::cout << std::endl << "      ";
                    }

                    if (bmap.get(i)) {
                        auto attr = block_attr.get(i);
                        const auto blk_attr = *reinterpret_cast<cfs_blk_attr_t*>(&attr);
                        switch (blk_attr.type) {
                            case INDEX_TYPE: std::cout << color::color(0,4,0) << (blk_attr.frozen ? color::bg_color(3,0,0) : "") << 'I'; break;
                            case STORAGE_TYPE:
                            case POINTER_TYPE: std::cout << color::color(0,3,5) << (blk_attr.frozen ? color::bg_color(3,0,0) : "") << 'P'; break;
                            default: std::cout << color::color(5,0,5) << (blk_attr.frozen ? color::bg_color(5,5,5) : "") << 'R'; break;
                        }
                        std::cout << color::no_color();
                    } else {
                        std::cout << '.';
                    }
                }

                std::cout << std::endl;

                // journaling
                // journaling journal(block_io);
                // const auto journal_entries = journal.export_journaling();
                // for (const auto decoded = decoder_jentries(journal_entries);
                //     const auto & entry : decoded)
                // {
                //     std::cout << entry << std::endl;
                // }
                //
                // std::cout << std::endl << std::endl << std::endl;
                //
                // std::vector < entry_t > logs = journal_entries;
                // logs.erase(logs.begin());
                // logs.pop_back();
                // std::vector < entry_t > last_transaction;
                // std::vector < entry_t > active_transactions;
                // for (const auto & entry : logs)
                // {
                //     if (actions::ACTION_TRANSACTION_BEGIN < entry.operation_name && entry.operation_name < actions::ACTION_TRANSACTION_END) {
                //         active_transactions.emplace_back(entry);
                //     }
                //
                //     if (entry.operation_name > actions::ACTION_TRANSACTION_END) {
                //         if (!active_transactions.empty() && active_transactions.back().operation_name == entry.operands.done_action.action_name) {
                //             active_transactions.pop_back();
                //         }
                //     }
                //
                //     last_transaction.emplace_back(entry);
                //
                //     if (active_transactions.empty()) {
                //         last_transaction.clear();
                //     }
                // }
                //
                //
                // for (const auto decoded = decoder_jentries(last_transaction);
                //     const auto & entry : decoded)
                // {
                //     std::cout << entry << std::endl;
                // }
            }
            return EXIT_SUCCESS;
        }

        throw runtime_error("No path specified");
    } catch (const std::exception &e) {
        error_log(e.what());
        return EXIT_FAILURE;
    }
}
