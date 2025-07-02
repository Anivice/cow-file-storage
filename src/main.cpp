#include "core/block_io.h"
#include "helper/log.h"
#include "core/bitmap.h"
#include "helper/cpp_assert.h"

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
        bitmap bmap(block_io, 1, 4, 12288, cfs_head.static_info.block_size);
        for (int i = 0; i < 12288 * 7; i++)
        {
            int index = rand() % 12288;
            bool val;
            for (int j = 0; j < 8; j++) {
                val = rand() % 2; bmap.set(index, val); assert_short(bmap.get(index) == val);
            }
            bmap.set(index, true); assert_short(bmap.get(index));
            bmap.set(index, false); assert_short(!bmap.get(index));
        }

        for (int i = 0; i < 12288; i++) {
            assert_short(!bmap.get(i));
        }
    }

    basic_io.close();
}
