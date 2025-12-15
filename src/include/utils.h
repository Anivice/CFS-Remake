#ifndef CFS_UTILS_H
#define CFS_UTILS_H

#include <string>
#include <cstdlib>
#include <cstdint>

/// Utilities
namespace cfs::utils
{
    /// Get environment variable (safe)
    /// @param name Name of the environment variable
    /// @return Return the environment variable, or empty string if unset
    std::string getenv(const std::string& name) noexcept;

    /// Replace string inside a string
    /// @param original Original string
    /// @param target String to be replaced
    /// @param replacement Replacement string
    /// @return Replaced string. Original string will be modified as well
    std::string replace_all(
        std::string & original,
        const std::string & target,
        const std::string & replacement) noexcept;

    /// Get Row and Column size from terminal
    /// @return Pair in [Col (x), Row (y)], or 80x25 if all possible attempt failed
    std::pair < const int, const int > get_screen_col_row() noexcept;

    namespace arithmetic
    {
        /// Get cells required to store all particles
        /// @param cell_size Cell Size, i.e., particles inside each cell
        /// @param particles Overall particles
        /// @return Cells required to store all particles
        uint64_t count_cell_with_cell_size(uint64_t cell_size, uint64_t particles) noexcept;
    }
}

#include "colors.h"
#include "execute.h"
#include "logger.h"

#endif //CFS_UTILS_H