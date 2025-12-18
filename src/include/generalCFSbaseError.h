#ifndef CFS_REMAKE_GENERAL_CFS_ERROR_H
#define CFS_REMAKE_GENERAL_CFS_ERROR_H

#include <stdexcept>
#include <string>
#include <sstream>
#include "utils.h"

namespace cfs::error
{
    class generalCFSbaseError : public std::exception
    {
    protected:
        std::string message;

    public:
        explicit generalCFSbaseError(const std::string& msg, bool include_backtrace_msg = false);
        ~generalCFSbaseError() override = default;
        [[nodiscard]] const char* what() const noexcept override { return message.c_str(); }
    };
}

#define make_simple_error_class(name)                                                           \
namespace cfs::error                                                                            \
{                                                                                               \
    class name final : public generalCFSbaseError {                                             \
    private:                                                                                    \
        template < typename Type >                                                              \
        void msg_conj(const Type & msg) { oss << msg; }                                         \
                                                                                                \
        std::ostringstream oss;                                                                 \
                                                                                                \
    public:                                                                                     \
        template <typename... Args>                                                             \
        explicit name(const Args& ...args) : generalCFSbaseError("<INSERT ERROR MESSAGE HERE>") \
        {                                                                                       \
            msg_conj(#name ": ");                                                               \
            (msg_conj(args), ...);                                                              \
            cfs::utils::replace_all(message, "<INSERT ERROR MESSAGE HERE>", oss.str());         \
        }                                                                                       \
        name() : generalCFSbaseError(#name) { }                                                 \
        ~name() override = default;                                                             \
    };                                                                                          \
}

#define make_simple_error_class_traceable(name)                                                         \
namespace cfs::error                                                                                    \
{                                                                                                       \
    class name final : public generalCFSbaseError {                                                     \
    private:                                                                                            \
        template < typename Type >                                                                      \
        void msg_conj(const Type & msg) { oss << msg; }                                                 \
                                                                                                        \
        std::ostringstream oss;                                                                         \
                                                                                                        \
    public:                                                                                             \
        template <typename... Args>                                                                     \
        explicit name(const Args& ...args) : generalCFSbaseError("<INSERT ERROR MESSAGE HERE>", true)   \
        {                                                                                               \
            msg_conj(#name ": ");                                                                       \
            (msg_conj(args), ...);                                                                      \
            cfs::utils::replace_all(message, "<INSERT ERROR MESSAGE HERE>", oss.str());                 \
        }                                                                                               \
        name() : generalCFSbaseError(#name, true) { }                                                   \
        ~name() override = default;                                                                     \
    };                                                                                                  \
}

make_simple_error_class_traceable(assertion_failed);
#define assert_throw(condition, msg) if (!(condition)) { throw cfs::error::assertion_failed("Assertion `" #condition "` failed: " msg); }
#define cfs_assert_simple(condition) assert_throw(condition, "")

#endif //CFS_REMAKE_GENERAL_CFS_ERROR_H