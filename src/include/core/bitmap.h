/* bitmap.h
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

#ifndef BITMAP_H
#define BITMAP_H

#include "core/block_io.h"

class bitmap {
    block_io_t & block_mapping;
    const uint64_t map_start;
    const uint64_t map_end;
    const uint64_t boundary;
    const uint64_t blk_size;

public:
    explicit bitmap(block_io_t & block_mapping_,
        uint64_t map_start_, uint64_t map_end_, uint64_t boundary_ /* size, max index == size - 1 */,
        uint64_t block_size_);
    bool get(uint64_t) const;
    void set(uint64_t, bool);
    uint64_t hash();
};

#endif //BITMAP_H
