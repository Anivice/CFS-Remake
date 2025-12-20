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

        if (parsed.contains("path")) {
            cfs::CowFileSystem CowFileSystem(parsed["path"]);
            CowFileSystem.readline();
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
