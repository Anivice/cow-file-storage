/* cfs.h
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

#ifndef CFS_H
#define CFS_H

#include <cstdint>

int mkfs_main(int argc, char **argv);
int mount_main(int argc, char **argv);
int fsck_main(int argc, char **argv);

constexpr uint64_t cfs_magick_number = 0xCFADBEEF20250701;

struct cfs_head_t
{
    uint64_t magick;    // fs magic
    struct {
        uint64_t sectors;               // sector numbers
        uint64_t block_over_sector;     // block_size = block_over_sector * sector_size (512)
        uint64_t block_size;
        uint64_t blocks;                // block numbers
        uint64_t data_bitmap_start;
        uint64_t data_bitmap_end;
        uint64_t data_bitmap_checksum;
        uint64_t data_bitmap_backup_start;
        uint64_t data_bitmap_backup_end;
        uint64_t data_block_attribute_table_start; // attribute is 16 byte for each data block
        uint64_t data_block_attribute_table_end;
        uint64_t data_table_start;
        uint64_t data_table_end;
        uint64_t journal_start;
        uint64_t journal_end;           // (journal_end - journal_start) == 32, journal is always 32 blocks
        uint64_t effective_blocks;
    } static_info; // static info
    uint64_t info_table_checksum;
    uint64_t magick_;
    struct {
        uint64_t mount_timestamp;       // when was the last time it's mounted
        uint64_t last_check_timestamp;  // last time check ran
        struct {
            uint64_t clean:1;
        } flags;
    } runtime_info;
    uint64_t info_table_checksum_;

    struct {
        uint64_t _1;
        uint64_t _2;
        uint64_t _3;
        uint64_t _4;
        uint64_t _5;
        uint64_t _6;
        uint64_t _7;
        uint64_t _8;
        uint64_t _9;
        uint64_t _10;
        uint64_t _11;
        uint64_t _12;
        uint64_t _13;
        uint64_t _14;
        uint64_t _15;
        uint64_t _16;
        uint64_t _17;
        uint64_t _18;
        uint64_t _19;
        uint64_t _20;
        uint64_t _21;
        uint64_t _22;
        uint64_t _23;
        uint64_t _24;
        uint64_t _25;
        uint64_t _26;
        uint64_t _27;
        uint64_t _28;
        uint64_t _29;
        uint64_t _30;
        uint64_t _31;
        uint64_t _32;
        uint64_t _33;
        uint64_t _34;
        uint64_t _35;
        uint64_t _36;
        uint64_t _37;
        uint64_t _38;
        uint64_t _39;
        uint64_t _40;
        uint64_t _41;
    } _reserved_;
};
static_assert(sizeof(cfs_head_t) == 512, "Faulty head size");

#endif //CFS_H
