/* test.h
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

#ifndef TEST_H
#define TEST_H

#include <map>
#include <string>

namespace test {

class unit_t {
public:
    virtual ~unit_t() = default;
    unit_t() = default;
    virtual bool run() = 0;
    virtual std::string success() = 0;
    virtual std::string failure() = 0;
    virtual std::string name() = 0;
};

extern std::map < std::string, void * > unit_tests;
} // test

#endif //TEST_H
