/* get_env.cpp
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

#include <cstdlib>
#include <algorithm>
#include <regex>
#include "helper/get_env.h"

std::string get_env(const std::string & name)
{
    const char * ptr = std::getenv(name.c_str());
    if (ptr == nullptr)
    {
        return "";
    }

    return ptr;
}

bool true_false_helper(std::string val)
{
    std::ranges::transform(val, val.begin(), ::tolower);
    if (val == "true") {
        return true;
    } else if (val == "false") {
        return false;
    } else {
        try {
            return std::stoi(val) != 0;
        } catch (...) {
            return false;
        }
    }
}

std::string replace_all(
    std::string & original,
    const std::string & target,
    const std::string & replacement)
{
    if (target.empty()) return original; // Avoid infinite loop if target is empty

    if (target.size() == 1 && replacement.empty()) {
        std::erase_if(original, [&target](const char c) { return c == target[0]; });
        return original;
    }

    size_t pos = 0;
    while ((pos = original.find(target, pos)) != std::string::npos) {
        original.replace(pos, target.length(), replacement);
        pos += replacement.length(); // Move past the replacement to avoid infinite loop
    }
    return original;
}

std::string regex_replace_all(std::string & original, const std::string & pattern, const std::function<std::string(const std::string &)>& replacement)
{
    std::vector < std::string > replace_list;
    const std::regex pattern_rgx(pattern);
    const auto matches_begin = std::sregex_iterator(begin(original), end(original), pattern_rgx);
    const auto matches_end = std::sregex_iterator();
    for (std::sregex_iterator i = matches_begin; i != matches_end; ++i)
    {
        const auto match = i->str();
        replace_list.emplace_back(match);
    }

    for (const auto & word : replace_list)
    {
        replace_all(original, word, replacement(word));
    }

    return original;
}
