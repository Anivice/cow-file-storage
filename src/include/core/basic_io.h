/* basic_io.h
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

#ifndef BASIC_IO_H
#define BASIC_IO_H

#include <cstdint>
#include <array>

/// sector index type
using sector_t = unsigned long long int;
using sector_data_t = std::array<uint8_t, 512>;

/// basic IO using low-level system calls only
class basic_io_t
{
    int fd = -1; /// file descriptor
    sector_t file_sectors = 0; /// sector count, sector is always 512 bytes

public:
    basic_io_t() = default;
    ~basic_io_t() { close(); }
    void open(const char *file_name);
    void close();
    void read(sector_data_t & buffer, sector_t) const;
    void write(const sector_data_t &buffer, sector_t);
    [[nodiscard]] sector_t get_file_sectors() const { return file_sectors; }
};

#endif //BASIC_IO_H
