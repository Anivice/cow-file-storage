#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CFS_MAX_FILENAME_LENGTH 128
struct snapshot_ioctl_msg { char snapshot_name [CFS_MAX_FILENAME_LENGTH]; };
#define CFS_PUSH_SNAPSHOT _IOW('M', 0x42, struct snapshot_ioctl_msg)

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s <filesystem mount point> <destination>\n", argv[0]);
        return EXIT_FAILURE;
    }

    auto const mountpoint = argv[1];
    auto const destination = argv[2];
    const int fd = open(mountpoint, O_DIRECTORY | O_RDONLY);
    if (fd < 0) {
        perror("open");
        return EXIT_FAILURE;
    }

    struct snapshot_ioctl_msg irq = {};
    strncpy(irq.snapshot_name, destination, CFS_MAX_FILENAME_LENGTH - 1);
    if (ioctl(fd, CFS_PUSH_SNAPSHOT, &irq) == -1) {
        perror("ioctl");
        return EXIT_FAILURE;
    }

    close(fd);
    return EXIT_SUCCESS;
}
