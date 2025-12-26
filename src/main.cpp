#include <iostream>
#include <algorithm>
#include <memory>
#include <sstream>
#include "colors.h"
#include "generalCFSbaseError.h"
#include "utils.h"
#include "args.h"
#include "routes.h"
#include "version.h"
#include "CowFileSystem.h"

namespace utils = cfs::utils;

utils::PreDefinedArgumentType::PreDefinedArgument MainArgument = {
    { .short_name = 'h', .long_name = "help",       .argument_required = false, .description = "Show help" },
    { .short_name = 'v', .long_name = "version",    .argument_required = false, .description = "Show version" },
    { .short_name = 'p', .long_name = "path",       .argument_required = true,  .description = "Path to CFS archive file" },
    { .short_name = 'c', .long_name = "",           .argument_required = true,  .description = "Execute a CFS command" },
    { .short_name = 'r', .long_name = "route",      .argument_required = true,  .description = "Specify a route" },
    { .short_name = 'F', .long_name = "flags",      .argument_required = true,  .description = "Specify route arguments" },
};

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

        if (route == "mkfs.cfs") {
            return mkfs_main(argc, argv);
        }

        /// No routes detected, utility mode
        bool is_terminal = true;
        struct stat st{};
        if (fstat(STDOUT_FILENO, &st) == -1) {
            is_terminal = false;
        } else {
            is_terminal = isatty(STDOUT_FILENO);
        }

        if (is_terminal) ilog("CFS utility version " CFS_IMPLEMENT_VERSION " (standard revision number " CFS_STANDARD_VERSION ")\n");
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

        if (parsed.contains("route"))
        {
            std::unique_ptr<char*[]> r_argv;
            std::vector<std::string> r_args;
            if (parsed.contains("flags")) {
                r_args = utils::splitString(parsed.at("flags"), ' ');
            }

            r_argv = std::make_unique<char*[]>(r_args.size() + 1);
            r_argv[0] = argv[0]; // redirect
            for (int i = 0; i < static_cast<int>(r_args.size()); ++i) {
                r_argv[i + 1] = const_cast<char *>(r_args[i].c_str());
            }

            const int d_argc = static_cast<int>(r_args.size()) + 1;
            char ** d_argv = r_argv.get();

            if (parsed.at("route") == "fsck.cfs") {
                return fsck_main(d_argc, d_argv);
            }

            if (parsed.at("route") == "mount.cfs") {
                return mount_main(d_argc, d_argv);
            }

            if (parsed.at("route") == "mkfs.cfs") {
                return mkfs_main(d_argc, d_argv);
            }
        }

        if (parsed.contains("path"))
        {
            cfs::CowFileSystem CowFileSystem(parsed["path"]);
            if (!parsed.contains('c')) {
                CowFileSystem.readline();
            } else {
                const std::string cmd = parsed['c'];
                std::stringstream ss(cmd);
                std::vector<std::string> args;
                while (!ss.eof()) {
                    std::string word;
                    ss >> word;
                    if (!word.empty())
                        args.push_back(word);
                    else
                        break;
                }
                CowFileSystem.command_main_entry_point(args);
            }
        } else {
            throw std::invalid_argument("Missing CFS file path");
        }

        return EXIT_SUCCESS;
    }
    catch (std::exception & e) {
        elog(e.what(), "\n");
        return EXIT_FAILURE;
    }
}
