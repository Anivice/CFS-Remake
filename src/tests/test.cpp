#include <atomic>
#include <thread>
#include <filesystem>
#include <regex>
#include "utils.h"
extern void show();

/*
* 16-12-2025 01:07:41.642724307 (main) [INFO]: [
  /home/anivice/CLionProjects/CFS-Remake/cmake-build-debug/bitmap.test,
  /home/anivice/CLionProjects/CFS-Remake/cmake-build-debug/false.test,
]
 */

int main(int argc, char ** argv)
{
    namespace fs = std::filesystem;
    namespace time = std::chrono;

    std::vector < std::string > test_selective_list;
    for (int i = 1; i < argc; i++) {
        test_selective_list.emplace_back(argv[i]);
    }

    bool all_passed = true;
    std::map < std::string, std::string > test_map;
    const std::regex test_name_pattern(R"([\w|\d]+\.test)");
    for (const std::string cmake_binary_dir = CMAKE_BINARY_DIR;
        const auto & entry : fs::directory_iterator(cmake_binary_dir))
    {
        const std::string path = entry.path().string();
        if (std::smatch matches; std::regex_search(path, matches, test_name_pattern)) {
            const auto & test_name = matches[0].str();
            test_map.emplace(test_name, path);
        }
    }

    std::ranges::for_each(test_selective_list, [&](const std::string & test_name)
    {
        if (!test_map.contains(test_name)) {
            wlog(test_name, " not found in list\n");
            all_passed = false;
        }
    });

    ilog(test_map, "\n\n");
    uint64_t test_index = 1;
    const uint64_t test_number = test_selective_list.empty() ? test_map.size() : std::min(test_map.size(), test_selective_list.size());
    std::ranges::for_each(test_map, [&](const std::pair < std::string, std::string > & test)
    {
        if (!test_selective_list.empty()) {
            if (std::ranges::find(test_selective_list, test.first) == test_selective_list.end()) {
                return; // skip if not selected in list
            }
        }
        const auto test_start_time = time::high_resolution_clock::now();
        std::atomic_bool test_in_progress = true;
        cfs::utils::cmd_status test_status;
        std::thread T0([&] {
            test_status = cfs::utils::exec_command(test.second);
            test_in_progress = false;
        });

        std::string time_string;
        while (test_in_progress)
        {
            const auto now = time::high_resolution_clock::now();
            const auto seconds_elapsed = time::duration_cast<time::seconds>(now - test_start_time).count();
            if (seconds_elapsed > 3) {
                time_string = ": " + cfs::color::color(0,0,0,5,5,5) + std::to_string(seconds_elapsed) + "s" + cfs::color::no_color();
            }
            std::cout << "\033[F\033[K";
            ilog("[", test_index, "/", test_number, "]: Running test: ",
                 cfs::color::color(5,5,5,0,0,5), test.first, cfs::color::no_color(),
                 time_string, "\n");
            for (int i = 0; i < 500; i++) {
                std::this_thread::sleep_for(time::milliseconds(2l));
                if (!test_in_progress) break;
            }
        }

        if (T0.joinable()) T0.join();
        if (test_status.exit_status != 0) {
            std::cout << "\033[F\033[K";
            ilog("[", test_index, "/", test_number, "]: ",
                 cfs::color::color(5,5,5,0,0,5), test.first, cfs::color::no_color(), time_string,
                 ":  ", cfs::color::color(0,0,0,5,0,0), "FAILED (", test_status.exit_status, ")",
                 cfs::color::no_color(), "\n");
            all_passed = false;
        }
        std::cout << std::endl;
        test_index++;
    });

    if (all_passed)
    {
        show();
        return EXIT_SUCCESS;
    }

    return EXIT_FAILURE;
}
