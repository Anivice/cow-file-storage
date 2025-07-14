/* mkfs.cpp
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

#define FUSE_USE_VERSION 31
#include <fuse.h>
#include <atomic>
#include <algorithm>
#include <sys/ioctl.h>
#include <pthread.h>
#include "service.h"
#include "helper/log.h"
#include "helper/arg_parser.h"
#include "helper/color.h"
#include "operations.h"

extern "C" struct snapshot_ioctl_msg {
    char snapshot_name [CFS_MAX_FILENAME_LENGTH];
    uint64_t action;
};
#define CFS_PUSH_SNAPSHOT _IOW('M', 0x42, struct snapshot_ioctl_msg)

#define CREATE      (0)
#define ROLLBACKTO  (1)

inline void set_thread_name(const char * name)
{
    pthread_setname_np(pthread_self(), name);
}

namespace mount {
    static std::string filesystem_path;
    static std::string filesystem_mount_destination;

    static int fuse_do_getattr (const char *path, struct stat *stbuf)
    {
        set_thread_name("fuse_do_getattr");
        return do_getattr(path, stbuf);
    }

    static int fuse_do_readdir (const char *path,
                    void *buffer,
                    const fuse_fill_dir_t filler,
                    off_t,
                    fuse_file_info *)
    {
        set_thread_name("fuse_do_readdir");
        std::vector<std::string> vector_buffer;
        const int status = do_readdir(path, vector_buffer);
        if (status != 0) return status;
        for (const auto & name : vector_buffer) {
            filler(buffer, name.c_str(), nullptr, 0);
        }
        return status;
    }

    static int fuse_do_mkdir (const char * path, const mode_t mode) {
        set_thread_name("fuse_do_mkdir");
        return do_mkdir(path, mode);
    }

    static int fuse_do_chmod (const char * path, const mode_t mode) {
        set_thread_name("fuse_do_chmod");
        return do_chmod(path, mode);
    }

    static int fuse_do_chown (const char * path, const uid_t uid, const gid_t gid) {
        set_thread_name("fuse_do_chown");
        return do_chown(path, uid, gid);
    }

    static int fuse_do_create (const char * path, const mode_t mode, fuse_file_info *) {
        set_thread_name("fuse_do_create");
        return do_create(path, mode);
    }

    static int fuse_do_flush (const char * path, fuse_file_info *) {
        set_thread_name("fuse_do_flush");
        return do_flush(path);
    }

    static int fuse_do_release (const char * path, fuse_file_info *) {
        set_thread_name("fuse_do_release");
        return do_release(path);
    }

    static int fuse_do_access (const char * path, const int mode) {
        set_thread_name("fuse_do_access");
        return do_access(path, mode);
    }

    static int fuse_do_open (const char * path, fuse_file_info *) {
        set_thread_name("fuse_do_open");
        return do_open(path);
    }

    static int fuse_do_read (const char *path, char *buffer, const size_t size, const off_t offset, fuse_file_info *) {
        set_thread_name("fuse_do_read");
        return do_read(path, buffer, size, offset);
    }

    static int fuse_do_write (const char * path, const char * buffer, const size_t size, const off_t offset, fuse_file_info *) {
        set_thread_name("fuse_do_write");
        return do_write(path, buffer, size, offset);
    }

    static int fuse_do_utimens (const char * path, const timespec tv[2]) {
        set_thread_name("fuse_do_utimens");
        return do_utimens(path, tv);
    }

    static int fuse_do_unlink (const char * path) {
        set_thread_name("fuse_do_unlink");
        return do_unlink(path);
    }

    static int fuse_do_rmdir (const char * path) {
        set_thread_name("fuse_do_rmdir");
        return do_rmdir(path);
    }

    static int fuse_do_fsync (const char * path, int, fuse_file_info *) {
        set_thread_name("fuse_do_fsync");
        return do_fsync(path, 0);
    }

    static int fuse_do_releasedir (const char * path, fuse_file_info *) {
        set_thread_name("fuse_do_releasedir");
        return do_releasedir(path);
    }

    static int fuse_do_fsyncdir (const char * path, int, fuse_file_info *) {
        set_thread_name("fuse_do_fsyncdir");
        return do_fsyncdir(path, 0);
    }

    static int fuse_do_truncate (const char * path, const off_t size) {
        set_thread_name("fuse_do_truncate");
        return do_truncate(path, size);
    }

    static int fuse_do_symlink (const char * path, const char * target) {
        set_thread_name("fuse_do_symlink");
        return do_symlink(path, target);
    }

    static int fuse_do_ioctl (const char *, const int cmd, void *,
        fuse_file_info *, const unsigned int flags, void * data)
    {
        set_thread_name("fuse_do_ioctl");
        if (!(flags & FUSE_IOCTL_DIR)) {
            return -ENOTTY;
        }

        if (cmd == CFS_PUSH_SNAPSHOT)
        {
            const auto * msg = static_cast<snapshot_ioctl_msg *>(data);
            if (msg->action == CREATE) {
                return do_snapshot(msg->snapshot_name);
            } else if (msg->action == ROLLBACKTO) {
                return do_rollback(msg->snapshot_name);
            }
        }

        return -EINVAL;
    }

    static int fuse_do_rename (const char * path, const char * name) {
        set_thread_name("fuse_do_rename");
        return do_rename(path, name);
    }

    static int fuse_do_fallocate(const char * path, const int mode, const off_t offset, const off_t length, fuse_file_info *) {
        set_thread_name("fuse_do_fallocate");
        return do_fallocate(path, mode, offset, length);
    }

    static int fuse_do_fgetattr (const char * path, struct stat * statbuf, fuse_file_info *) {
        set_thread_name("fuse_do_fgetattr");
        return fuse_do_getattr(path, statbuf);
    }

    static int fuse_do_ftruncate (const char * path, const off_t length, fuse_file_info *) {
        set_thread_name("fuse_do_ftruncate");
        return fuse_do_truncate(path, length);
    }

    static int fuse_do_readlink (const char * path, char * buffer, const size_t size) {
        set_thread_name("fuse_do_readlink");
        return do_readlink(path, buffer, size);
    }

    void fuse_do_destroy (void *) {
        set_thread_name("fuse_do_destroy");
        do_destroy();
    }

    void* fuse_do_init (fuse_conn_info *conn)
    {
        set_thread_name("fuse_do_init");
        conn->want |= FUSE_CAP_BIG_WRITES | FUSE_CAP_IOCTL_DIR;
        return nullptr;
    }

    static int fuse_do_mknod (const char * path, const mode_t mode, const dev_t device) {
        set_thread_name("fuse_do_mknod");
        return do_mknod(path, mode, device);
    }

    int fuse_statfs (const char *, struct statvfs * status)
    {
        set_thread_name("fuse_statfs");
        *status = do_fstat();
        return 0;
    }

    static fuse_operations fuse_operation_vector_table =
    {
        .getattr    = fuse_do_getattr,
        .readlink   = fuse_do_readlink,
        .mknod      = fuse_do_mknod,
        .mkdir      = fuse_do_mkdir,
        .unlink     = fuse_do_unlink,
        .rmdir      = fuse_do_rmdir,
        .symlink    = fuse_do_symlink,
        .rename     = fuse_do_rename,
        .chmod      = fuse_do_chmod,
        .chown      = fuse_do_chown,
        .truncate   = fuse_do_truncate,
        .open       = fuse_do_open,
        .read       = fuse_do_read,
        .write      = fuse_do_write,
        .statfs     = fuse_statfs,
        .flush      = fuse_do_flush,
        .release    = fuse_do_release,
        .fsync      = fuse_do_fsync,
        .opendir    = fuse_do_open,
        .readdir    = fuse_do_readdir,
        .releasedir = fuse_do_releasedir,
        .fsyncdir   = fuse_do_fsyncdir,
        .init       = fuse_do_init,
        .destroy    = fuse_do_destroy,
        .access     = fuse_do_access,
        .create     = fuse_do_create,
        .ftruncate  = fuse_do_ftruncate,
        .fgetattr   = fuse_do_fgetattr,
        .utimens    = fuse_do_utimens,
        .ioctl      = fuse_do_ioctl,
        .fallocate  = fuse_do_fallocate,
    };

    const arg_parser::parameter_vector Arguments = {
        { .name = "help",       .short_name = 'h', .arg_required = false,   .description = "Prints this help message" },
        { .name = "version",    .short_name = 'v', .arg_required = false,   .description = "Prints version" },
        { .name = "verbose",    .short_name = 'V', .arg_required = false,   .description = "Enable verbose output" },
        { .name = "fuse",       .short_name = 'f', .arg_required = true,    .description = "Arguments passed to fuse" },
    };

    void print_help(const std::string & program_name)
    {
        uint64_t max_name_len = 0;
        std::vector< std::pair <std::string, std::string>> output;
        const std::string required_str = " arg";
        for (const auto & [name, short_name, arg_required, description] : Arguments)
        {
            std::string name_str =
                (short_name == '\0' ? "" : "-" + std::string(1, short_name))
                += ",--" + name
                += (arg_required ? required_str : "");

            if (max_name_len < name_str.size())
            {
                max_name_len = name_str.size();
            }

            output.emplace_back(name_str, description);
        }

        std::cout << color::color(5,5,5) << program_name << color::no_color() << color::color(0,2,5) << " [options]" << color::no_color()
                  << std::endl << color::color(1,2,3) << "options:" << color::no_color() << std::endl;
        for (const auto & [name, description] : output)
        {
            std::cout << "    " << color::color(1,5,4) << name << color::no_color()
                      << std::string(max_name_len + 4 - name.size(), ' ')
                      << color::color(4,5,1) << description << color::no_color() << std::endl;
        }
    }

    static std::vector<std::string> splitString(const std::string& s, const char delim = ' ')
    {
        std::vector<std::string> parts;
        std::string token;
        std::stringstream ss(s);

        while (std::getline(ss, token, delim)) {
            parts.push_back(token);
        }

        return parts;
    }
}

int fuse_redirect(const int argc, char ** argv)
{
    fuse_args args = FUSE_ARGS_INIT(argc, argv);
    if (fuse_opt_parse(&args, nullptr, nullptr, nullptr) == -1)
    {
        std::cerr << "FUSE initialization failed, errno: "
                  << strerror(errno) << " (" << errno << ")" << std::endl;
        return EXIT_FAILURE;
    }

    do_init(mount::filesystem_path);
    const int ret = fuse_main(args.argc, args.argv, &mount::fuse_operation_vector_table, nullptr);
    fuse_opt_free_args(&args);
    return ret;
}

int mount_main(int argc, char **argv)
{
    try
    {
        arg_parser args(argc, argv, mount::Arguments);
        auto contains = [&args](const std::string & name, std::string & val)->bool
        {
            const auto it = std::ranges::find_if(args,
                [&name](const std::pair<std::string, std::string> & p)->bool{ return p.first == name; });
            if (it != args.end())
            {
                val = it->second;
                return true;
            }

            return false;
        };

        std::string arg_val;
        if (contains("help", arg_val)) // GNU compliance, help must be processed first if it appears and ignore all other arguments
        {
            mount::print_help(argv[0]);
            return EXIT_SUCCESS;
        }

        if (contains("version", arg_val))
        {
            std::cout << color::color(5,5,5) << argv[0] << color::no_color()
                << color::color(0,3,3) << " core version " << color::color(0,5,5) << CORE_VERSION
                << color::color(0,3,3) << " backend version " << color::color(0,5,5) << BACKEND_VERSION
                << color::no_color() << std::endl;
            return EXIT_SUCCESS;
        }

        if (contains("verbose", arg_val))
        {
            debug::verbose = true;
            verbose_log("Verbose mode enabled");
        }

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        std::unique_ptr<char*[]> fuse_argv;
        contains("fuse", arg_val);
        std::vector<std::string> fuse_args = mount::splitString(arg_val, ' ');
        if constexpr (DEBUG) {
            // fuse_args.emplace_back("-s");
            fuse_args.emplace_back("-d");
            fuse_args.emplace_back("-f");
        }

        std::vector<std::string> bares;
        for (const auto & [key, val] : args) {
            if (key.empty()) {
                bares.emplace_back(val);
            }
        }

        if (bares.size() != 2) {
            throw std::invalid_argument("Invalid arguments");
        }

        mount::filesystem_path = bares[0];
        mount::filesystem_mount_destination = bares[1];
        fuse_args.push_back(mount::filesystem_mount_destination);

        fuse_argv = std::make_unique<char*[]>(fuse_args.size() + 1);
        fuse_argv[0] = argv[0]; // redirect
        for (int i = 0; i < static_cast<int>(fuse_args.size()); ++i) {
            fuse_argv[i + 1] = const_cast<char *>(fuse_args[i].c_str());
        }

        debug_log("Mounting filesystem ", mount::filesystem_path, " to ", mount::filesystem_mount_destination);
        debug_log("Arguments passed down to fuse from command line are: ", fuse_args);

        const int d_fuse_argc = static_cast<int>(fuse_args.size()) + 1;
        char ** d_fuse_argv = fuse_argv.get();
        return fuse_redirect(d_fuse_argc, d_fuse_argv);
    }
    catch (const std::exception & e)
    {
        error_log(e.what());
        return EXIT_FAILURE;
    }
}
