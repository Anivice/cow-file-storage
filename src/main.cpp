#include "core/block_io.h"
#include "helper/log.h"
#include "core/bitmap.h"

int main(int argc, char *argv[])
{
    basic_io_t basic_io;
    basic_io.open("./file");

    {
        block_io_t block_io(basic_io);
        cfs_head_t cfs_head;
        auto & head = block_io.at(0);
        auto & data = block_io.at(1);
        head.get((uint8_t*)&cfs_head, sizeof(cfs_head), 0);
        data.update((uint8_t*)"FUCK", 4, 2);
        bitmap bmap(block_io, 1, 2, 32, cfs_head.static_info.block_size);
        auto val = bmap.get(2);
        bmap.set(2, true);
        val = bmap.get(2);
        bmap.set(2, false);
        val = bmap.get(2);
    }

    basic_io.close();
}
