#include "routes.h"
#include "utils.h"
#include "args.h"
#include "version.h"

using namespace cfs;
utils::PreDefinedArgumentType::PreDefinedArgument fsckMainArgument = {
    { .short_name = 'h',    .long_name = "help",        .argument_required = false,     .description = "Show help" },
    { .short_name = 'v',    .long_name = "version",     .argument_required = false,     .description = "Show version" },
    { .short_name = 'p',    .long_name = "path",        .argument_required = true,      .description = "Path to CFS archive file" },
    { .short_name = -1,     .long_name = "modify",      .argument_required = true,      .description = "Apply changes to the file system" },
};

int fsck_main(int argc, char** argv)
{
    try
    {
        const utils::PreDefinedArgumentType PreDefinedArguments(fsckMainArgument);
        utils::ArgumentParser ArgumentParser(argc, argv, PreDefinedArguments);
        const auto parsed = ArgumentParser.parse();
        if (parsed.contains("help")) {
            std::cout << *argv << " <FSCK> [Arguments [OPTIONS...]...]" << std::endl;
            std::cout << PreDefinedArguments.print_help();
            return EXIT_SUCCESS;
        }

        if (parsed.contains("version")) {
            std::cout << *argv << " [FSCK]" << std::endl;
            std::cout.write(reinterpret_cast<const char *>(version_text), version_text_len);
            std::cout << std::endl;
            return EXIT_SUCCESS;
        }

        if (parsed.contains("path")) {
            const auto & path = parsed["path"];
            ilog("CFS target is ", path, "\n");
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
