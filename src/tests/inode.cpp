#include "inode.h"
#include "smart_block_t.h"
#include "cfsBasicComponents.h"
#include <fcntl.h>
#include <filesystem>
#include <linux/falloc.h>
#include <unistd.h>
#include "utils.h"
#include <random>

int main(int argc, char ** argv)
{
    try
    {
        const char * disk = "bigfile.img";
        {
            if (std::filesystem::exists(disk)) {
                std::filesystem::remove(disk);
            }
            const int fd = open(disk, O_RDWR | O_CREAT, 0644);
            assert_throw(fd > 0, "fd");
            assert_throw(fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, 0, 1024 * 1024 * 64) == 0, "fallocate() failed");
            assert_throw(fallocate(fd, FALLOC_FL_ZERO_RANGE, 0, 1024 * 1024 * 64) == 0, "fallocate() failed");
            close(fd);
            chmod(disk, 0755);
            cfs::make_cfs(disk, 512, "test");
            // return 0;
        }
        cfs::filesystem fs(disk);
        cfs::cfs_journaling_t journal(&fs);
        cfs::cfs_bitmap_block_mirroring_t raid1_bitmap(&fs, &journal);
        cfs::cfs_block_attribute_access_t block_attribute(&fs, &journal);
        cfs::cfs_block_manager_t block_manager(&raid1_bitmap, &fs.cfs_header_block, &block_attribute, &journal);
        raid1_bitmap.set_bit(0, true); // mark 0 as allocated
        raid1_bitmap.set_bit(1, true);
        block_attribute.set<cfs::block_type>(1, cfs::INDEX_NODE_BLOCK);
        cfs::inode_t inode(0, &fs, &block_manager, &journal, &block_attribute, nullptr);
        cfs::inode_t inode1(1, &fs, &block_manager, &journal, &block_attribute, &inode);

        auto write = [&](const std::string & input, const std::string & output)
        {
            const int fd = open(output.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
            cfs_assert_simple(fd > 0);
            const cfs::basic_io::mmap file(input); //test data
            inode1.resize(file.size());
            inode1.write(file.data(), file.size(), 0);

            std::vector<char> data;
            data.resize(file.size());
            inode1.read(data.data(), file.size(), 0);
            ::write(fd, data.data(), data.size());
            close(fd);
        };

        cfs_assert_simple(argc == 5);
        for (int i = 0; i < 5; ++i) {
            const auto out1 = std::to_string(i) + "-" + std::string(argv[2]);
            const auto out2 = std::to_string(i) + "-" + std::string(argv[4]);
            dlog("Iteration i=", i, "\n");
            dlog("Iteration i=", i, " -- First half\n");
            write(argv[1], out1);
            dlog("Iteration i=", i, " -- Second half\n");
            write(argv[3], out2);
            dlog("Iteration i=", i, " finished\n");
        }

        inode.inode_copy_on_write(1);
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
