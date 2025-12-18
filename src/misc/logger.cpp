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
            std::cout << "Unable to open log file " << log_file << ": " << strerror(errno) << std::endl;
            abort();
        }

        output = &ofile;
        color::g_color_disabled_since_output_is_not_terminal = true;
    } else {
        output = &std::cout;
    }

    log_level = 0;
#if (DEBUG)
    filter_level = 0;
#else
    filter_level = 1;
#endif

    const auto env_log_level = utils::getenv("LOG_LEVEL");
    filter_level = std::strtol(env_log_level.c_str(), nullptr, 10);
    if (filter_level > 3) filter_level = 3;

    endl_found_in_last_log = true;
}

cfs::log::Logger::~Logger() noexcept
{
    if (!endl_found_in_last_log) {
        constexpr unsigned char enter_emoji[] = { 0xe2, 0x8f, 0x8e }; // "⏎"
        output->write(reinterpret_cast<const char *>(enter_emoji), sizeof(enter_emoji));
        *output << std::endl;
    }
}

std::string cfs::log::strip_func_name(std::string name) noexcept
{
    if (const auto p = name.find('('); p != std::string::npos) {
        name.erase(p);
    }

    if (const auto p = name.rfind(' '); p != std::string::npos) {
        name.erase(0, p + 1);
    }

    return name;
}
