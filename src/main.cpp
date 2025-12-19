#include <iostream>
#include <algorithm>
#include <memory>
#include <sstream>
#include "colors.h"
#include "generalCFSbaseError.h"
#include "utils.h"
#include "args.h"
#include "cfs.h"
#include "routes.h"
#include "readline.h"
#include "history.h"
#include "smart_block_t.h"
#include "version.h"
#include "cfs_command.h"
#include "commandTemplateTree.h"

namespace utils = cfs::utils;

utils::PreDefinedArgumentType::PreDefinedArgument MainArgument = {
    { .short_name = 'h', .long_name = "help",       .argument_required = false, .description = "Show help" },
    { .short_name = 'v', .long_name = "version",    .argument_required = false, .description = "Show version" },
};

std::vector <std::string> ls_under_pwd_of_cfs(const std::string & /* type is always cfs */)
{
    return {};
}

bool command_main_entry_point(const std::vector<std::string> & vec)
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

int main(int argc, char** argv)
{
    try
    {
        auto basename = [](const std::string & name)->std::string
        {
            if (const auto pos = name.find_last_of('/');
                pos != std::string::npos)
            {
                return name.substr(pos + 1);
            }
            return name;
        };

        const auto route = basename(argv[0]);

        if (route == "fsck.cfs") {
            return fsck_main(argc, argv);
        }

        if (route == "mount.cfs") {
            return mount_main(argc, argv);
        }

        if (route == "mkfs.route") {
            return mkfs_main(argc, argv);
        }

        /// No routes detected, utility mode
        ilog("CFS utility version " CFS_IMPLEMENT_VERSION " (standard revision number " CFS_STANDARD_VERSION ")\n");
        const utils::PreDefinedArgumentType PreDefinedArguments(MainArgument);
        utils::ArgumentParser ArgumentParser(argc, argv, PreDefinedArguments);
        const auto parsed = ArgumentParser.parse();
        if (parsed.contains("help")) {
            std::cout << *argv << " [Arguments [OPTIONS...]...]" << std::endl;
            std::cout << PreDefinedArguments.print_help();
            return EXIT_SUCCESS;
        }

        if (parsed.contains("version")) {
            std::cout << *argv << std::endl;
            std::cout.write(reinterpret_cast<const char *>(version_text), version_text_len);
            std::cout << std::endl;
            return EXIT_SUCCESS;
        }

        cmdTpTree::read_command(command_main_entry_point, ls_under_pwd_of_cfs, "cfs> ");
        return EXIT_SUCCESS;
    }
    catch (std::exception & e) {
        elog(e.what(), "\n");
        return EXIT_FAILURE;
    }
}
