#include "helper/log.h"
#include "service.h"

int main(int argc, char *argv[])
{
    try {
        filesystem fs("./file");
        // auto new_block = fs.allocate_new_block();
        // auto attr = fs.get_attr(new_block);
        // attr.type = POINTER_TYPE;
        // fs.set_attr(new_block, attr);
        // debug_log(new_block);
        fs.freeze_block();
        // fs.deallocate_block(new_block);
        fs.clear_frozen_all();
    } catch (const std::exception &e) {
        error_log(e.what());
    }
}
