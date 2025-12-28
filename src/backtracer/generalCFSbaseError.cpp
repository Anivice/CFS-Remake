#include <atomic>
#include <vector>
#include <execinfo.h>
#include <sstream>
#include <regex>
#include <cxxabi.h>
#include <ranges>
#include <algorithm>
#include "utils.h"
#include "colors.h"
#include "generalCFSbaseError.h"
#include "execute.h"
#include <csignal>

#define MAX_STACK_FRAMES (64)

std::string demangle(const char* mangled)
{
    int status = 0;
    char* dem = abi::__cxa_demangle(mangled, nullptr, nullptr, &status);
    std::string result = (status == 0 && dem) ? dem : mangled;
    std::free(dem);
    return result;
}

typedef std::vector < std::pair<std::string, void*> > backtrace_info;
backtrace_info obtain_stack_frame()
{
    backtrace_info result;
    void* buffer[MAX_STACK_FRAMES] = {};
    const int frames = backtrace(buffer, MAX_STACK_FRAMES);

    char** symbols = backtrace_symbols(buffer, frames);
    if (symbols == nullptr) {
        return backtrace_info {};
    }

    for (int i = 1; i < frames; ++i) {
        result.emplace_back(symbols[i], buffer[i]);
    }

    free(symbols);
    return result;
}

std::atomic_bool g_trim_symbol = true;

bool trim_symbol_yes()
{
    auto trim = cfs::utils::getenv("SYMTRIM");
    std::ranges::transform(trim, trim.begin(), ::tolower);
    return trim.empty() ? g_trim_symbol.load() : (trim == "true" || trim == "yes" || trim == "y");
}

// fast backtrace
std::string backtrace_level_1()
{
    std::stringstream ss;
    const backtrace_info frames = obtain_stack_frame();
    int i = 0;

    auto trim_sym = [](std::string name)->std::string
    {
        if (trim_symbol_yes())
        {
            name = std::regex_replace(name, std::regex(R"(\(.*\))"), "");
            name = std::regex_replace(name, std::regex(R"(\[abi\:.*\])"), "");
            name = std::regex_replace(name, std::regex(R"(std\:\:.*\:\:)"), "");
            return name;
        }

        return name;
    };

    auto get_pair = [](const std::string & name)->std::pair<std::string, std::string>
    {
        const std::regex pattern(R"((.*)\((.*)(\+0x.*)\)\s\[.*\])");
        if (std::smatch matches; std::regex_search(name, matches, pattern)) {
            if (matches.size() == 4) {
                return std::make_pair(matches[1].str(),
                    matches[2].str().empty() ? matches[3].str() : demangle(matches[2].str().c_str()));
            }
        }

        return std::make_pair("", name);
    };

    for (const auto &symbol_name: frames | std::views::keys)
    {
        const auto [path, name] = get_pair(symbol_name);
        ss  << cfs::color::color(0,4,1) << "Frame " << cfs::color::color(5,2,1) << "#" << i++ << " "
            << std::hex << cfs::color::color(2,4,5) << path
            << ": " << cfs::color::color(1,5,5) << trim_sym(name) << cfs::color::no_color() << "\n";
    }

    return ss.str();
}

std::string addr2line()
{
    std::string addr2line = cfs::utils::getenv("ADDR2LINE");
    if (addr2line.empty()) {
        return "addr2line";
    }

    return addr2line;
}

