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
        const uint64_t len = fs.static_info_.data_table_end - fs.static_info_.data_table_start;

        std::random_device dev, dev2;
        std::mt19937 rng(dev()), rng_result(dev());
        std::uniform_int_distribution<std::mt19937::result_type> dist6(0, len - 1);
        std::uniform_int_distribution<std::mt19937::result_type> result(0, 1);
        std::mutex random_use_mutex;

        auto index_random = [&]->uint64_t {
            std::lock_guard<std::mutex> lock(random_use_mutex);
            return dist6(rng);
        };
        auto bit_random = [&]->bool {
            std::lock_guard<std::mutex> lock(random_use_mutex);
            return static_cast<bool>(result(rng_result) & 0x01);
        };

        std::map < uint64_t, bool > reflection;
        std::mutex reflection_mutex;

        for (auto i = 0ull; i < len; i++) {
            raid1_bitmap.set_bit(i, true);
            reflection[i] = true;
        }

        auto set_reflection = [&](const uint64_t index, const bool new_bit)
        {
            std::lock_guard<std::mutex> lock(reflection_mutex);
            reflection[index] = new_bit;
        };

        auto T0 = [&](const uint64_t index)
        {
            pthread_setname_np(pthread_self(), ("T" + std::to_string(index)).c_str());
            for (uint64_t i = 0; i < len; i++)
            {
                std::lock_guard<std::mutex> lock(reflection_mutex);
                const auto pos = index_random();
                const auto set_result = bit_random();
                raid1_bitmap.set_bit(pos, set_result);
                set_reflection(pos, set_result);
            }
        };

        std::vector<std::thread> threads;
        pthread_setname_np(pthread_self(), "main");

        for (int i = 0; i < std::thread::hardware_concurrency(); i++) {
            threads.emplace_back(T0, i);
        }

        std::ranges::for_each(threads, [](std::thread & T) { if (T.joinable()) T.join(); });

        uint64_t total_positives_in_map = 0, total_positives_in_reflection = 0 /*, total_positives_in_reflection2 = 0*/;
        for (auto i = 0ull; i < len; i++) {
            total_positives_in_map += raid1_bitmap.get_bit(i);
        }

        for (const auto & val : reflection | std::views::values) {
            total_positives_in_reflection += val;
        }

        // for (const auto & val : raid1_bitmap.debug_map_ | std::views::values) {
            // total_positives_in_reflection2 += val;
        // }

        dlog(total_positives_in_map, ", ", total_positives_in_reflection, /* ", ", total_positives_in_reflection2, */ "\n");
        cfs_assert_simple(total_positives_in_reflection == total_positives_in_map /* && total_positives_in_map == total_positives_in_reflection2 */);
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
