/* arg_parser.h
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

#ifndef ARG_PARSER_H
#define ARG_PARSER_H

#include <string>
#include <vector>

class arg_parser
{
private:
    using arg_vector = std::vector < std::pair <std::string, std::string> >;
    arg_vector args;

public:
    struct parameter_t
    {
        std::string name;
        char short_name;
        bool arg_required;
        std::string description;
    };

    using parameter_vector = std::vector < parameter_t >;

    arg_parser(int argc, char ** argv, const parameter_vector & parameters);

    using iterator = arg_vector::iterator;
    using const_iterator = arg_vector::const_iterator;
    iterator begin() { return args.begin(); }
    iterator end() { return args.end(); }
    [[nodiscard]] const_iterator begin() const { return args.begin(); }
    [[nodiscard]] const_iterator end() const { return args.end(); }
};

#endif //ARG_PARSER_H