// slow backtrace, with better trace info
std::string backtrace_level_2()
{
    std:: stringstream ss;
    const auto frames = obtain_stack_frame();
    int i = 0;
    const std::regex pattern(R"(([^\(]+)\(([^\)]*)\) \[([^\]]+)\])");
    std::smatch matches;

    struct traced_info
    {
        std::string name;
        std::string file;
    };

    auto generate_addr2line_trace_info = [](const std::string & executable_path, const std::string& address)->traced_info
    {
        auto [fd_stdout, fd_stderr, exit_status]
            = cfs::utils::exec_command("/bin/sh", "", "-c",
                addr2line() + " --demangle -f -p -a -e " + executable_path + " " + address);

        if (exit_status != 0)
        {
            return {};
        }

        std::string caller, path;
        if (const size_t pos = fd_stdout.find('/'); pos != std::string::npos) {
            caller = fd_stdout.substr(0, pos - 4);
            path = fd_stdout.substr(pos);
        }

        if (trim_symbol_yes())
        {
            if (const size_t pos2 = caller.find('('); pos2 != std::string::npos) {
                caller = caller.substr(0, pos2);
            }
        }

        if (!caller.empty() && !path.empty()) {
            return {.name = caller, .file = cfs::utils::replace_all(path, "\n", "") };
        }

        return {.name = cfs::utils::replace_all(fd_stdout, "\n", ""), .file = ""};
    };

    for (const auto & [symbol, frame] : frames)
    {
        if (std::regex_search(symbol, matches, pattern) && matches.size() > 3)
        {
            const std::string& exec_path_in_frame = matches[1].str();
            const std::string& executable_path = (exec_path_in_frame.find("cfs") == std::string::npos ?
                exec_path_in_frame : "/proc/" + std::to_string(getpid()) + "/exe");
            const std::string& traced_address = matches[2].str();
            const std::string& traced_runtime_address = matches[3].str();
            traced_info info;
            if (traced_address.empty()) {
                info = generate_addr2line_trace_info(executable_path, traced_runtime_address);
            } else {

                info = generate_addr2line_trace_info(executable_path, traced_address);
            }

            bool meaningful = true;
            std::string source_file = __FILE__;
            source_file = source_file.substr(0, source_file.find_last_of('.'));
            source_file = source_file.substr(source_file.find_last_of('/') + 1);
            if (!info.file.empty() && info.file.find(source_file) != std::string::npos) meaningful = false;

            if (meaningful)
            {
                ss  << cfs::color::color(0,4,1) << "Frame " << cfs::color::color(5,2,1) << "#" << i++ << " "
                    << std::hex << cfs::color::color(2,4,5) << frame
                    << ": " << cfs::color::color(1,5,5) << info.name << cfs::color::no_color() << "\n";
                if (!info.file.empty()) {
                    ss  << cfs::color::color(5,0,0) << "      => " << info.file << cfs::color::no_color() << "\n";
                }
            }
            else
            {
                ss  << cfs::color::color(3,3,3) << "Frame " << "#" << i++ << " "
                    << std::hex << frame << ": " << info.name << cfs::color::no_color() << "\n";
                if (!info.file.empty()) {
                    ss << cfs::color::color(3,3,3) << "         " << info.file << cfs::color::no_color() << "\n";
                }
            }
        } else {
            ss << "No trace information for " << std::hex << frame << "\n";
        }
    }

    return ss.str();
}

static std::mutex back_trace_mtx_;
constexpr int g_pre_defined_level = 2;
static std::string backtrace()
{
    std::lock_guard lock(back_trace_mtx_);
    std::signal(SIGPIPE, SIG_IGN);
    const auto level = cfs::utils::getenv("BACKTRACE_LEVEL");
    switch (level.empty() ? g_pre_defined_level
        : std::strtoll(level.c_str(), nullptr, 10))
    {
        case 1: return backtrace_level_1();
        case 2: return backtrace_level_2();
        default: return "";
    }
}

cfs::error::generalCFSbaseError::generalCFSbaseError(const std::string& msg, const bool include_backtrace_msg)
{
#ifndef __DEBUG__
    message = msg;
#else
    message = color::color(5,0,0) + "Error encountered: " + color::color(0,0,0,5,0,0) + msg + color::no_color();
    message += "\n ==> errno: " +
        (errno == 0 ? color::color(0,5,0) : color::color(5,0,0)) + strerror(errno) + color::no_color();
    if (include_backtrace_msg)
    {
        std::stringstream ss;
        ss << color::color(2,2,2) << "\n<=== BACKTRACE STARTS ===>\n" << color::no_color();
        ss << backtrace();
        ss << color::color(2,2,2) << "<=== BACKTRACE ENDS ===>" << color::no_color();
        message += ss.str();
    }
#endif
}
