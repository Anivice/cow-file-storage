#include "core/block_io.h"
#include "helper/log.h"
#include "helper/cpp_assert.h"
#include "core/ring_buffer.h"

int main(int argc, char *argv[])
{
    basic_io_t basic_io;
    basic_io.open("./file");

    {
        block_io_t block_io(basic_io);
        cfs_head_t cfs_head{};
        auto & head = block_io.at(0);
        // auto & data = block_io.at(1);
        head.get((uint8_t*)&cfs_head, sizeof(cfs_head), 0);
        // data.update((uint8_t*)"FUCK", 4, 2);
        // bitmap bmap(block_io, 1, 4, 12288, cfs_head.static_info.block_size);
        // for (int i = 0; i < 12288 * 7; i++)
        // {
        //     int index = rand() % 12288;
        //     bool val;
        //     for (int j = 0; j < 8; j++) {
        //         val = rand() % 2; bmap.set(index, val); assert_short(bmap.get(index) == val);
        //     }
        //     bmap.set(index, true); assert_short(bmap.get(index));
        //     bmap.set(index, false); assert_short(!bmap.get(index));
        // }

        // for (int i = 0; i < 12288; i++) {
        //     assert_short(!bmap.get(i));
        // }
        ring_buffer buffer(block_io, cfs_head.static_info.block_size, 1, 5);
        std::vector<uint8_t> data, data2;
        data.resize(338);
        data2.resize(338);

        for (int k = 0; k < 510; k++)
        {
            for (int i = 0; i < 128; i++)
            {
                for (auto & c : data) {
                    c = rand() % 255;
                }

                buffer.write(data.data(), data.size());
            }

            while (buffer.read(data2.data(), data2.size()));
        }

        for (int i = 0; i < 32*1024*7; i++)
        {
            uint8_t byte1 = rand() % 255, byte2 = rand() % 255, byte3 = rand() % 255, byte_r;
            buffer.write(&byte1, 1);
            buffer.write(&byte2, 1);
            buffer.write(&byte3, 1);
            buffer.read(&byte_r, 1);
            buffer.read(&byte_r, 1);
            buffer.read(&byte_r, 1);
            assert_short(byte3 == byte_r);
        }
    }

    basic_io.close();
}
