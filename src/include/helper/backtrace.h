/* backtrace.h
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

#ifndef BACKTRACE_H
#define BACKTRACE_H

#include <string>
#include <atomic>

namespace debug {
    std::string backtrace();
    extern std::atomic_int g_pre_defined_level;
    extern std::atomic_bool g_trim_symbol;
    bool true_false_helper(std::string val);
    std::string demangle(const char* mangled);
}

#endif //BACKTRACE_H
