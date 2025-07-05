#include "helper/log.h"
#include "service.h"

int main(int argc, char *argv[])
{
    try {
        filesystem fs("./file");
        auto new_block = fs.allocate_new_block();
        debug_log(new_block);
        // fs.deallocate_block(new_block);
        // fs.revert_transaction();
    } catch (const std::exception &e) {
        error_log(e.what());
    }
}
