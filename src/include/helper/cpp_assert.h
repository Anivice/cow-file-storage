/* cpp_assert.h
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

#ifndef CPP_ASSERT_H
#define CPP_ASSERT_H

#include "err_type.h"

inline void assert_throw(const bool condition, const std::string & message)
{
    if (!condition)
    {
        throw runtime_error(message);
    }
}

#define _line_str(x) #x
#define line_str(x) _line_str(x)
#define assert_short(condition) assert_throw(condition, __FILE__ ":" line_str(__LINE__) ": " #condition)

#endif //CPP_ASSERT_H
