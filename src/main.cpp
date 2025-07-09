#include "helper/log.h"
#include "operations.h"

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
        dentries.clear();
        do_readdir("/dir1", dentries);
        debug_log(dentries);

        char buff [256]{};
        debug_log("do_read: ", strerror(-do_read("/dir1/file2", buff, sizeof(buff), 0)), ": ", buff);
        do_destroy();
    } catch (const std::exception &e) {
        error_log(e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
