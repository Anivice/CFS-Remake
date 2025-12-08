#ifndef CFS_UTILS_H
#define CFS_UTILS_H

#include <string>
#include <cstdlib>

/// Utilities
namespace cfs::utils
{
    /// Get environment variable (safe)
    /// @param name Name of the environment variable
    /// @return Return the environment variable, or empty string if unset
    std::string getenv(const std::string& name);

    /// Replace string inside a string
    /// @param original Original string
    /// @param target String to be replaced
    /// @param replacement Replacement string
    /// @return Replaced string. Original string will be modified as well
    std::string replace_all(
        std::string & original,
        const std::string & target,
        const std::string & replacement);
}

#endif //CFS_UTILS_H