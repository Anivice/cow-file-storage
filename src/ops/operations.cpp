#include <unistd.h>
#include <sys/param.h>
#include <memory>
#include <sstream>
#include <functional>
#include <atomic>
#include "operations.h"
#include "service.h"
#include "helper/log.h"
#include "helper/cpp_assert.h"

std::unique_ptr < filesystem > filesystem_instance;
std::mutex operations_mutex;
std::atomic_bool content_changed_out_of_sync_to_fstat = true;
std::atomic_bool content_changed_out_of_sync_to_get_inode = true;
std::map < std::string /* path */, uint64_t /* inode */ > path_to_inode_fast_map;

static std::vector<std::string> splitString(const std::string& s, const char delim = '/')
{
    std::vector<std::string> parts;
    std::string token;
    std::stringstream ss(s);

    while (std::getline(ss, token, delim)) {
        parts.push_back(token);
    }

    assert_short(!parts.empty() && parts.front().empty());
    parts.erase(parts.begin());
    return parts;
}

template < typename InodeType >
[[nodiscard]] InodeType get_inode_by_path(const std::vector<std::string> & path_vec)
{
    uint64_t current_entry = 0;
    std::string current_path = "::";
    if (content_changed_out_of_sync_to_get_inode) {
        path_to_inode_fast_map.clear();
        content_changed_out_of_sync_to_get_inode = false;
    }

    for (const auto & entry : path_vec)
    {
        current_path += entry + "::";
        if (auto it = path_to_inode_fast_map.find(current_path); it != path_to_inode_fast_map.end()) {
            current_entry = it->second;
        } else {
            auto dir = filesystem_instance->make_inode<filesystem::directory_t>(current_entry);
            current_entry = dir.get_inode(entry);
            path_to_inode_fast_map[current_path] = current_entry;
        }
    }

    return filesystem_instance->make_inode<InodeType>(current_entry);
}

#define CATCH_TAIL                                                                  \
    catch (fs_error::no_such_file_or_directory &) {                                 \
        return -ENOENT;                                                             \
    } catch (fs_error::not_a_directory &) {                                         \
        return -ENOTDIR;                                                            \
    } catch (fs_error::is_a_directory &) {                                          \
        return -EISDIR;                                                             \
    } catch (fs_error::filesystem_space_depleted &) {                               \
        return -ENOSPC;                                                             \
    } catch (fs_error::operation_bot_permitted &) {                                 \
        return -EPERM;                                                              \
    } catch (fs_error::inode_exists&) {                                             \
        return -EEXIST;                                                             \
    } catch (std::exception &e) {                                                   \
        error_log("Unhandled exception: ", e.what());                               \
        return -EIO;                                                                \
    } catch (...) {                                                                 \
        error_log("Unhandled unknown exception");                                   \
        return -EIO;                                                                \
    }

#define RETURN_EROFS_IF_INODE_IS_FROZEN(inode)                                      \
    if ((inode).get_inode_blk_attr().frozen) {                                      \
        return -EROFS;                                                              \
    }

int do_getattr (const char *path, struct stat *stbuf)
{
    try {
        std::lock_guard lock(operations_mutex);
        auto inode = get_inode_by_path<filesystem::inode_t>(splitString(path));
        *stbuf = inode.get_header().attributes;
        return 0;
    }
    CATCH_TAIL
}

int do_readdir (const char *path, std::vector < std::string > & entries)
{
    try {
        std::lock_guard lock(operations_mutex);
        entries.emplace_back(".");
        entries.emplace_back("..");
        auto inode = get_inode_by_path<filesystem::directory_t>(splitString(path));

        for (auto dentries = inode.list_dentries();
            const auto & dentry : dentries | std::views::keys)
        {
            entries.push_back(dentry);
        }
        return 0;
    }
    CATCH_TAIL
}

int do_mkdir (const char * path, const mode_t mode)
{
    try {
        std::lock_guard lock(operations_mutex);
        auto path_vec = splitString(path);
        const auto target = path_vec.back();
        path_vec.pop_back();
        auto inode = get_inode_by_path<filesystem::directory_t>(path_vec);
        RETURN_EROFS_IF_INODE_IS_FROZEN(inode);
        inode.create_dentry(target, mode | S_IFDIR);
        content_changed_out_of_sync_to_fstat = true;
        content_changed_out_of_sync_to_get_inode = true;
        return 0;
    }
    CATCH_TAIL
}

