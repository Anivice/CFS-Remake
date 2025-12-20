#include "CowFileSystem.h"
#include "args.h"
#include "cfs.h"
#include "routes.h"
#include "readline.h"
#include "history.h"
#include "smart_block_t.h"
#include "version.h"
#include "cfs_command.h"
#include "generalCFSbaseError.h"
#include <iostream>
#include <algorithm>
#include <memory>
#include <sstream>
#include "colors.h"

namespace cfs {
    std::vector<std::string> CowFileSystem::ls_under_pwd_of_cfs(const std::string &)
    {
        return {};
    }

    bool CowFileSystem::command_main_entry_point(const std::vector<std::string> &vec)
    {
        if (vec.empty()) return true;

        if (vec.front() == "quit" || vec.front() == "exit") {
            return false;
        }

        if (vec.front() == "help") {
            std::cout << cmdTpTree::command_template_tree.get_help();
        }
        else if (vec.front() == "help_at") {
            try {
                const std::vector help_path(vec.begin() + 1, vec.end());
                std::ranges::for_each(help_path, [](const auto & v) { std::cout << v << " "; });
                std::cout << ": " << cmdTpTree::command_template_tree.get_help(help_path) << std::endl;
            } catch (std::exception & e) {
                elog(e.what(), "\n");
            }
        }
        else if (vec.front() == "version") {
            std::cout.write(reinterpret_cast<const char *>(version_text), version_text_len);
            std::cout << std::endl;
        }
        else if (vec.front() == "format") {
            if (vec.size() < 2) {
                std::cerr << "format [DISK] <BLOCK SIZE> <LABEL>" << std::endl;
                return true;
            }

            const std::string & disk = vec[1];
            std::string block_size_str = "4096", label;

            if (vec.size() >= 3) {
                block_size_str = vec[2];
            }

            if (vec.size() == 4) {
                label = vec[3];
            }

            try {
                const auto block_size = std::strtoull(block_size_str.c_str(), nullptr, 10);
                cfs::make_cfs(disk, block_size, label);
            } catch (std::exception & e) {
                elog(e.what(), "\n");
            }
        }

        return true;
    }
} // cfs