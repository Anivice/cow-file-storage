/* get_env.h
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

#ifndef GET_ENV_H
#define GET_ENV_H

// env variables
#define BACKTRACE_LEVEL "BACKTRACE_LEVEL"
#define COLOR           "COLOR"
#define TRIM_SYMBOL     "TRIM_SYMBOL"

#include <string>
#include <functional>

std::string get_env(const std::string & name);
bool true_false_helper(std::string val);
std::string replace_all(std::string & original, const std::string & target, const std::string & replacement);
std::string regex_replace_all(std::string & original, const std::string & pattern, const std::function<std::string(const std::string &)>& replacement);

template <typename IntType>
IntType get_variable(const std::string & name)
{
    const auto var = get_env(name);
    if (var.empty())
    {
        return 0;
    }

    return static_cast<IntType>(strtoll(var.c_str(), nullptr, 10));
}

#endif //GET_ENV_H
