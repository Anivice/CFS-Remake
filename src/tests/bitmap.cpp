#include <iostream>
#include <random>
#include <thread>
#include "colors.h"
#include "generalCFSbaseError.h"
#include "smart_block_t.h"
#include "utils.h"

class bitmap : public cfs::bitmap_base {
private:
    std::vector <uint8_t> data;

public:
    explicit bitmap(const uint64_t size)
    {
        init_data_array = [&](const uint64_t bytes)->bool
        {
            try {
                data.resize(bytes, 0);
                data_array_ = data.data();
                return true;
            } catch (...) {
                return false;
            }
        };

        cfs::bitmap_base::init(size);
    }
};

int main()
{
    try
    {
        constexpr uint64_t len = 512 * 1000 * 1000 * 1000ull / 4096;
        bitmap bitmap(len);

        std::random_device dev, dev2;
        std::mt19937 rng(dev()), rng_result(dev());
        std::uniform_int_distribution<std::mt19937::result_type> dist6(0, len - 1);
        std::uniform_int_distribution<std::mt19937::result_type> result(0, 1);

        auto index_random = [&]->uint64_t { return dist6(rng); };
        auto bit_random = [&]->bool { return static_cast<bool>(result(rng_result) & 0x01); };

        std::atomic < uint64_t > total_positives = 0;
        for (uint64_t i = 0; i < len; i++)
        {
            const auto index = index_random();
            if (!bitmap.get_bit(index))
            {
                const auto set_result = bit_random();
                bitmap.set_bit(index, set_result);
                if (set_result) ++total_positives;
                if (bitmap.get_bit(index) != set_result) {
                    return EXIT_FAILURE;
                }
            }
        }

        auto Check = [&]
        {
            uint64_t total = 0;
            for (uint64_t i = 0; i < len; i++) {
                total += bitmap.get_bit(i);
            }

            if (total != total_positives) {
                std::abort();
            }
        };

        std::vector<std::thread> threads;
        for (int i = 0; i < /* std::thread::hardware_concurrency() */ 2; i++) {
            threads.emplace_back(Check);
        }

        std::ranges::for_each(threads, [](std::thread & T0) {
            if (T0.joinable()) T0.join();
        });

        return EXIT_SUCCESS;
    }
    catch (cfs::error::generalCFSbaseError & e) {
        elog(e.what(), "\n");
    }
    catch (std::exception & e) {
        elog(e.what(), "\n");
    }

    return 0;
}
