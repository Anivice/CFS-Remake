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
        cfs::cfs_block_attribute_access_t attribute(&fs, &journal);
        const uint64_t len = fs.static_info_.data_table_end - fs.static_info_.data_table_start;

        std::random_device dev, dev2;
        std::mt19937 rng(dev()), rng_result(dev());
        std::uniform_int_distribution<std::mt19937::result_type> dist6(0, len - 1);
        std::uniform_int_distribution<std::mt19937::result_type> result(1, 0xFFFF);
        std::mutex random_use_mutex;

        auto index_random = [&]->uint64_t {
            std::lock_guard<std::mutex> lock(random_use_mutex);
            return dist6(rng);
        };
        auto bit_random = [&]->uint16_t {
            std::lock_guard<std::mutex> lock(random_use_mutex);
            return static_cast<uint16_t>(result(rng_result));
        };

        std::map < uint64_t, uint64_t> bit_random_map;
        std::mutex bit_random_mutex;

        auto set = [&](const uint64_t index, const uint64_t attr)
        {
            std::lock_guard<std::mutex> lock(bit_random_mutex);
            bit_random_map[index] = attr;
        };

        auto T0 = [&](const uint64_t T)
        {
            pthread_setname_np(pthread_self(), ("T" + std::to_string(T)).c_str());

            for (int i = 0; i < len; i++)
            {
                const auto index = index_random();
                const auto bit = bit_random();
                attribute.set<cfs::index_node_referencing_number>(index, bit);
                set(index, bit);
            }
        };

        std::vector<std::thread> threads;
        pthread_setname_np(pthread_self(), "main");

        for (int i = 0; i < std::thread::hardware_concurrency(); i++) {
            threads.emplace_back(T0, i);
        }

        std::ranges::for_each(threads, [](std::thread & T) { if (T.joinable()) T.join(); });

        std::ranges::for_each(bit_random_map, [&](const std::pair < uint64_t, uint64_t > & pair) {
            cfs_assert_simple(attribute.get<cfs::index_node_referencing_number>(pair.first) == pair.second);
            dlog(attribute.get<cfs::index_node_referencing_number>(pair.first), ", ", pair.second, "\n");
        });

        uint64_t size = 0;
        for (int i = 0; i < len; i++) {
            size += attribute.get<cfs::index_node_referencing_number>(i) != 0;
        }
        cfs_assert_simple(size == bit_random_map.size());
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