int do_chown (const char * path, const uid_t uid, const gid_t gid)
{
    try
    {
        std::lock_guard lock(operations_mutex);
        auto inode = get_inode_by_path<filesystem::inode_t>(splitString(path));
        RETURN_EROFS_IF_INODE_IS_FROZEN(inode);
        auto header = inode.get_header();
        header.attributes.st_uid = uid;
        header.attributes.st_gid = gid;
        header.attributes.st_ctim = filesystem::inode_t::get_current_time();
        inode.save_header(header);
        return 0;
    }
    CATCH_TAIL;
}

int do_chmod (const char * path, const mode_t mode)
{
    try {
        std::lock_guard lock(operations_mutex);
        auto inode = get_inode_by_path<filesystem::inode_t>(splitString(path));
        RETURN_EROFS_IF_INODE_IS_FROZEN(inode);
        auto header = inode.get_header();
        header.attributes.st_mode = mode;
        header.attributes.st_ctim = filesystem::inode_t::get_current_time();
        inode.save_header(header);
        return 0;
    }
    CATCH_TAIL
}

int do_create (const char * path, const mode_t mode)
{
    try {
        std::lock_guard lock(operations_mutex);
        auto path_vec = splitString(path);
        const auto target = path_vec.back();
        path_vec.pop_back();
        auto inode = get_inode_by_path<filesystem::directory_t>(path_vec);
        RETURN_EROFS_IF_INODE_IS_FROZEN(inode);
        try {
            inode.get_inode(target);
            return -EEXIST;
        } catch (...) {}
        inode.create_dentry(target, mode);
        content_changed_out_of_sync_to_fstat = true;
        content_changed_out_of_sync_to_get_inode = true;
        return 0;
    }
    CATCH_TAIL
}

int do_flush (const char *)
{
    try {
        std::lock_guard lock(operations_mutex);
        filesystem_instance->sync();
        return 0;
    }
    CATCH_TAIL
}

int do_release (const char *)
{
    return 0;
}

int do_access (const char * path, int mode)
{
    try {
        std::lock_guard lock(operations_mutex);
        auto inode = get_inode_by_path<filesystem::inode_t>(splitString(path));
        auto fstat = inode.get_header().attributes;

        if (mode == F_OK)
        {
            return 0;
        }

        // permission check:
        mode <<= 6;
        mode &= 0x01C0;
        if (inode.get_inode_blk_attr().frozen) {
            fstat.st_mode &= 0500; // strip write permission
        }
        return -!(mode & fstat.st_mode);
    }
    CATCH_TAIL
}

int do_open (const char * path)
{
    try {
        std::lock_guard lock(operations_mutex);
        (void)get_inode_by_path<filesystem::inode_t>(splitString(path));
        return 0;
    }
    CATCH_TAIL
}

int do_read (const char *path, char *buffer, const size_t size, const off_t offset)
{
    try {
        std::lock_guard lock(operations_mutex);
        auto inode = get_inode_by_path<filesystem::inode_t>(splitString(path));
        return static_cast<int>(inode.read(buffer, size, offset));
    }
    CATCH_TAIL
}

int do_write (const char * path, const char * buffer, const size_t size, const off_t offset)
{
    try {
        std::lock_guard lock(operations_mutex);
        auto inode = get_inode_by_path<filesystem::inode_t>(splitString(path));
        RETURN_EROFS_IF_INODE_IS_FROZEN(inode);
        if (const auto [attributes] = inode.get_header();
            static_cast<uint64_t>(attributes.st_size) < (offset + size)) // expand on demand
        {
            inode.resize(size + offset);
        }
        content_changed_out_of_sync_to_fstat = true;
        return static_cast<int>(inode.write(buffer, size, offset));
    }
    CATCH_TAIL
}

int do_utimens (const char * path, const timespec tv[2])
{
    try {
        std::lock_guard lock(operations_mutex);
        auto inode = get_inode_by_path<filesystem::inode_t>(splitString(path));
        RETURN_EROFS_IF_INODE_IS_FROZEN(inode);
        auto header = inode.get_header();
        header.attributes.st_atim = tv[0];
        header.attributes.st_mtim = tv[1];
        inode.save_header(header);
        return 0;
    }
    CATCH_TAIL
}

int do_unlink (const char * path)
{
    try {
        std::lock_guard lock(operations_mutex);
        auto path_vec = splitString(path);
        const auto target = path_vec.back();
        path_vec.pop_back();
        auto inode = get_inode_by_path<filesystem::directory_t>(path_vec);
        const auto target_inode_id = inode.get_inode(target);
        auto target_inode = filesystem_instance->make_inode<filesystem::inode_t>(target_inode_id);
        RETURN_EROFS_IF_INODE_IS_FROZEN(inode);
        RETURN_EROFS_IF_INODE_IS_FROZEN(target_inode);
        if (target_inode.get_header().attributes.st_mode & S_IFDIR)
        {
            return -EISDIR;
        }

        inode.unlink_inode(target);
        content_changed_out_of_sync_to_fstat = true;
        content_changed_out_of_sync_to_get_inode = true;
        return 0;
    }
    CATCH_TAIL
}

