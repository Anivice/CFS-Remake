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
#include "mmap.h"

make_simple_error_class(bitmap_base_init_data_array_returns_false);
make_simple_error_class(cannot_even_read_cfs_header_in_that_small_tiny_file)
make_simple_error_class(not_even_a_cfs_filesystem)
make_simple_error_class(filesystem_head_corrupt_and_unable_to_recover)
make_simple_error_class(invalid_argument)
make_simple_error_class(cannot_discard_blocks)

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

    /// block state lock
    class block_shared_lock_t : public bitmap_base
    {
    private:
        std::vector <uint8_t> data;

    public:
        block_shared_lock_t() = default;
        ~block_shared_lock_t() override = default;

        /// Create a state lock
        /// @param file mmap file
        /// @param block_size Block size
        void create(const basic_io::mmap * file, uint64_t block_size);
    };

    constexpr uint64_t cfs_magick_number = 0xCFADBEEF20251216;
    constexpr uint64_t cfs_header_size = 512;

    struct cfs_head_t
    {
        uint64_t magick; // fs magic
        struct static_info_t {
            char label [64];
            uint64_t block_size;
            uint64_t blocks; // block numbers
            uint64_t data_bitmap_start;
            uint64_t data_bitmap_end;
            uint64_t data_bitmap_backup_start;
            uint64_t data_bitmap_backup_end;
            uint64_t data_block_attribute_table_start; // attribute is 16 byte for each data block
            uint64_t data_block_attribute_table_end;
            uint64_t data_table_start;
            uint64_t data_table_end;
            uint64_t journal_start;
            uint64_t journal_end;
        };

        static_info_t static_info; // static info
        static_info_t static_info_dup; // static info, dup
        uint64_t static_info_checksum; // static info checksum
        uint64_t static_info_checksum_dup; // static info checksum, dup

        struct runtime_info_t {
            uint64_t mount_timestamp;       // when was the last time it's mounted
            uint64_t last_check_timestamp;  // last time check ran
            uint64_t snapshot_number;       // max 127
            uint64_t snapshot_number_dup;
            uint64_t snapshot_number_dup2;
            uint64_t snapshot_number_dup3;
            struct {
                uint64_t clean:1;
            } flags;
            uint64_t last_allocated_block;
            uint64_t allocated_blocks;
        };
        runtime_info_t runtime_info; // runtime info
        runtime_info_t runtime_info_cow; // cow of the last change
        uint64_t runtime_info_checksum; // runtime info
        uint64_t runtime_info_checksum_cow; // crc64 of cow of the last change

        struct {
            uint64_t _1;
        } _reserved_;
    };
    static_assert(sizeof(cfs_head_t) == cfs_header_size, "Faulty header size");

    class block_t {
    protected:
        block_shared_lock_t * bitlocker_; // block bit locker
        char * data_; // mapped file address
        const uint64_t addressable_space_; // block size addressable for this block_t
        const uint64_t start_address_; // start address in linear
        const uint64_t end_address_; // end address in linear
        const uint64_t block_size_; // block size
        const uint64_t block_count_; // blocks in this block_t

        /// Create a block
        /// @param data Starting address of the block
        /// @param bitlocker Block bit locker
        /// @param addressable_space block size addressable for this block_t
        /// @param start_address Block starting address
        /// @param end_address Block ending address
        /// @param block_size Individual block size
        /// @param block_count Total blocks in this block_t
        block_t(char * data,
            block_shared_lock_t * bitlocker,
            uint64_t addressable_space,
            uint64_t start_address,
            uint64_t end_address,
            uint64_t block_size,
            uint64_t block_count);

    public:
        friend class smart_block_t;
    };

    constexpr uint64_t cfs_minimum_size = 1024 * 1024 * 1;

    /// Format a CFS
    /// @param path_to_block_file Disk path
    /// @param block_size block size
    /// @param label disk label
    /// @return None
    void make_cfs(const std::string &path_to_block_file, const uint64_t block_size, const std::string & label);

    class filesystem
    {
    private:
        basic_io::mmap file_;
        block_shared_lock_t bitlocker_;
        cfs_head_t::static_info_t static_info_;

    public:
        /// check headers, fix if possible, and create a bit state locker for all blocks
        explicit filesystem(const std::string & path_to_block_file);
    };
}

#endif //CFS_SMART_BLOCK_T_H