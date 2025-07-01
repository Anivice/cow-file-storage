/* log.h
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

#ifndef LOG_H
#define LOG_H

#include <iostream>
#include <mutex>
#include <map>
#include <unordered_map>
#include <atomic>
#include <iomanip>
#include <ranges>
#include <vector>
#include <tuple>      // for std::tuple, std::make_tuple
#include <utility>    // for std::forward
#include <any>
#include <cstring>
#include <regex>
#include <algorithm>
#include <ranges>
#include "color.h"

#define construct_simple_type_compare(type)                             \
    template <typename T>                                               \
    struct is_##type : std::false_type {};                              \
    template <>                                                         \
    struct is_##type<type> : std::true_type { };                        \
    template <typename T>                                               \
    constexpr bool is_##type##_v = is_##type<T>::value;

namespace debug {
    template <typename T, typename = void>
    struct is_container : std::false_type
    {
    };

    template <typename T>
    struct is_container<T,
        std::void_t<decltype(std::begin(std::declval<T>())),
        decltype(std::end(std::declval<T>()))>> : std::true_type
    {
    };

    template <typename T>
    constexpr bool is_container_v = is_container<T>::value;

    template <typename T>
    struct is_map : std::false_type
    {
    };

    template <typename Key, typename Value>
    struct is_map<std::map<Key, Value>> : std::true_type
    {
    };

    template <typename T>
    constexpr bool is_map_v = is_map<T>::value;

    template <typename T>
    struct is_unordered_map : std::false_type
    {
    };

    template <typename Key, typename Value, typename Hash, typename KeyEqual,
        typename Allocator>
    struct is_unordered_map<
        std::unordered_map<Key, Value, Hash, KeyEqual, Allocator>>
        : std::true_type
    {
    };

    template <typename T>
    constexpr bool is_unordered_map_v = is_unordered_map<T>::value;

    template <typename T>
    struct is_string : std::false_type
    {
    };

    template <>
    struct is_string<std::string> : std::true_type
    {
    };

    template <>
    struct is_string<const char*> : std::true_type
    {
    };

    template <>
    struct is_string<std::string_view> : std::true_type
    {
    };

    template <typename T>
    constexpr bool is_string_v = is_string<T>::value;

    construct_simple_type_compare(bool);

    inline class move_front_t {} move_front;
    construct_simple_type_compare(move_front_t);

    inline class cursor_off_t {} cursor_off;
    construct_simple_type_compare(cursor_off_t);

    inline class cursor_on_t {} cursor_on;
    construct_simple_type_compare(cursor_on_t);

    template <typename T>
    struct is_pair : std::false_type
    {
    };

    template <typename Key, typename Value>
    struct is_pair<std::pair<Key, Value>> : std::true_type
    {
    };

    template <typename T>
    constexpr bool is_pair_v = is_pair<T>::value;

    extern std::mutex log_mutex;
    extern std::atomic_bool verbose;
    template <typename ParamType>
    void _log(const ParamType& param);
    template <typename ParamType, typename... Args>
    void _log(const ParamType& param, const Args&... args);

    /////////////////////////////////////////////////////////////////////////////////////////////
    extern std::string str_true;
    extern std::string str_false;

    template <typename Container>
	requires (debug::is_container_v<Container> &&
		!debug::is_map_v<Container> &&
        !debug::is_unordered_map_v<Container>)
	void print_container(const Container& container)
    {
        constexpr uint64_t max_elements = 8;
        uint64_t num_elements = 0;
        _log("[");
        for (auto it = std::begin(container); it != std::end(container); ++it)
        {
            if (num_elements == max_elements)
            {
                _log("\n");
                _log("    ");
                num_elements = 0;
            }

            if (sizeof(*it) == 1 /* 8bit data width */)
            {
                const auto & tmp = *it;
                _log("0x", std::hex, std::setw(2), std::setfill('0'),
                    static_cast<unsigned>((*(uint8_t *)(&tmp)) & 0xFF)); // ugly workarounds
            }
            else {
                _log(*it);
            }

            if (std::next(it) != std::end(container)) {
                _log(", ");
            }

            num_elements++;
        }
        _log("]");
    }

	template <typename Map>
	requires (debug::is_map_v<Map> || debug::is_unordered_map_v<Map>)
	void print_container(const Map & map)
    {
    	_log("{");
    	for (auto it = std::begin(map); it != std::end(map); ++it)
    	{
    		_log(it->first);
    		_log(": ");
    		_log(it->second);
    		if (std::next(it) != std::end(map)) {
    			_log(", ");
    		}
    	}
    	_log("}");
    }

    template <typename ParamType> void _log(const ParamType& param)
    {
        // NOLINTBEGIN(clang-diagnostic-repeated-branch-body)
        if constexpr (debug::is_string_v<ParamType>) { // if we don't do it here, it will be assumed as a container
            std::cerr << param;
        }
        else if constexpr (debug::is_container_v<ParamType>) {
            debug::print_container(param);
        }
        else if constexpr (debug::is_bool_v<ParamType>) {
            std::cerr << (param ? "True" : "False");
        }
        else if constexpr (debug::is_pair_v<ParamType>) {
            std::cerr << "<";
            _log(param.first);
            std::cerr << ": ";
            _log(param.second);
            std::cerr << ">";
        }
        else if constexpr (debug::is_move_front_t_v<ParamType>) {
            std::cerr << "\033[F\033[K";
        }
        else if constexpr (debug::is_cursor_off_t_v<ParamType>) {
            std::cerr << "\033[?25l";
        }
        else if constexpr (debug::is_cursor_on_t_v<ParamType>) {
            std::cerr << "\033[?25h";
        }
        else {
            std::cerr << param;
        }
        // NOLINTEND(clang-diagnostic-repeated-branch-body)
    }

    template <typename ParamType, typename... Args>
    void _log(const ParamType& param, const Args &...args)
    {
        _log(param);
        (_log(args), ...);
    }

    extern bool do_i_show_caller_next_time;

    template < typename StringType >
    requires (std::is_same_v<StringType, std::string> || std::is_same_v<StringType, std::string_view>)
    bool _do_i_show_caller_next_time_(const StringType & str)
    {
        return (!str.empty() && str.back() == '\n');
    }

    inline bool _do_i_show_caller_next_time_(const char * str)
    {
        return (strlen(str) > 0 && str[strlen(str) - 1] == '\n');
    }

    inline bool _do_i_show_caller_next_time_(const char c)
    {
        return (c == '\n');
    }

    template<typename T>
    struct is_char_array : std::false_type {};

    template<std::size_t N>
    struct is_char_array<char[N]> : std::true_type {};

    template<std::size_t N>
    struct is_char_array<const char[N]> : std::true_type {};

    extern int caller_max_size;

    template <typename... Args> void log_with_caller(const char * caller, const Args &...args)
    {
        std::lock_guard<std::mutex> lock(log_mutex);
        static_assert(sizeof...(Args) > 0, "log(...) requires at least one argument");
        auto ref_tuple = std::forward_as_tuple(args...);
        using LastType = std::tuple_element_t<sizeof...(Args) - 1, std::tuple<Args...>>;
        const std::any last_arg = std::get<sizeof...(Args) - 1>(ref_tuple);

        if (do_i_show_caller_next_time)
        {
            const int current_caller_size = static_cast<int>(strlen(caller));
            if (caller_max_size == 0 || caller_max_size < current_caller_size)
            {
                caller_max_size = current_caller_size;
            }

            _log(color::color(0, 2, 2), "[", caller, "]",
                std::string(std::max(caller_max_size + 1 - current_caller_size, 1), ' '),
                color::no_color());
        }

        if constexpr (!is_char_array<LastType>::value)
        {
            do_i_show_caller_next_time = _do_i_show_caller_next_time_(std::any_cast<LastType>(last_arg));
        }
        else
        {
            do_i_show_caller_next_time = _do_i_show_caller_next_time_(std::any_cast<const char*>(last_arg));
        }

        _log(args...);
    }

    template <typename... Args> void log(const Args &...args)
    {
        std::lock_guard<std::mutex> lock(log_mutex);
        do_i_show_caller_next_time = true;
        _log(args...);
    }
}

