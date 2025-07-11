#ifndef OPERATIONS_H
#define OPERATIONS_H

#include <vector>
#include <string>
#include <sys/stat.h>
#include <cstdint>

int do_getattr (const char *path, struct stat *stbuf);
int do_readdir (const char *path, std::vector < std::string > & entries);
int do_mkdir (const char * path, mode_t mode);
int do_chown (const char * path, uid_t uid, gid_t gid);
int do_chmod (const char * path, mode_t mode);
int do_create (const char * path, mode_t mode);
int do_flush (const char * path);
int do_release (const char * path);
int do_access (const char * path, int mode);
int do_open (const char * path);
int do_read (const char *path, char *buffer, size_t size, off_t offset);
int do_write (const char * path, const char * buffer, size_t size, off_t offset);
int do_utimens (const char * path, const struct timespec tv[2]);
int do_unlink (const char * path);
int do_rmdir (const char * path);
int do_fsync (const char * path, int);
int do_releasedir (const char * path);
int do_fsyncdir (const char * path, int);
int do_truncate (const char * path, off_t size);
int do_symlink  (const char * path, const char * target);
int do_snapshot(const char * name);
int do_rename (const char * path, const char * name);
int do_fallocate(const char * path, int mode, off_t offset, off_t length);
int do_fgetattr (const char * path, struct stat * statbuf);
int do_ftruncate (const char * path, off_t length);
int do_readlink (const char * path, char * buffer, size_t size);
void do_destroy ();
void do_init(const std::string & location);
int do_mknod (const char * path, mode_t mode, dev_t device);
struct statvfs do_fstat();

#endif //OPERATIONS_H
