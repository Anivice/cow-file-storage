/* err_type.h
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

#ifndef ERR_TYPE_H
#define ERR_TYPE_H

#include <stdexcept>
#include "backtrace.h"
#include "color.h"

class runtime_error : public std::runtime_error
{
    std::string additional;
public:
    explicit runtime_error(const std::string& what_arg) : std::runtime_error(what_arg)
    {
        additional = color::color(5,0,0) + what_arg + color::no_color();
        if (const std::string bt = debug::backtrace(); !bt.empty())
        {
            additional += "\n";
            additional += bt;
        }
        else
        {
            additional += color::color(2,2,0) + "\nSet BACKTRACE_LEVEL=1 or 2 to see detailed backtrace information\n" + color::no_color();
        }
    }

    [[nodiscard]] const char* what() const noexcept override
    {
        return additional.c_str();
    }
};

#endif //ERR_TYPE_H