#include <source_location>
namespace debug { std::string _strip_name_(const std::string & name); }
#define debug_log(...)      if (DEBUG) ::debug::log_with_caller(debug::_strip_name_(std::source_location::current().function_name()).c_str(), color::color(2,2,2), "[DEBUG]:   ", color::color(4,4,4), __VA_ARGS__, color::no_color(), "\n")
#define verbose_log(...)    if (::debug::verbose) ::debug::log_with_caller(debug::_strip_name_(std::source_location::current().function_name()).c_str(), color::color(2,2,2), "[VERBOSE]: ", color::no_color(), __VA_ARGS__, "\n");
#define console_log(...)    if (DEBUG) ::debug::log_with_caller(debug::_strip_name_(std::source_location::current().function_name()).c_str(), __VA_ARGS__, "\n"); else ::debug::log(__VA_ARGS__, "\n");
#define warning_log(...)    ::debug::log_with_caller(debug::_strip_name_(std::source_location::current().function_name()).c_str(), color::color(4,4,0), "[WARNING]: ", color::color(5,5,0), __VA_ARGS__, color::no_color(), "\n")
#define error_log(...)      ::debug::log_with_caller(debug::_strip_name_(std::source_location::current().function_name()).c_str(), color::color(4,0,0), "[ERROR]:   ", color::color(5,0,0), __VA_ARGS__, errno != 0 ? color::color(5,0,0) : color::color(0,4,0), "errno=", errno, " (", strerror(errno), ")", color::no_color(), "\n")

#endif // LOG_H
