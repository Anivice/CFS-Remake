#include "smart_block_t.h"
#include <fcntl.h>
#include <linux/falloc.h>
#include <unistd.h>
#include "utils.h"

int main(int argc, char ** argv)
{
    try
    {
        const char * disk = "bigfile.img";
        switch (argc)
        {
            case 1:
            case 2:
            {
                const int fd = open(disk, O_RDWR | O_CREAT);
                assert_throw(fd > 0, "fd");
                assert_throw(fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, 0, cfs::cfs_minimum_size) == 0, "fallocate() failed");
                assert_throw(fallocate(fd, FALLOC_FL_ZERO_RANGE, 0, cfs::cfs_minimum_size) == 0, "fallocate() failed");
                close(fd);
                if (argc == 2) cfs::make_cfs(disk, 4096, "test");
            }
            break;
            default: break;
        }
        cfs::filesystem fs(disk);
    }
    catch (cfs::error::generalCFSbaseError & e) {
        elog(e.what(), "\n");
        return EXIT_FAILURE;
    }
    catch (std::exception& e) {
        elog(e.what(), "\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
