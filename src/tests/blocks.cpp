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
        if (argc== 1)
        {
            const int fd = open(disk, O_RDWR | O_CREAT, 0600);
            assert_throw(fd > 0, "fd");
            assert_throw(fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, 0, cfs::cfs_minimum_size) == 0, "fallocate() failed");
            assert_throw(fallocate(fd, FALLOC_FL_ZERO_RANGE, 0, cfs::cfs_minimum_size) == 0, "fallocate() failed");
            close(fd);
            chmod(disk, 0755);
            cfs::make_cfs(disk, 512, "test");
        }
        cfs::filesystem fs(disk);

        std::atomic_int counter(0);
        auto T0 = [&](const uint64_t index)
        {
            pthread_setname_np(pthread_self(), ("T" + std::to_string(index)).c_str());
            for (int i = 0; i < 127;)
            {
                try {
                    auto lock = fs.lock(index, index + 1);
                    const char c = *lock.data();
                    std::memset(lock.data(), i ^ (index & 0xFF), lock.size());
                    *lock.data() = c + 1;
                    write(1, ".", 1);
                    i++;
                }
                catch (std::exception & e) {
                    elog(e.what(), "\n");
                    abort();
                }
            }

            ++counter;
            write(1, "x", 1);
        };

        std::vector<std::thread> threads;
        threads.emplace_back(T0, 1);
        threads.emplace_back(T0, 1);
        threads.emplace_back(T0, 2);
        threads.emplace_back(T0, 3);
        threads.emplace_back(T0, 4);
        threads.emplace_back(T0, 5);
        threads.emplace_back(T0, 6);

        pthread_setname_np(pthread_self(), "main");
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
