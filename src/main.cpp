#include "helper/log.h"
#include "operations.h"
#include "service.h"

extern std::unique_ptr < filesystem > filesystem_instance;

int main(int argc, char *argv[])
{
    try {
        do_init("./file");
        debug_log("do_create: ", strerror(-do_create("/file1", S_IFREG | 0755)));
        debug_log("do_write: ",  strerror(-do_write("/file1", "Hello, World!", 13, 0)));
        debug_log("do_mkdir: ",  strerror(-do_mkdir("/dir1", S_IFDIR | 0755)));

        std::vector<std::string> dentries;
        do_readdir("/dir1", dentries);
        debug_log(dentries);

        debug_log("do_rename: ", strerror(-do_rename("/file1", "/dir1/file2")));
        debug_log("do_rename: ", strerror(-do_rename("/", "/dir2")));

        debug_log("do_truncate: ",  strerror(-do_truncate("/dir1/file2", 5)));
        debug_log("do_write: ",  strerror(-do_write("/dir1/file2", "AAAya", 5, 0)));

        dentries.clear();
        do_readdir("/dir1", dentries);
        debug_log(dentries);

        char buff [256]{};
        debug_log("do_read: ", strerror(-do_read("/dir1/file2", buff, sizeof(buff), 0)), ": ", buff);
        std::memset(buff, 0, sizeof(buff));
        debug_log("do_read: ", strerror(-do_read("/dir2/dir1/file2", buff, sizeof(buff), 0)), ": ", buff);
        // debug_log("do_unlink: ", strerror(-do_unlink("/dir1/file2")));

        dentries.clear();
        do_readdir("/dir2/dir1", dentries);
        debug_log(dentries);
        do_rmdir("/dir2");

        dentries.clear();
        debug_log("do_readdir: ", strerror(-do_readdir("/dir1", dentries)));
        debug_log(dentries);

        std::memset(buff, 0, sizeof(buff));
        debug_log("do_read: ", strerror(-do_read("/dir1/file2", buff, sizeof(buff), 0)), ": ", buff);

        do_destroy();
    } catch (const std::exception &e) {
        error_log(e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
