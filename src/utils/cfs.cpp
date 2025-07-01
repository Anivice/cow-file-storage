/* cfs.cpp
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

#include <atomic>
#include <string>
#include "helper/log.h"
#include "helper/get_env.h"
#include "core/cfs.h"
#include "helper/err_type.h"

int main(int argc, char **argv)
{
    try
    {
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        if (!get_env("VERBOSE").empty())
        {
            debug::verbose = true_false_helper(get_env("VERBOSE"));
            if (debug::verbose) verbose_log("Verbose mode enabled by environment variable");
        }
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        verbose_log("Determining executable route...")
        std::string executable_route(argv[0]);
        if (const auto pos = executable_route.find_last_of('/');
            pos != std::string::npos)
        {
            executable_route = executable_route.substr(pos + 1);
        }

        verbose_log("Route literal is ", executable_route);
        if (executable_route == "mkfs.cfs") {
            verbose_log("Route to mkfs.cfs");
            return mkfs_main(argc, argv);
        }

        if (executable_route == "mount.cfs") {
            verbose_log("Route to mount.cfs");
            return mount_main(argc, argv);
        }

        if (executable_route == "fsck.cfs") {
            verbose_log("Route to fsck.cfs");
            return fsck_main(argc, argv);
        }

        verbose_log("Unknown route");
        throw runtime_error("No meaningful route can be determined by literal " + executable_route);
    }
    catch (const std::exception & e)
    {
        error_log(e.what());
        return EXIT_FAILURE;
    }
}
