#include "logger.h"
#include "utils.h"

cfs::log::Logger cfs::log::cfs_logger;

cfs::log::Logger::Logger() noexcept
{
    const auto log_file = utils::getenv("LOG");
    if (log_file == "stdout") {
        output = &std::cout;
    } else if (log_file == "stderr") {
        output = &std::cerr;
    } else if (!log_file.empty()) {
        ofile.open(log_file, std::ios::out | std::ios::app);
        if (!ofile.is_open())
        {
            std::cerr << "Unable to open log file " << log_file << ": " << strerror(errno) << std::endl;
            abort();
        }

        output = &ofile;
    } else {
        output = &std::cerr;
    }

    log_level = 0;
#if (DEBUG)
    filter_level = 0;
#else
    filter_level = 2;
#endif

    endl_found_in_last_log = true;
}

std::string cfs::log::strip_func_name(std::string name)
{
    if (const auto p = name.find('('); p != std::string::npos) {
        name.erase(p);
    }

    if (const auto p = name.rfind(' '); p != std::string::npos) {
        name.erase(0, p + 1);
    }

    return name;
}
