#include <iostream>
#include "colors.h"
#include "generalCFSbaseError.h"
#include "utils.h"
#include "args.h"
#include "cfs.h"

namespace utils = cfs::utils;

utils::PreDefinedArgumentType::PreDefinedArgument MainArgument = {
    { .short_name = 'h', .long_name = "help", .argument_required = false, .description = "Show help" },
    { .short_name = 'v', .long_name = "version", .argument_required = false, .description = "Show version" },
};

int main(int argc, char** argv)
{
    try
    {
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
    }
    catch (cfs::error::generalCFSbaseError & e) {
        elog(e.what(), "\n");
    }
    catch (std::exception & e) {
        elog(e.what(), "\n");
    }

    return 0;
}