int do_rmdir (const char * path)
{
    try {
        std::lock_guard lock(operations_mutex);
        auto path_vec = splitString(path);
        const auto target = path_vec.back();
        path_vec.pop_back();
        auto inode = get_inode_by_path<filesystem::directory_t>(path_vec);
        const auto child = inode.get_inode(target);
        auto child_inode = filesystem_instance->make_inode<filesystem::inode_t>(child);
        content_changed_out_of_sync_to_fstat = true;
        content_changed_out_of_sync_to_get_inode = true;
        if (child_inode.get_inode_blk_attr().frozen == 1) {
            return -EROFS;
        }
        if (child_inode.get_header().attributes.st_size != 0) {
            return -ENOTEMPTY;
        }
        inode.unlink_inode(target);
        return 0;
    }
    CATCH_TAIL
}

int do_fsync (const char *, int)
{
    try {
        std::lock_guard lock(operations_mutex);
        filesystem_instance->sync();
        return 0;
    }
    CATCH_TAIL
}

int do_releasedir (const char *)
{
    return 0;
}

int do_fsyncdir (const char *, int)
{
    try {
        std::lock_guard lock(operations_mutex);
        filesystem_instance->sync();
        return 0;
    }
    CATCH_TAIL
}

int do_truncate (const char * path, const off_t size)
{
    try {
        std::lock_guard lock(operations_mutex);
        auto inode = get_inode_by_path<filesystem::inode_t>(splitString(path));
        RETURN_EROFS_IF_INODE_IS_FROZEN(inode);
        inode.resize(size);
        content_changed_out_of_sync_to_fstat = true;
        return 0;
    }
    CATCH_TAIL
}

int do_symlink (const char * path, const char * target)
{
    try {
        std::lock_guard lock(operations_mutex);
        auto path_vec = splitString(target);
        const auto target_link = path_vec.back();
        path_vec.pop_back();
        auto inode = get_inode_by_path<filesystem::directory_t>(path_vec);
        RETURN_EROFS_IF_INODE_IS_FROZEN(inode);
        auto new_inode = inode.create_dentry(target_link, S_IFLNK | 0755);
        new_inode.resize(strlen(path));
        new_inode.write(path, strlen(path), 0);
        content_changed_out_of_sync_to_fstat = true;
        content_changed_out_of_sync_to_get_inode = true;
        return 0;
    }
    CATCH_TAIL
}

int do_snapshot(const char * name)
{
    try {
        std::lock_guard lock(operations_mutex);
        debug_log("Snapshot creation request, target at ", name);
        auto target_parent = splitString(name);
        const auto target = target_parent.back();
        target_parent.pop_back();
        if (!target_parent.empty()) {
            error_log("Snapshot cannot be created under any location other than root!");
            return -EPERM;
        }
        filesystem_instance->sync();
        auto root = get_inode_by_path<filesystem::directory_t>({});
        root.snapshot(target);
        filesystem_instance->sync();
        content_changed_out_of_sync_to_fstat = true;
        content_changed_out_of_sync_to_get_inode = true;
        debug_log("Snapshot creation completed for ", name);
        return 0;
    } CATCH_TAIL
}

int do_rollback(const char * name)
{
    try {
        std::lock_guard lock(operations_mutex);
        debug_log("Snapshot rollback request, target at ", name);
        auto target_parent = splitString(name);
        const auto target = target_parent.back();
        target_parent.pop_back();
        if (!target_parent.empty()) {
            error_log("Filesystem cannot be rolled back from any location other than under root!");
            return -EPERM;
        }
        auto root = get_inode_by_path<filesystem::directory_t>({});
        root.reset_as(target);
        filesystem_instance->sync();
        debug_log("Filesystem rollback completed, history inode is ", name);
        path_to_inode_fast_map.clear();
        content_changed_out_of_sync_to_fstat = true;
        content_changed_out_of_sync_to_get_inode = true;
        return 0;
    } CATCH_TAIL
}

