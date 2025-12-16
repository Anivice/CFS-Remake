#ifndef CFS_UTILS_H
#define CFS_UTILS_H

#include <string>
#include <cstdlib>
#include <cstdint>
#include "generalCFSbaseError.h"

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
        /// @throws cfs::error::assertion_failed DIV/0
        uint64_t count_cell_with_cell_size(uint64_t cell_size, uint64_t particles);

        class CRC64 {
        public:
            CRC64() noexcept;

            /// Update CRC64 using new data
            /// @param data New data
            /// @param length New data length
            void update(const uint8_t* data, size_t length) noexcept;

            /// Get checksum
            [[nodiscard]] uint64_t get_checksum() const noexcept;

        private:
            uint64_t crc64_value{};
            uint64_t table[256] {};
            void init_crc64() noexcept;
        };

        /// Hash a set of data
        /// @param data Address of the data array
        /// @param length Data length
        /// @return CRC64 checksum of the provided data
        [[nodiscard]] inline
        uint64_t hashcrc64(const uint8_t * data, const size_t length) noexcept
        {
            CRC64 hash;
            hash.update(data, length);
            return hash.get_checksum();
        }

        template < typename Type >
        concept PODType = std::is_standard_layout_v<Type> && std::is_trivial_v<Type>;

        /// Hash a struct
        /// @tparam Type Implied data type, constraints being `std::is_standard_layout_v && std::is_trivial_v`
        /// @param data Struct data
        /// @return CRC64 checksum of the provided data
        template < PODType Type >
        [[nodiscard]] uint64_t hashcrc64(const Type & data) noexcept {
            return hashcrc64((uint8_t*)&data, sizeof(data));
        }
    }

    /// Return current UNIX timestamp
    /// @return Current UNIX timestamp
    uint64_t get_timestamp() noexcept;
}

#include "colors.h"
#include "execute.h"
#include "logger.h"

#endif //CFS_UTILS_H