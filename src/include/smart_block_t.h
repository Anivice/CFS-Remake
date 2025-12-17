#ifndef CFS_SMART_BLOCK_T_H
#define CFS_SMART_BLOCK_T_H

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>
#include <functional>
#include <memory>
#include <thread>
#include <map>
#include <condition_variable>
#include "generalCFSbaseError.h"
#include "mmap.h"
#include "utils.h"

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
        /// @throws cfs::error::assertion_failed Init failed
        void init(uint64_t required_blocks);

    public:
        bitmap_base() noexcept = default;
        virtual ~bitmap_base() noexcept = default;

        bitmap_base(const bitmap_base &) = delete;
        bitmap_base & operator=(const bitmap_base &) = delete;
        bitmap_base(bitmap_base &&) = delete;
        bitmap_base & operator=(bitmap_base &&) = delete;

        /// Get bit at the specific location
        /// @param index Bit Index
        /// @return The bit at the specific location
        /// @throws cfs::error::assertion_failed Out of bounds
        bool get_bit(uint64_t index);

        /// Set the bit at the specific location
        /// @param index Bit Index
        /// @param new_bit The new bit value
        /// @return NONE
        /// /// @throws cfs::error::assertion_failed Out of bounds
        void set_bit(uint64_t index, bool new_bit);
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
    constexpr uint64_t cfs_minimum_size = 1024 * 1024 * 1;

    /// Format a CFS
    /// @param path_to_block_file Disk path
    /// @param block_size block size
    /// @param label disk label
    /// @return None
    /// @throws cfs::error::assertion_failed Can't do basic C operations
    /// @throws cfs::error::cannot_discard_blocks Cannot discard blocks on block devices
    void make_cfs(const std::string &path_to_block_file, const uint64_t block_size, const std::string & label);

    class filesystem
    {
    public:
        class guard_continuous;
        class block_shared_lock_t {
        private:
            const uint64_t blocks_ = 0;
            class bitmap_t : public cfs::bitmap_base {
            private:
                std::vector <uint8_t> data;

            public:
                /// init bitmap
                /// @param size bitmap size
                /// @throws cfs::error::assertion_failed (thrown by init())
                void create(uint64_t size);
            } bitmap;

            /// init bitmap
            /// @throws cfs::error::assertion_failed (thrown by bitmap::init())
            void init() { bitmap.create(blocks_); }
            std::mutex bitmap_mtx_;
            std::condition_variable cv;

        public:
            /// lock by index
            /// @param index Block ID index
            void lock(uint64_t index);

            /// unlock by index
            /// @param index Block ID index
            void unlock(uint64_t index);

            friend class filesystem;
            friend class guard_continuous;
        };

    private:
        basic_io::mmap file_;
        block_shared_lock_t bitlocker_;
        const cfs_head_t::static_info_t static_info_;

    protected:
        class cfs_header_block_t {
        private:
            filesystem * parent_ = nullptr;
            const uint64_t tailing_header_blk_id_ = 0;
            cfs_head_t * fs_head;
            cfs_head_t * fs_end;
            std::mutex mtx_;

        public:
            /// get runtime info from header blocks (both tailing and leading)
            /// @return Runtime info
            cfs_head_t::runtime_info_t load();

            /// set runtime info to header blocks (both tailing and leading)
            /// @param info New runtime info
            void set(const cfs_head_t::runtime_info_t &info);

        private:
            cfs_header_block_t() noexcept = default;
            ~cfs_header_block_t() noexcept = default;

        public:
            friend class filesystem;
        } cfs_header_block;

    public:
        /// check headers, fix if possible, and create a bit state locker for all blocks
        /// @param path_to_block_file Path to block file
        /// @throws cfs::error::cannot_even_read_cfs_header_in_that_small_tiny_file Too small
        /// @throws cfs::error::not_even_a_cfs_filesystem Not CFS
        /// @throws cfs::error::filesystem_head_corrupt_and_unable_to_recover FS corrupt
        explicit filesystem(const std::string & path_to_block_file);

        /// lock guard
        class guard {
        private:
            block_shared_lock_t * bitlocker_;
            char * data_;
            const uint64_t block_address_;
            const uint64_t block_size_;

            /// make lock guard
            /// @param bitlocker global lock
            /// @param data block data
            /// @param block_address block ID
            /// @param block_size Block size
            /// @throws cfs::error::assertion_failed out of bounds
            guard(block_shared_lock_t * bitlocker, char * data, uint64_t block_address, uint64_t block_size);

        public:
            /// @throws cfs::error::assertion_failed out of bounds
            ~guard() noexcept { bitlocker_->unlock(block_address_); }

            /// get the address of the currently locked block page
            /// @return data pointer
            char * data() const noexcept { return data_; }

            /// return accessible size
            /// @return accessible size
            [[nodiscard]] uint64_t size() const noexcept { return block_size_; }

            guard& operator=(const guard&) = delete;
            guard(const guard&) = delete;
            guard& operator=(guard&&) = delete;
            guard(guard&&) = delete;
            friend class filesystem;
        };

        /// lock guard
        class guard_continuous {
        private:
            block_shared_lock_t * bitlocker_;
            char * data_;
            const uint64_t start_;
            const uint64_t end_;
            const uint64_t block_size_;

            /// make lock guard (continuous)
            /// @param bitlocker global lock
            /// @param data block data
            /// @param start Region block ID to lock (start)
            /// @param end Region block ID to lock (end)
            /// @param block_size Block size
            /// @throws cfs::error::assertion_failed out of bounds
            guard_continuous(block_shared_lock_t * bitlocker, char * data, uint64_t start, uint64_t end, uint64_t block_size);

        public:
            /// @throws cfs::error::assertion_failed out of bounds
            ~guard_continuous();

            /// get the address of the currently locked block page
            /// @return data pointer
            char * data() const noexcept { return data_; }

            /// return accessible size
            /// @return accessible size
            [[nodiscard]] uint64_t size() const noexcept { return (end_ - start_ + 1) * block_size_; }

            guard_continuous& operator=(const guard_continuous&) = delete;
            guard_continuous(const guard_continuous&) = delete;
            guard_continuous& operator=(guard_continuous&&) = delete;
            guard_continuous(guard_continuous&&) = delete;
            friend class filesystem;
        };

        /// Lock a certain block
        /// @param index Block ID to lock
        /// @return lock_guard
        /// @throws cfs::error::assertion_failed Invalid arguments
        guard lock(uint64_t index);

        /// Lock a region of blocks
        /// @param start Region block ID to lock (start)
        /// @param end Region block ID to lock (end)
        /// @return lock_guard
        /// @throws cfs::error::assertion_failed Invalid arguments
        guard_continuous lock(uint64_t start, uint64_t end);

        /// flush all data, write clean flag, close file
        ~filesystem() noexcept;
    };
}

#endif //CFS_SMART_BLOCK_T_H