int do_rename (const char * path, const char * name)
{
    try {
        std::lock_guard lock(operations_mutex);
        auto source_parent = splitString(path);
        const auto source = source_parent.back();
        source_parent.pop_back();

        auto target_parent = splitString(name);
        const auto target = target_parent.back();
        target_parent.pop_back();

        auto source_parent_dir = get_inode_by_path<filesystem::directory_t>(source_parent);
        auto target_parent_dir = get_inode_by_path<filesystem::directory_t>(target_parent);

        RETURN_EROFS_IF_INODE_IS_FROZEN(source_parent_dir);
        RETURN_EROFS_IF_INODE_IS_FROZEN(target_parent_dir);

        auto src_dentries = source_parent_dir.list_dentries();
        auto index_node_id = src_dentries.at(source);
        auto target_inode = filesystem_instance->make_inode<filesystem::inode_t>(index_node_id);
        RETURN_EROFS_IF_INODE_IS_FROZEN(target_inode);

        src_dentries.erase(source);
        source_parent_dir.save_dentries(src_dentries);

        auto target_dentries = target_parent_dir.list_dentries();
        if (target_dentries.contains(target)) {
            return -EEXIST;
        }
        target_dentries.emplace(target, index_node_id);
        target_parent_dir.save_dentries(target_dentries);
        content_changed_out_of_sync_to_fstat = true;
        content_changed_out_of_sync_to_get_inode = true;
        return 0;
    }
    CATCH_TAIL
}

int do_fallocate(const char * path, const int mode, const off_t offset, const off_t length)
{
    try {
        std::lock_guard lock(operations_mutex);
        auto inode = get_inode_by_path<filesystem::inode_t>(splitString(path));
        RETURN_EROFS_IF_INODE_IS_FROZEN(inode);
        inode.resize(offset + length);
        auto header = inode.get_header();
        header.attributes.st_mode = mode | S_IFREG;
        header.attributes.st_ctim = filesystem::inode_t::get_current_time();
        inode.save_header(header);
        content_changed_out_of_sync_to_fstat = true;
        return 0;
    }
    CATCH_TAIL
}

int do_fgetattr (const char * path, struct stat * statbuf)
{
    return do_getattr(path, statbuf);
}

int do_ftruncate (const char * path, const off_t length)
{
    return do_truncate(path, length);
}

int do_readlink (const char * path, char * buffer, const size_t size)
{
    try {
        std::lock_guard lock(operations_mutex);
        auto inode = get_inode_by_path<filesystem::inode_t>(splitString(path));
        if (auto header = inode.get_header(); (header.attributes.st_mode & S_IFMT) == S_IFLNK) {
            const auto len = inode.read(buffer, size, 0);
            buffer[std::min(len, size)] = '\0';
            return 0;
        }

        return -EINVAL;
    }
    CATCH_TAIL
}

void do_destroy ()
{
    std::lock_guard lock(operations_mutex);
    filesystem_instance.reset();
}

void do_init(const std::string & location)
{
    std::lock_guard lock(operations_mutex);
    filesystem_instance = std::make_unique<filesystem>(location.c_str());
}

int do_mknod (const char * path, const mode_t mode, const dev_t device)
{
    try
    {
        std::lock_guard lock(operations_mutex);
        auto path_vec = splitString(path);
        const auto target_name = path_vec.back();
        path_vec.pop_back();
        auto inode = get_inode_by_path<filesystem::directory_t>(path_vec);
        RETURN_EROFS_IF_INODE_IS_FROZEN(inode);
        auto new_inode = inode.create_dentry(target_name, mode);
        auto header = new_inode.get_header();
        header.attributes.st_dev = device;
        inode.save_header(header);
        content_changed_out_of_sync_to_fstat = true;
        content_changed_out_of_sync_to_get_inode = true;
        return 0;
    }
    CATCH_TAIL;
}

struct statvfs statvfs_5s_interval_cache;
std::chrono::time_point<std::chrono::system_clock> last_fstat_invoke_time;
std::mutex do_fstat_unique_mutex;

struct statvfs do_fstat()
{
    std::lock_guard lock_do_fstat(do_fstat_unique_mutex);
    const auto now = std::chrono::system_clock::now();
    if (const auto sec = std::chrono::duration_cast<std::chrono::seconds>(now - last_fstat_invoke_time).count();
        sec > 5 && content_changed_out_of_sync_to_fstat)
    {
        std::lock_guard lock(operations_mutex);
        statvfs_5s_interval_cache = filesystem_instance->fstat();
        last_fstat_invoke_time = now;
        content_changed_out_of_sync_to_fstat = false;
        return statvfs_5s_interval_cache;
    }

    return statvfs_5s_interval_cache;
}
