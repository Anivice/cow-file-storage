#include "core/block_io.h"
#include "helper/log.h"

int main(int argc, char *argv[])
{
    basic_io_t basic_io;
    basic_io.open("./file");

    block_io_t block_io(basic_io);

    basic_io.close();
}
