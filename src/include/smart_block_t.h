#ifndef CFS_SMART_BLOCK_T_H
#define CFS_SMART_BLOCK_T_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>
#include <functional>
#include <memory>
#include <thread>
#include "generalCFSbaseError.h"

make_simple_error_class(bitmap_base_init_data_array_returns_false);

namespace cfs
{
    /// bitmap base class
    class bitmap_base
    {
    protected:
        uint8_t * data_array_ = nullptr;
        std::mutex array_mtx_;
        std::atomic<uint64_t> particles_;
        const uint64_t bytes_required_ = 0;

        /// Initialize `data_array_`
        /// @param bytes Bytes required
        /// @return If success, return true, else, false. bitmap_base() will throw an error if false.
        std::function<bool(uint64_t)> init_data_array = [](const uint64_t bytes) -> bool { return false; };

        /// Create a global bitmap mutex lock state map
        /// @param required_blocks Mutexes required
        void init(uint64_t required_blocks);

    public:
        bitmap_base() = default;
        virtual ~bitmap_base() = default;
        bitmap_base(const bitmap_base &) = delete;
        bitmap_base & operator=(const bitmap_base &) = delete;
        bitmap_base(bitmap_base &&) = delete;
        bitmap_base & operator=(bitmap_base &&) = delete;

        /// Get bit at the specific location
        /// @param index Bit Index
        /// @return The bit at the specific location
        bool get_bit(uint64_t index);

        /// Set the bit at the specific location
        /// @param index Bit Index
        /// @param new_bit The new bit value
        /// @return NONE
        void set_bit(uint64_t index, bool new_bit);
    };

    class smart_block_t
    {
    };
}

#endif //CFS_SMART_BLOCK_T_H