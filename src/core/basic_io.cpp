/* basic_io.cpp
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

#include <unistd.h>
#include <fcntl.h>
#include "core/basic_io.h"
#include "helper/cpp_assert.h"
#include "helper/err_type.h"

void basic_io_t::open(const char *file_name)
{
    fd = ::open(file_name, /* O_DIRECT | */ O_RDWR /* | O_DSYNC | O_LARGEFILE | O_NOATIME | O_SYNC */);
    flock lk = {
        .l_type   = F_WRLCK,   // write lock
        .l_whence = SEEK_SET,  // lock the whole file
        .l_start  = 0,
        .l_len    = 0,
        .l_pid    = getpid(),
    };
    assert_throw(fcntl(fd, F_SETLKW, &lk) != -1, "fcntl lock");
    assert_throw(fd != -1, "Error opening file");
    const uint64_t size = lseek(fd, 0, SEEK_END);
    assert_throw(size != static_cast<uint64_t>(-1), "Error getting size of file");
    file_sectors = size / 512;
}

void basic_io_t::close()
{
    if (fd != -1) {
        flock lk = {
            .l_type   = F_UNLCK,
            .l_whence = SEEK_SET,
            .l_start  = 0,
            .l_len    = 0,
            .l_pid    =  getpid(),
        };
        fcntl(fd, F_SETLK, &lk);     // unlock
        ::close(fd);
        fd = -1;
    }
}

void basic_io_t::read(sector_data_t & buffer, const sector_t sector)
{
    if (sector >= file_sectors) {
        throw runtime_error("Error reading sector");
    }

    assert_short(lseek(fd, static_cast<long>(sector * 512), SEEK_SET) >= 0);
    assert_short(::read(fd, buffer.data(), 512) == 512);
}

void basic_io_t::write(const sector_data_t & buffer, const sector_t sector)
{
    if (sector >= file_sectors) {
        throw runtime_error("Error reading sector");
    }

    assert_short(lseek(fd, static_cast<long>(sector * 512), SEEK_SET) >= 0);
    assert_short(::write(fd, buffer.data(), 512) == 512);
}
