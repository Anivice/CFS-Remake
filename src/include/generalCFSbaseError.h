#ifndef CFS_REMAKE_GENERAL_CFS_ERROR_H
#define CFS_REMAKE_GENERAL_CFS_ERROR_H

#include <stdexcept>

namespace cfs::error
{
    class generalCFSbaseError : std::exception
    {
    protected:
        std::string message;

    public:
        explicit generalCFSbaseError(const std::string& msg, bool include_backtrace_msg = false);
        ~generalCFSbaseError() override = default;
        [[nodiscard]] const char* what() const noexcept override { return message.c_str(); }
    };
}

#endif //CFS_REMAKE_GENERAL_CFS_ERROR_H