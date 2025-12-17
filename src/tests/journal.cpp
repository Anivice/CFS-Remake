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
        cfs::cfs_journaling_t journaling(&fs);
        std::vector < cfs::cfs_action_t > action_mirror;
        const auto cell_size = (fs.static_info_.journal_end - fs.static_info_.journal_start) * fs.static_info_.block_size / sizeof(cfs::cfs_action_t);

        auto push_action = [&](
            const uint64_t action,
            const uint64_t action_param0 = 0,
            const uint64_t action_param1 = 0,
            const uint64_t action_param2 = 0,
            const uint64_t action_param3 = 0,
            const uint64_t action_param4 = 0)
        {
            cfs::cfs_action_t j_action = { };
            j_action.cfs_magic = cfs::cfs_magick_number;
            j_action.action_data.action_plain = {
                .action = action,
                .action_param0 = action_param0,
                .action_param1 = action_param1,
                .action_param2 = action_param2,
                .action_param3 = action_param3,
                .action_param4 = action_param4,
            };
            j_action.action_param_crc64 = cfs::utils::arithmetic::hashcrc64((uint8_t*)&j_action.action_data, sizeof(j_action.action_data));
            action_mirror.push_back(j_action);
            while (action_mirror.size() >= cell_size) action_mirror.erase(action_mirror.begin());
        };

        for (int i = 0; i < cell_size * 4; i++) {
            journaling.push_action(i / cell_size, i % cell_size, i);
            push_action(i / cell_size, i % cell_size, i);
        }
        const auto ac = journaling.dump_actions();
        cfs_assert_simple(ac.size() == action_mirror.size());
        int index = 0;
        std::ranges::for_each(action_mirror, [&](const cfs::cfs_action_t & action) {
            cfs_assert_simple(std::memcmp(&ac[index++], &action, sizeof(cfs::cfs_action_t)) == 0);
        });
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
