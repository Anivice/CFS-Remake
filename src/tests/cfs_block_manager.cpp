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
            assert_throw(fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, 0, cfs::cfs_minimum_size) == 0, "fallocate() failed");
            assert_throw(fallocate(fd, FALLOC_FL_ZERO_RANGE, 0, cfs::cfs_minimum_size) == 0, "fallocate() failed");
            close(fd);
            chmod(disk, 0755);
            cfs::make_cfs(disk, 512, "test");
        }
        cfs::filesystem fs(disk);
        cfs::cfs_journaling_t journal(&fs);
        cfs::cfs_bitmap_block_mirroring_t raid1_bitmap(&fs, &journal);
        cfs::cfs_block_attribute_access_t block_attribute(&fs, &journal);
        cfs::cfs_block_manager_t block_manager(&raid1_bitmap, &fs.cfs_header_block, &block_attribute, &journal);
        const uint64_t len = fs.static_info_.data_table_end - fs.static_info_.data_table_start;

        auto T0 = [&](const uint64_t index)
        {
            pthread_setname_np(pthread_self(), ("T" + std::to_string(index)).c_str());
            for (uint64_t i = 0; i < len; i++) {
                (void)block_manager.allocate();
            }
        };

        std::vector<std::thread> threads;
        pthread_setname_np(pthread_self(), "main");

        for (int i = 0; i < std::thread::hardware_concurrency(); i++) {
            threads.emplace_back(T0, i);
        }

        std::ranges::for_each(threads, [](std::thread & T) { if (T.joinable()) T.join(); });
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
