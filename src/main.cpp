#include "helper/log.h"
#include "service.h"

int main(int argc, char *argv[])
{
    try {
        filesystem fs("./file");

        filesystem::inode_t inode(fs, 0, 4096);
        inode.unblocked_resize(1024*13);
        inode.unblocked_resize(512*13);
    } catch (const std::exception &e) {
        error_log(e.what());
    }
}
