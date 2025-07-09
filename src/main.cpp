#include "helper/log.h"
#include "service.h"

int main(int argc, char *argv[])
{
    try {
        filesystem fs("./file");
        auto root = fs.get_root();
        auto dentries = root.list_dentries();
        // root.create_dentry("file1", S_IFREG);
        /*
        *  switch (sb.st_mode & S_IFMT) {
        *  case S_IFBLK:  printf("block device\n");            break;
        *  case S_IFCHR:  printf("character device\n");        break;
        *  case S_IFDIR:  printf("directory\n");               break;
        *  case S_IFIFO:  printf("FIFO/pipe\n");               break;
        *  case S_IFLNK:  printf("symlink\n");                 break;
        *  case S_IFREG:  printf("regular file\n");            break;
        *  case S_IFSOCK: printf("socket\n");                  break;
        *  default:       printf("unknown?\n");                break;
        */
        debug_log(root.list_dentries());
        auto file1 = root.get_inode("file1");
        file1.resize(13);
        file1.write("Hello, world!", 13, 0);
    } catch (const std::exception &e) {
        error_log(e.what());
    }
}
