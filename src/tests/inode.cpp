#include "smart_block_t.h"
#include "cfsBasicComponents.h"
#include <fcntl.h>
#include <linux/falloc.h>
#include <unistd.h>
#include "utils.h"
#include <random>

int main(int argc, char ** argv)
{
    try
    {
        const char * disk = "bigfile.img";
        if (argc == 1)
        {
            const int fd = open(disk, O_RDWR | O_CREAT, 0644);
            assert_throw(fd > 0, "fd");
            assert_throw(fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, 0, 1024 * 1024 * 64) == 0, "fallocate() failed");
            assert_throw(fallocate(fd, FALLOC_FL_ZERO_RANGE, 0, 1024 * 1024 * 64) == 0, "fallocate() failed");
            close(fd);
            chmod(disk, 0755);
            cfs::make_cfs(disk, 512, "test");
            // return 0;
        }
        const int fd = open("out.png", O_RDWR | O_CREAT | O_TRUNC, 0644);
        cfs_assert_simple(fd > 0);

        cfs::filesystem fs(disk);
        cfs::cfs_journaling_t journal(&fs);
        cfs::cfs_bitmap_block_mirroring_t raid1_bitmap(&fs, &journal);
        cfs::cfs_block_attribute_access_t block_attribute(&fs, &journal);
        cfs::cfs_block_manager_t block_manager(&raid1_bitmap, &fs.cfs_header_block, &block_attribute, &journal);
        raid1_bitmap.set_bit(0, true); // mark 0 as allocated
        cfs::cfs_inode_service_t inode_service(0, &fs, &block_manager, &journal, &block_attribute);

        const cfs::basic_io::mmap file("1766215744-94d7707d-33baaa9f196336c7.png"); //test data
        inode_service.write(file.data(), file.size(), 0);

        std::vector<char> data;
        data.resize(file.size());
        inode_service.read(data.data(), file.size(), 0);
        write(fd, data.data(), data.size());
        close(fd);

        auto show = [&inode_service]
        {
            const auto [level1_pointers, level2_pointers, level3_pointers]
                = inode_service.linearize_all_blocks();
            dlog("LV1: ", level1_pointers, "\n LV2:", level2_pointers, "\n LV3:", level3_pointers, "\n\n");
        };

        show();
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
