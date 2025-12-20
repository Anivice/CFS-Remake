#include "routes.h"
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

utils::PreDefinedArgumentType::PreDefinedArgument mkfsMainArgument = {
    { .short_name = 'h', .long_name = "help",       .argument_required = false, .description = "Show help" },
    { .short_name = 'v', .long_name = "version",    .argument_required = false, .description = "Show version" },
    { .short_name = 'p', .long_name = "path",       .argument_required = true,  .description = "Path to CFS archive file" },
    { .short_name = 'L', .long_name = "label",      .argument_required = true,  .description = "CFS label" },
    { .short_name = 'b', .long_name = "block",      .argument_required = true,  .description = "Block size" },
};

int mkfs_main(int argc, char** argv)
{
    try
    {
        const utils::PreDefinedArgumentType PreDefinedArguments(mkfsMainArgument);
        utils::ArgumentParser ArgumentParser(argc, argv, PreDefinedArguments);
        const auto parsed = ArgumentParser.parse();
        if (parsed.contains("help")) {
            std::cout << *argv << " <MKFS> [Arguments [OPTIONS...]...]" << std::endl;
            std::cout << PreDefinedArguments.print_help();
            return EXIT_SUCCESS;
        }

        if (parsed.contains("version")) {
            std::cout << *argv << " [MKFS]" << std::endl;
            std::cout.write(reinterpret_cast<const char *>(version_text), version_text_len);
            std::cout << std::endl;
            return EXIT_SUCCESS;
        }

        if (parsed.contains("path")) {
            const auto & path = parsed["path"];
            uint64_t block_size = 4096;
            std::string label;

            if (parsed.contains("label")) {
                label = parsed["label"];
            }

            if (parsed.contains("block")) {
                block_size = std::strtoul(parsed["block"].c_str(), nullptr, 10);
            }

            cfs::make_cfs(path, block_size, label);
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
