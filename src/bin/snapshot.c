#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define CFS_MAX_FILENAME_LENGTH 255
struct snapshot_ioctl_msg {
    char snapshot_name [CFS_MAX_FILENAME_LENGTH];
    uint64_t action;
};
#define CFS_PUSH_SNAPSHOT _IOW('M', 0x42, struct snapshot_ioctl_msg)
#define min(a, b) (((a) < (b)) ? (a) : (b))

const char * actions[] = { "create", "rollback", "delete" };

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: %s <filesystem mount point> <action:create/rollbackto/delete> <snapshot_name>\n", argv[0]);
        return EXIT_FAILURE;
    }

    auto const mountpoint = argv[1];
    auto const action = argv[2];
    auto const snapshot_name = argv[3];
    const int fd = open(mountpoint, O_DIRECTORY | O_RDONLY);
    if (fd < 0) {
        perror("open");
        return EXIT_FAILURE;
    }

    uint64_t action_determined = UINT64_MAX;
    for (uint64_t i = 0; i < sizeof(actions) / sizeof(actions[0]); i++) {
        if (!strncmp(actions[i], action, min(sizeof(action), strlen(actions[i])))) {
            action_determined = i;
        }
    }

    if (action_determined == UINT64_MAX) {
        fprintf(stderr, "%s is not a valid action\n", action);
        return EXIT_FAILURE;
    }

    struct snapshot_ioctl_msg irq = {};
    for (size_t i = 0; i < strlen(snapshot_name); i++)
    {
        if (snapshot_name[i] == '/') {
            fprintf(stderr, "Snapshot name cannot contain '/'!\n");
            return EXIT_FAILURE;
        }
    }
    strncpy(irq.snapshot_name, snapshot_name, CFS_MAX_FILENAME_LENGTH - 2);
    irq.action = action_determined;
    if (ioctl(fd, CFS_PUSH_SNAPSHOT, &irq) == -1) {
        perror("ioctl");
        return EXIT_FAILURE;
    }

    close(fd);
    return EXIT_SUCCESS;
}
