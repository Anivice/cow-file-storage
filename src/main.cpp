#include "helper/log.h"
#include "service.h"

int main(int argc, char *argv[])
{
    try {
    } catch (const std::exception &e) {
        error_log(e.what());
    }
}
