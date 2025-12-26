#ifndef CFS_UTILS_H
#define CFS_UTILS_H

#include <string>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include "lz4.h"
#include "cppcrc.h"
#include <ctime>
#include <sstream>

/// Utilities
namespace cfs::utils
{
    /// Get environment variable (safe)
    /// @param name Name of the environment variable
    /// @return Return the environment variable, or empty string if unset
    std::string getenv(const std::string& name) noexcept;

    std::vector<std::string> splitString(const std::string& s, char delim = ' ');

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
    std::pair < const int, const int > get_screen_row_col() noexcept;

    namespace arithmetic
    {
        /// Get cells required to store all particles
        /// @param cell_size Cell Size, i.e., particles inside each cell
        /// @param particles Overall particles
        /// @return Cells required to store all particles
        /// @throws cfs::error::assertion_failed DIV/0
        uint64_t count_cell_with_cell_size(uint64_t cell_size, uint64_t particles);

        /// Hash a set of data
        /// @param data Address of the data array
        /// @param length Data length
        /// @return 64 bit checksum of the provided data
        [[nodiscard]] uint64_t hash64(const uint8_t * data, size_t length) noexcept;

        /// Hash a set of data
        /// @param data Address of the data array
        /// @param length Data length
        /// @return 5 bit checksum of the provided data
        [[nodiscard]] uint8_t hash5(const uint8_t * data, size_t length) noexcept;

        template < typename Type >
        concept PODType = std::is_standard_layout_v<Type> && std::is_trivial_v<Type>;

        /// Hash a struct
        /// @tparam Type Implied data type, constraints being `std::is_standard_layout_v && std::is_trivial_v`
        /// @param data Struct data
        /// @return CRC64 checksum of the provided data
        template < PODType Type >
        [[nodiscard]] uint64_t hash64(const Type & data) noexcept {
            return hash64((uint8_t*)&data, sizeof(data));
        }

        std::vector < uint8_t > compress(const std::vector < uint8_t > & data) noexcept;
        std::vector < uint8_t > decompress(const std::vector < uint8_t > & data) noexcept;
    }

    /// Return current UNIX timestamp
    /// @return Current UNIX timestamp
    uint64_t get_timestamp() noexcept;

    /// Return current timespec
    /// @return Current timespec
    timespec get_timespec() noexcept;

    void print_table(
        const std::vector<std::pair < std::string, int > > & titles,
        const std::vector<std::vector < std::string > > & vales,
        const std::string & label);

    enum JustificationType : int { Center = 1, Left = 2, Right = 3 };

    template < typename Arg > void _print(const Arg & arg, std::vector<std::string> & ret) {
        std::stringstream ss;
        ss << arg;
        ret.push_back(ss.str());
    }

    template < typename ...Args >
    void print(std::vector<std::string> & ret, const Args & ...arg) {
        (_print(arg, ret), ...);
    }

    std::string value_to_human(unsigned long long value,
        const std::string & lv1, const std::string & lv2,
        const std::string & lv3, const std::string & lv4);

    inline std::string value_to_speed(const unsigned long long value) {
        return value_to_human(value, "B/s", "KB/s", "MB/s", "GB/s");
    }

    inline std::string value_to_size(const unsigned long long value) {
        return value_to_human(value, "B", "KB", "MB", "GB");
    }

}

#define NO_COPY_OBJ(name)                                       \
    name(const name &) = delete;                                \
    name & operator=(const name &) = delete;                    \
    name(name &&) = delete;                                     \
    name & operator=(name &&) = delete;

#include "colors.h"
#include "logger.h"

#endif //CFS_UTILS_H