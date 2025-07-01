#include "core/basic_io.h"
#include "helper/log.h"

int main(int argc, char *argv[])
{
    basic_io_t basic_io;
    basic_io.open("./file");
    sector_data_t buffer;
    basic_io.read(buffer, 4);
    debug::log(basic_io.get_file_sectors());
    basic_io.close();
}
