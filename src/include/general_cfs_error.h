#ifndef CFS_REMAKE_GENERAL_CFS_ERROR_H
#define CFS_REMAKE_GENERAL_CFS_ERROR_H

#include <stdexcept>

namespace cfs::error
{
    class general_cfs_error : std::exception
    {
    protected:
        std::string message;

    public:
        explicit general_cfs_error(const std::string& msg, bool include_backtrace_msg = false);
        ~general_cfs_error() override = default;
        [[nodiscard]] const char* what() const noexcept override { return message.c_str(); }
    };
}

#endif //CFS_REMAKE_GENERAL_CFS_ERROR_H