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
#include "generalCFSbaseError.h"
#include "mmap.h"
#include "utils.h"

make_simple_error_class(bitmap_base_init_data_array_returns_false);
make_simple_error_class(cannot_even_read_cfs_header_in_that_small_tiny_file)
make_simple_error_class(not_even_a_cfs_filesystem)
make_simple_error_class(filesystem_head_corrupt_and_unable_to_recover)
make_simple_error_class(invalid_argument)
make_simple_error_class(cannot_discard_blocks)
make_simple_error_class(no_locks_available);

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

    class block_shared_lock_t {
    public:
        static constexpr size_t lock_numbers = 4; // 1024 * 16;
        static constexpr uint64_t max_map_size = 1024ull * 512ull;

    private:
        std::array < std::mutex, lock_numbers > lock_array_;
        std::unordered_map < uint64_t /* block ID */, bool /* is allocated ? */ > lock_array_allocate_map_;
        std::map < uint64_t /* block ID */, uint64_t /* lock ID */ > block_id_to_lock_id_map_;
        uint64_t last_allocated_position = 0;
        std::mutex g_lock_array_alloc_dealloc_mtx_;

    public:
        block_shared_lock_t() {
            lock_array_allocate_map_.reserve(lock_numbers);
        }

    private:

        void deallocate_all_unlocked_unblocked()
        {
            for (auto && [ lock_id, alloc_status ] : lock_array_allocate_map_)
            {
                if (lock_array_[lock_id].try_lock())
                {
                    lock_array_[lock_id].unlock();
                    auto ptr = std::ranges::find_if(block_id_to_lock_id_map_,
                        [&](const std::pair < uint64_t /* block ID */, uint64_t /* lock ID */ > & pair)->bool {
                        return pair.second == lock_id;
                    });

                    if (ptr != block_id_to_lock_id_map_.end()) {
                        block_id_to_lock_id_map_.erase(ptr);
                    }

                    alloc_status = false;
                }
            }
        }

        uint64_t allocate_lock_array_unblocked(const uint64_t index)
        {
            if (block_id_to_lock_id_map_.size() == lock_numbers)
            {
                for (int i = 0; i < 5; i++)
                {
                    if (block_id_to_lock_id_map_.size() == lock_numbers) {
                        deallocate_all_unlocked_unblocked();
                        std::this_thread::sleep_for(std::chrono::nanoseconds(1l));
                    } else {
                        break;
                    }
                }
            }

            if (block_id_to_lock_id_map_.size() == lock_numbers) {
                throw error::no_locks_available();
            }

            auto try_alloc_lock = [&](const uint64_t try_allocate_loc)->bool
            {
                if (try_allocate_loc >= lock_numbers) return false;
                if (auto & status = lock_array_allocate_map_[try_allocate_loc]) {
                    return false;
                } else {
                    status = true;
                    return true;
                }
            };

            last_allocated_position++;
            while (!try_alloc_lock(last_allocated_position))
            {
                if (last_allocated_position < lock_numbers) {
                    last_allocated_position++;
                } else {
                    last_allocated_position = 0;
                }
            }

            block_id_to_lock_id_map_[index] = last_allocated_position;
            return last_allocated_position;
        }

    public:
        void lock(const uint64_t index)
        {
            // dlog("lock ", index, "\n");
            std::mutex * mutex = nullptr;
            {
                std::lock_guard<std::mutex> lock(g_lock_array_alloc_dealloc_mtx_);
                const auto ptr = block_id_to_lock_id_map_.find(index);
                if (ptr == block_id_to_lock_id_map_.end()) {
                    const auto lock_id = allocate_lock_array_unblocked(index);
                    mutex = &lock_array_[lock_id];
                }
                else {
                    mutex = &lock_array_[ptr->second];
                }
            }

            if (mutex) {
                mutex->lock();
            }
            // dlog("locked ", index, ", map: ", block_id_to_lock_id_map_, "\n");
        }

        void unlock(const uint64_t index)
        {
            std::mutex * mutex = nullptr;
            {
                std::lock_guard<std::mutex> lock(g_lock_array_alloc_dealloc_mtx_);
                const auto ptr = block_id_to_lock_id_map_.find(index);
                if (ptr != block_id_to_lock_id_map_.end()) {
                    mutex = &lock_array_[ptr->second];
                }
            }
            if (mutex) {
                mutex->unlock();
                // dlog("unlocked ", index, "\n");
            }
        }
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
    void make_cfs(const std::string &path_to_block_file, const uint64_t block_size, const std::string & label);

    class filesystem
    {
    private:
        basic_io::mmap file_;
        block_shared_lock_t bitlocker_;
        const cfs_head_t::static_info_t static_info_;

    protected:
        class cfs_header_block_t {
        private:
            block_shared_lock_t * bitlocker_ = nullptr;
            const uint64_t tailing_header_blk_id_ = 0;
            cfs_head_t * fs_head;
            cfs_head_t * fs_end;

        public:
            /// get runtime info from header blocks (both tailing and leading)
            /// @return Runtime info
            cfs_head_t::runtime_info_t load();

            /// set runtime info to header blocks (both tailing and leading)
            /// @param info New runtime info
            void set(const cfs_head_t::runtime_info_t &info);

        private:
            cfs_header_block_t() = default;
            ~cfs_header_block_t() = default;

        public:
            friend class filesystem;
        } cfs_header_block;

    public:
        /// check headers, fix if possible, and create a bit state locker for all blocks
        explicit filesystem(const std::string & path_to_block_file);

        /// lock guard
        class guard {
        private:
            block_shared_lock_t * bitlocker_;
            char * data_;
            uint64_t block_address_;
            guard(block_shared_lock_t * bitlocker, char * data, const uint64_t block_address) :
                bitlocker_(bitlocker), data_(data), block_address_(block_address)
            {
                bitlocker_->lock(block_address_);
            }

        public:
            ~guard() {
                // dlog("unlock ", block_address_, "\n");
                bitlocker_->unlock(block_address_);
            }

            /// get the address of the currently locked block page
            char * data() const { return data_; }

            friend class filesystem;
        };

        /// Lock a certain block
        /// @param index Block ID to lock
        /// @return lock_guard
        guard lock(const uint64_t index) {
            cfs_assert_simple(index > 0 && index < static_info_.blocks - 1);
            return guard(&this->bitlocker_, this->file_.data() + index * static_info_.block_size, index);
        }

        /// flush all data, write clean flag, close file
        ~filesystem();
    };
}

#endif //CFS_SMART_BLOCK_T_H