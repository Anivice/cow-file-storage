#include "helper/log.h"
#include "service.h"

int main(int argc, char *argv[])
{
    try {
        filesystem fs("./file");
        filesystem::inode_t inode(fs, 0, 4096);
        inode.unblocked_resize(4096*10);
        debug_log(inode.linearized_level3_pointers());
    } catch (const std::exception &e) {
        error_log(e.what());
    }
}
