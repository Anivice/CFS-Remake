#ifndef CFS_CFSBASICCOMPONENTS_H
#define CFS_CFSBASICCOMPONENTS_H

#include "cfs.h"
#include "commandTemplateTree.h"
#include "smart_block_t.h"
#include "generalCFSbaseError.h"
#include "tsl/hopscotch_map.h"

make_simple_error_class(no_more_free_spaces)
make_simple_error_class_traceable(tracer)

#define auto_write_two_two(j, f, s_a, s_d, ss_a, ss_d, f_a, f_d)                        \
    cfs::journal_auto_write_t journal_auto_write(j, f,                                  \
        s_a, s_d, 0, 0, 0, 0,                                                           \
        ss_a, ss_d, 0, 0, 0, 0,                                                         \
        f_a, f_d, 0, 0, 0, 0)

#define auto_write_three_two(j, f, s_a, s_d, s_dx, ss_a, ss_d, f_a, f_d)                \
    cfs::journal_auto_write_t journal_auto_write(j, f,                                  \
        s_a, s_d, s_dx, 0, 0, 0,                                                        \
        ss_a, ss_d, 0, 0, 0, 0,                                                         \
        f_a, f_d, 0, 0, 0, 0)

#define auto_write_four_two(j, f, s_a, s_d, s_dx, s_dx1, ss_a, ss_d, f_a, f_d)          \
    cfs::journal_auto_write_t journal_auto_write(j, f,                                  \
        s_a, s_d, s_dx, s_dx1, 0, 0,                                                    \
        ss_a, ss_d, 0, 0, 0, 0,                                                         \
        f_a, f_d, 0, 0, 0, 0)

#define auto_write_five_two(j, f, s_a, s_d, s_dx, s_dx1, s_dx2, ss_a, ss_d, f_a, f_d)   \
    cfs::journal_auto_write_t journal_auto_write(j, f,                                  \
        s_a, s_d, s_dx, s_dx1, s_dx2, 0,                                                \
        ss_a, ss_d, 0, 0, 0, 0,                                                         \
        f_a, f_d, 0, 0, 0, 0)

#define auto_write_six_two(j, f, s_a, s_d, s_dx, s_dx1, s_dx2, s_dx3, ss_a, ss_d, f_a, f_d)         \
    cfs::journal_auto_write_t journal_auto_write(j, f,                                              \
        s_a, s_d, s_dx, s_dx1, s_dx2, s_dx3,                                                        \
        ss_a, ss_d, 0, 0, 0, 0,                                                                     \
        f_a, f_d, 0, 0, 0, 0)

#define g_transaction_3(j, f, act) \
    auto_write_two_two(j, f, GlobalTransaction, act, GlobalTransaction, act##_Completed, GlobalTransaction, act##_Failed)

#define g_transaction_4(j, f, act, dx) \
    auto_write_three_two(j, f, GlobalTransaction, act, dx, GlobalTransaction, act##_Completed, GlobalTransaction, act##_Failed)

#define g_transaction_5(j, f, act, dx, dx1) \
    auto_write_four_two(j, f, GlobalTransaction, act, dx, dx1, GlobalTransaction, act##_Completed, GlobalTransaction, act##_Failed)

#define g_transaction_6(j, f, act, dx, dx1, dx2) \
    auto_write_five_two(j, f, GlobalTransaction, act, dx, dx1, dx2, GlobalTransaction, act##_Completed, GlobalTransaction, act##_Failed)

#define g_transaction_7(j, f, act, dx, dx1, dx2, dx3) \
    auto_write_six_two(j, f, GlobalTransaction, act, dx, dx1, dx2, dx3, GlobalTransaction, act##_Completed, GlobalTransaction, act##_Failed)

#define __COUNT_ARGS(_0, _1, _2, _3, _4, _5, _6, _7, N, ...) N

#define COUNT_ARGS(...) \
    __COUNT_ARGS(, ##__VA_ARGS__, \
    g_transaction_7,g_transaction_6,g_transaction_5,g_transaction_4,g_transaction_3, \
    2,1,0)

#define g_transaction(...)  COUNT_ARGS(__VA_ARGS__)(__VA_ARGS__)

namespace cfs
{
    class cfs_journaling_t
    {
        cfs::filesystem * parent_fs_governor_;
        const uint64_t journal_start_;
        const uint64_t journal_end_;
        const uint64_t block_size_;
        const uint64_t capacity_ = 0;

        char * journal_raw_buffer_;
        char * journal_body_;

        journal_header_t * journal_header_;     // start of the ring
        journal_header_t * journal_header_cow_; // end of the ring
        std::mutex mutex_;

        /// put a 8bit data into the journal
        /// @param c 8bit data
        void putc(char c);

        /// dump all journal data
        [[nodiscard]] std::vector<uint8_t> dump() const;

    public:
        explicit cfs_journaling_t(cfs::filesystem * parent_fs_governor);
        ~cfs_journaling_t();

        /// dump all journal actions
        [[nodiscard]] std::vector < cfs_action_t > dump_actions();

        /// push an action into the journal
        /// @param action Filesystem action
        /// @param action_param0 Action parameter 0
        /// @param action_param1 Action parameter 1
        /// @param action_param2 Action parameter 2
        /// @param action_param3 Action parameter 3
        /// @param action_param4 Action parameter 4
        void push_action(
            uint64_t action,
            uint64_t action_param0 = 0,
            uint64_t action_param1 = 0,
            uint64_t action_param2 = 0,
            uint64_t action_param3 = 0,
            uint64_t action_param4 = 0);

        NO_COPY_OBJ(cfs_journaling_t);
    };

    class cfs_bitmap_singular_t : public bitmap_base
    {
    public:
        explicit cfs_bitmap_singular_t(char * mapped_area, uint64_t data_block_numbers);

        /// get CRC64 of the whole bitmap
        // [[nodiscard]] uint64_t dump_checksum64();
    };

    class cfs_bitmap_block_mirroring_t
    {
        cfs_bitmap_singular_t mirror1;
        cfs_bitmap_singular_t mirror2;
        cfs::filesystem * parent_fs_governor_;
        cfs_journaling_t * journal_;

        // /// Small cache pool
        // /// @tparam static_cache_size Static cache size
        // template < uint64_t static_cache_size >
        // class static_cache_t
        // {
        //     tsl::hopscotch_map < uint64_t, bool > bitmap_static_level_cache_;
        //     tsl::hopscotch_map < uint64_t, uint64_t > bitmap_static_level_access_counter_;
        //     std::mutex static_level_cache_mtx_;
        //
        // public:
        //     /// initialize small cache pool
        //     static_cache_t();
        //
        //     /// set small cache pool
        //     /// @param index index
        //     /// @param val Value
        //     void set_fast_cache(uint64_t index, bool val);
        //
        //     /// get small cache pool
        //     /// @param index Index
        //     /// @return -1 if not found, or 0/1 to indicate values
        //     int get_fast_cache(uint64_t index);
        // };

        // static_cache_t<1024 * 1024 * 64> small_cache_;

    public:
        explicit cfs_bitmap_block_mirroring_t(cfs::filesystem * parent_fs_governor, cfs_journaling_t * journal);

        /// Get bit at the specific location
        /// @param index Bit Index
        /// @return The bit at the specific location
        /// @throws cfs::error::assertion_failed Out of bounds
        bool get_bit(uint64_t index);

        /// Set the bit at the specific location
        /// @param index Bit Index
        /// @param new_bit The new bit value
        /// @return NONE
        /// @throws cfs::error::assertion_failed Out of bounds
        void set_bit(uint64_t index, bool new_bit);
    };

    // template<uint64_t static_cache_size>
    // cfs_bitmap_block_mirroring_t::static_cache_t<static_cache_size>::static_cache_t() {
    //     bitmap_static_level_cache_.reserve(static_cache_size);
    // }
    //
    // template<uint64_t static_cache_size>
    // void cfs_bitmap_block_mirroring_t::static_cache_t<static_cache_size>::set_fast_cache(
    //     const uint64_t index,
    //     const bool val)
    // {
    //     std::lock_guard<std::mutex> guard(static_level_cache_mtx_);
    //     if (bitmap_static_level_cache_.size() > static_cache_size)
    //     {
    //         std::vector<std::pair<uint64_t, uint64_t>> access_cache_;
    //         for (auto ptr = bitmap_static_level_cache_.begin(); ptr != bitmap_static_level_cache_.end();++ptr)
    //         {
    //             if (auto acc = bitmap_static_level_access_counter_.find(ptr->first);
    //                 acc == bitmap_static_level_access_counter_.end())
    //             {
    //                 bitmap_static_level_cache_.erase(ptr);
    //             }
    //         }
    //
    //         for (const auto & [pos, rate] : bitmap_static_level_access_counter_) {
    //             access_cache_.emplace_back(pos, rate);
    //         }
    //
    //         std::ranges::sort(access_cache_,
    //                           [](const std::pair <uint64_t, uint64_t> & a, const std::pair <uint64_t, uint64_t> & b)->bool {
    //                               return a.second < b.second;
    //                           });
    //         access_cache_.resize(access_cache_.size() / 2);
    //         std::ranges::for_each(access_cache_, [&](const std::pair <uint64_t, uint64_t> & pos) {
    //             bitmap_static_level_cache_.erase(pos.first);
    //         });
    //         bitmap_static_level_access_counter_.clear();
    //     }
    //
    //     bitmap_static_level_cache_[index] = val;
    // }
    //
    // template<uint64_t static_cache_size>
    // int cfs_bitmap_block_mirroring_t::static_cache_t<static_cache_size>::get_fast_cache(const uint64_t index)
    // {
    //     std::lock_guard<std::mutex> guard(static_level_cache_mtx_);
    //     const auto ptr = bitmap_static_level_cache_.find(index);
    //     if (ptr == bitmap_static_level_cache_.end()) {
    //         return -1;
    //     }
    //
    //     bitmap_static_level_access_counter_[index]++;
    //     return ptr->second;
    // }
    
    class block_status { };    // => BlockStatusType
    class block_type { };      // => BlockType
    class block_type_cow { };
    class allocation_oom_scan_per_refresh_count { };
    class newly_allocated_thus_no_cow { };
    class index_node_referencing_number { }; // Max 0xFFFF (65535) depth of snapshots
    class block_checksum { };

    class cfs_block_attribute_access_t {
        filesystem * parent_fs_governor_;
        cfs_journaling_t * journal_;
        cfs::filesystem::block_shared_lock_t location_lock_;

    public:
        explicit cfs_block_attribute_access_t(filesystem * parent_fs_governor, cfs_journaling_t * journal);

        class smart_lock_t {
            cfs_block_attribute_t before_ { };
            cfs_block_attribute_access_t * parent_;

        public:
            cfs_block_attribute_t * data_;
            cfs_block_attribute_t * operator->() const { return data_; }
            cfs_block_attribute_t & operator*() const { return *data_; }

        private:
            const uint64_t index_;
            explicit smart_lock_t(cfs_block_attribute_access_t * parent, uint64_t index);

        public:
            ~smart_lock_t();

            void copy_on_write() {
                parent_->journal_->push_action(FilesystemAttributeModification,
                                               *reinterpret_cast<uint32_t *>(&before_),
                                               index_); // journal set after
            }

            friend class cfs_block_attribute_access_t;
            NO_COPY_OBJ(smart_lock_t)
        };

    protected:
        [[nodiscard]] smart_lock_t lock(const uint64_t index) { return smart_lock_t(this, index); }

    public:
        template < typename Type >
        requires (std::is_same_v<Type, block_status>
            || std::is_same_v<Type, block_type>
            || std::is_same_v<Type, block_type_cow>
            || std::is_same_v<Type, allocation_oom_scan_per_refresh_count>
            || std::is_same_v<Type, newly_allocated_thus_no_cow>
            || std::is_same_v<Type, index_node_referencing_number>
            || std::is_same_v<Type, block_checksum>)
        uint32_t get(uint64_t index);

        template < typename Type >
        requires (std::is_same_v<Type, block_status>
            || std::is_same_v<Type, block_type>
            || std::is_same_v<Type, block_type_cow>
            || std::is_same_v<Type, allocation_oom_scan_per_refresh_count>
            || std::is_same_v<Type, newly_allocated_thus_no_cow>
            || std::is_same_v<Type, index_node_referencing_number>
            || std::is_same_v<Type, block_checksum>)
        void set(uint64_t index, uint32_t value);

        template < typename Type1, typename Type2 >
        requires (
               std::is_same_v<Type1, block_status>
            || std::is_same_v<Type1, block_type>
            || std::is_same_v<Type1, block_type_cow>
            || std::is_same_v<Type1, allocation_oom_scan_per_refresh_count>
            || std::is_same_v<Type1, newly_allocated_thus_no_cow>
            || std::is_same_v<Type1, index_node_referencing_number>
            || std::is_same_v<Type1, block_checksum>
            || std::is_same_v<Type2, block_status>
            || std::is_same_v<Type2, block_type>
            || std::is_same_v<Type2, block_type_cow>
            || std::is_same_v<Type2, allocation_oom_scan_per_refresh_count>
            || std::is_same_v<Type2, newly_allocated_thus_no_cow>
            || std::is_same_v<Type2, index_node_referencing_number>
            || std::is_same_v<Type2, block_checksum>)
        void move(uint64_t index);

        template < typename Type >
        requires (std::is_same_v<Type, block_status>
            || std::is_same_v<Type, block_type>
            || std::is_same_v<Type, block_type_cow>
            || std::is_same_v<Type, allocation_oom_scan_per_refresh_count>
            || std::is_same_v<Type, newly_allocated_thus_no_cow>
            || std::is_same_v<Type, index_node_referencing_number>
            || std::is_same_v<Type, block_checksum>)
        void inc(uint64_t index, uint32_t value = 1);

        template < typename Type >
        requires (std::is_same_v<Type, block_status>
            || std::is_same_v<Type, block_type>
            || std::is_same_v<Type, block_type_cow>
            || std::is_same_v<Type, allocation_oom_scan_per_refresh_count>
            || std::is_same_v<Type, newly_allocated_thus_no_cow>
            || std::is_same_v<Type, index_node_referencing_number>
            || std::is_same_v<Type, block_checksum>)
        void dec(uint64_t index, uint32_t value = 1);

        void clear(const uint64_t index, const cfs_block_attribute_t & value = { }) { *lock(index) = value; }
        friend class smart_lock_t;
    };

    template<typename Type>
    requires (std::is_same_v<Type, block_status>
        || std::is_same_v<Type, block_type>
        || std::is_same_v<Type, block_type_cow>
        || std::is_same_v<Type, allocation_oom_scan_per_refresh_count>
        || std::is_same_v<Type, newly_allocated_thus_no_cow>
        || std::is_same_v<Type, index_node_referencing_number>
        || std::is_same_v<Type, block_checksum>)
    uint32_t cfs_block_attribute_access_t::get(const uint64_t index)
    {
        if constexpr (std::is_same_v<Type, block_status>) {
            return lock(index)->block_status;
        }
        else if constexpr (std::is_same_v<Type, block_type>) {
            return lock(index)->block_type;
        }
        else if constexpr (std::is_same_v<Type, block_type_cow>) {
            return lock(index)->block_type_cow;
        }
        else if constexpr (std::is_same_v<Type, allocation_oom_scan_per_refresh_count>) {
            return lock(index)->allocation_oom_scan_per_refresh_count;
        }
        else if constexpr (std::is_same_v<Type, newly_allocated_thus_no_cow>) {
            return lock(index)->newly_allocated_thus_no_cow;
        }
        else if constexpr (std::is_same_v<Type, index_node_referencing_number>) {
            return lock(index)->index_node_referencing_number;
        }
        else if constexpr (std::is_same_v<Type, block_checksum>) {
            return lock(index)->block_checksum;
        }
        else {
            // already guarded in requires
            return 0;
        }
    }

    template<typename Type>
    requires (std::is_same_v<Type, block_status>
    || std::is_same_v<Type, block_type>
    || std::is_same_v<Type, block_type_cow>
    || std::is_same_v<Type, allocation_oom_scan_per_refresh_count>
    || std::is_same_v<Type, newly_allocated_thus_no_cow>
    || std::is_same_v<Type, index_node_referencing_number>
    || std::is_same_v<Type, block_checksum>)
    void cfs_block_attribute_access_t::set(const uint64_t index, const uint32_t value)
    {
        if constexpr (std::is_same_v<Type, block_status>) {
            lock(index)->block_status = value;
        }
        else if constexpr (std::is_same_v<Type, block_type>) {
            lock(index)->block_type = value;
        }
        else if constexpr (std::is_same_v<Type, block_type_cow>) {
            lock(index)->block_type_cow = value;
        }
        else if constexpr (std::is_same_v<Type, allocation_oom_scan_per_refresh_count>) {
            lock(index)->allocation_oom_scan_per_refresh_count = value;
        }
        else if constexpr (std::is_same_v<Type, newly_allocated_thus_no_cow>) {
            lock(index)->newly_allocated_thus_no_cow = value;
        }
        else if constexpr (std::is_same_v<Type, index_node_referencing_number>) {
            lock(index)->index_node_referencing_number = value;
        }
        else if constexpr (std::is_same_v<Type, block_checksum>) {
            lock(index)->block_checksum = value;
        }
        else {
            // already guarded in requires
        }
    }

    template<typename Type1, typename Type2>
    requires (
       std::is_same_v<Type1, block_status>
    || std::is_same_v<Type1, block_type>
    || std::is_same_v<Type1, block_type_cow>
    || std::is_same_v<Type1, allocation_oom_scan_per_refresh_count>
    || std::is_same_v<Type1, newly_allocated_thus_no_cow>
    || std::is_same_v<Type1, index_node_referencing_number>
    || std::is_same_v<Type1, block_checksum>
    || std::is_same_v<Type2, block_status>
    || std::is_same_v<Type2, block_type>
    || std::is_same_v<Type2, block_type_cow>
    || std::is_same_v<Type2, allocation_oom_scan_per_refresh_count>
    || std::is_same_v<Type2, newly_allocated_thus_no_cow>
    || std::is_same_v<Type2, index_node_referencing_number>
    || std::is_same_v<Type2, block_checksum>)
    void cfs_block_attribute_access_t::move(const uint64_t index)
    {
        const auto lock_ = lock(index);
        uint32_t val{};
        if constexpr (std::is_same_v<Type1, block_status>) {
            val = lock_->block_status;
        }
        else if constexpr (std::is_same_v<Type1, block_type>) {
            val = lock_->block_type;
        }
        else if constexpr (std::is_same_v<Type1, block_type_cow>) {
            val = lock_->block_type_cow;
        }
        else if constexpr (std::is_same_v<Type1, allocation_oom_scan_per_refresh_count>) {
            val = lock_->allocation_oom_scan_per_refresh_count;
        }
        else if constexpr (std::is_same_v<Type1, newly_allocated_thus_no_cow>) {
            val = lock_->newly_allocated_thus_no_cow;
        }
        else if constexpr (std::is_same_v<Type1, index_node_referencing_number>) {
            val = lock_->index_node_referencing_number;
        }
        else if constexpr (std::is_same_v<Type1, block_checksum>) {
            val = lock_->block_checksum;
        }
        else {
            // already guarded in requires
        }

        if constexpr (std::is_same_v<Type2, block_status>) {
            lock_->block_status = val ;
        }
        else if constexpr (std::is_same_v<Type2, block_type>) {
            lock_->block_type = val ;
        }
        else if constexpr (std::is_same_v<Type2, block_type_cow>) {
            lock_->block_type_cow = val ;
        }
        else if constexpr (std::is_same_v<Type2, allocation_oom_scan_per_refresh_count>) {
            lock_->allocation_oom_scan_per_refresh_count = val ;
        }
        else if constexpr (std::is_same_v<Type2, newly_allocated_thus_no_cow>) {
            lock_->newly_allocated_thus_no_cow = val ;
        }
        else if constexpr (std::is_same_v<Type2, index_node_referencing_number>) {
            lock_->index_node_referencing_number = val ;
        }
        else if constexpr (std::is_same_v<Type2, block_checksum>) {
            lock_->block_checksum = val ;
        }
        else {
            // already guarded in requires
        }
    }

    template<typename Type>
    requires (std::is_same_v<Type, block_status>
    || std::is_same_v<Type, block_type>
    || std::is_same_v<Type, block_type_cow>
    || std::is_same_v<Type, allocation_oom_scan_per_refresh_count>
    || std::is_same_v<Type, newly_allocated_thus_no_cow>
    || std::is_same_v<Type, index_node_referencing_number>
    || std::is_same_v<Type, block_checksum>)
    void cfs_block_attribute_access_t::inc(const uint64_t index, const uint32_t value)
    {
        if constexpr (std::is_same_v<Type, block_status>) {
            lock(index)->block_status += value;
        }
        else if constexpr (std::is_same_v<Type, block_type>) {
            lock(index)->block_type += value;
        }
        else if constexpr (std::is_same_v<Type, block_type_cow>) {
            lock(index)->block_type_cow += value;
        }
        else if constexpr (std::is_same_v<Type, allocation_oom_scan_per_refresh_count>) {
            lock(index)->allocation_oom_scan_per_refresh_count += value;
        }
        else if constexpr (std::is_same_v<Type, newly_allocated_thus_no_cow>) {
            lock(index)->newly_allocated_thus_no_cow += value;
        }
        else if constexpr (std::is_same_v<Type, index_node_referencing_number>) {
            lock(index)->index_node_referencing_number += value;
        }
        else if constexpr (std::is_same_v<Type, block_checksum>) {
            lock(index)->block_checksum += value;
        }
        else {
            // already guarded in requires
        }
    }

    template<typename Type>
    requires (std::is_same_v<Type, block_status>
    || std::is_same_v<Type, block_type>
    || std::is_same_v<Type, block_type_cow>
    || std::is_same_v<Type, allocation_oom_scan_per_refresh_count>
    || std::is_same_v<Type, newly_allocated_thus_no_cow>
    || std::is_same_v<Type, index_node_referencing_number>
    || std::is_same_v<Type, block_checksum>)
    void cfs_block_attribute_access_t::dec(const uint64_t index, const uint32_t value)
    {
        if constexpr (std::is_same_v<Type, block_status>) {
            lock(index)->block_status -= value;
        }
        else if constexpr (std::is_same_v<Type, block_type>) {
            lock(index)->block_type -= value;
        }
        else if constexpr (std::is_same_v<Type, block_type_cow>) {
            lock(index)->block_type_cow -= value;
        }
        else if constexpr (std::is_same_v<Type, allocation_oom_scan_per_refresh_count>) {
            lock(index)->allocation_oom_scan_per_refresh_count -= value;
        }
        else if constexpr (std::is_same_v<Type, newly_allocated_thus_no_cow>) {
            lock(index)->newly_allocated_thus_no_cow -= value;
        }
        else if constexpr (std::is_same_v<Type, index_node_referencing_number>) {
            lock(index)->index_node_referencing_number -= value;
        }
        else if constexpr (std::is_same_v<Type, block_checksum>) {
            lock(index)->block_checksum -= value;
        }
        else {
            // already guarded in requires
        }
    }

    /// auto write to journal so I don't have to
    class journal_auto_write_t {
        cfs_journaling_t * journal_;
        bool & success_;


        // success
        const uint64_t success_action_;
        const uint64_t success_action_param0_;
        const uint64_t success_action_param1_;
        const uint64_t success_action_param2_;
        const uint64_t success_action_param3_;
        const uint64_t success_action_param4_;

        // failed
        const uint64_t failed_action_;
        const uint64_t failed_action_param0_;
        const uint64_t failed_action_param1_;
        const uint64_t failed_action_param2_;
        const uint64_t failed_action_param3_;
        const uint64_t failed_action_param4_;

    public:
        journal_auto_write_t(
            cfs_journaling_t * journal,
            bool & success,

            // start
            uint64_t start_action,
            uint64_t start_action_param0,
            uint64_t start_action_param1,
            uint64_t start_action_param2,
            uint64_t start_action_param3,
            uint64_t start_action_param4,

            // success
            uint64_t success_action,
            uint64_t success_action_param0,
            uint64_t success_action_param1,
            uint64_t success_action_param2,
            uint64_t success_action_param3,
            uint64_t success_action_param4,

            // failed
            uint64_t failed_action,
            uint64_t failed_action_param0,
            uint64_t failed_action_param1,
            uint64_t failed_action_param2,
            uint64_t failed_action_param3,
            uint64_t failed_action_param4);

        ~journal_auto_write_t();
    };

    class cfs_block_manager_t {
        cfs_bitmap_block_mirroring_t * bitmap_;
        filesystem::cfs_header_block_t * header_;
        cfs_block_attribute_access_t * block_attribute_;
        cfs_journaling_t * journal_;

    public:
        cfs_block_manager_t(
            cfs_bitmap_block_mirroring_t * bitmap,
            filesystem::cfs_header_block_t * header,
            cfs_block_attribute_access_t * block_attribute,
            cfs_journaling_t * journal
            );

        /// allocate a new block
        /// @return New block index
        /// @throws cfs::error::no_more_free_spaces Space ran out
        [[nodiscard]] uint64_t allocate();

        /// deallocate a block
        /// @param index Block index
        void deallocate(uint64_t index);
    };

    template < typename F> concept Allocator_ = requires(F f, const uint8_t c) { { std::invoke(f, c) } -> std::same_as<uint64_t>; };
    template < typename F> concept Deallocator_ = requires(F f, uint64_t index) {
        { std::invoke(f, index) } -> std::same_as<void>;
    };

    class cfs_inode_service_t : protected cfs_inode_t
    {
        filesystem::guard inode_effective_lock_;
        filesystem * parent_fs_governor_;
        cfs_block_manager_t * block_manager_;
        cfs_journaling_t * journal_;
        cfs_block_attribute_access_t * block_attribute_;
        const uint64_t block_size_;
        const uint64_t block_index_;

        struct linearized_block_t {
            std::vector < uint64_t > level1_pointers;
            std::vector < uint64_t > level2_pointers;
            std::vector < uint64_t > level3_pointers;
        };

        struct allocation_map_t {
            std::vector < std::pair < uint64_t, bool > > level1_pointers;
            std::vector < std::pair < uint64_t, bool > > level2_pointers;
            std::vector < std::pair < uint64_t, bool > > level3_pointers;
        };

        struct linearized_block_descriptor_t {
            uint64_t level1_pointers;
            uint64_t level2_pointers;
            uint64_t level3_pointers;
        };

        /// lock data block ID
        /// @param index data block ID
        /// @return page lock
        [[nodiscard]] auto lock_page(const uint64_t index) const {
            cfs_assert_simple(index != block_index_);
            return parent_fs_governor_->lock(index + parent_fs_governor_->static_info_.data_table_start);
        }

        /// copy-on-write for one block
        /// @param index Block index
        /// @return Redundancy block index
        uint64_t copy_on_write(uint64_t index);

    public:

        /// Linearize all blocks by st_size
        /// @return linearized pointers in std::vector <uint64_t> * 3 struct
        [[nodiscard]] linearized_block_t linearize_all_blocks() const;

        /// calculate how many pointers for each level
        /// @return pointers required for each level for this particular size
        [[nodiscard]] linearized_block_descriptor_t size_to_linearized_block_descriptor(uint64_t size) const;

        /// Smart allocator and deallocator
        /// @tparam FuncAlloc Allocator
        /// @tparam FuncDealloc Deallocator
        template < Allocator_ FuncAlloc, Deallocator_ FuncDealloc >
        class smart_reallocate_t {
        public:
            FuncAlloc alloc_;
            FuncDealloc dealloc_;
            uint64_t block_index_ = UINT64_MAX;
            bool control_ = true;
            bool allocated_by_smart_ = false;
            uint8_t block_type_ = STORAGE_BLOCK;

            explicit smart_reallocate_t(
                FuncAlloc alloc, FuncDealloc dealloc,
                const uint64_t block_index, const bool control, const uint8_t block_type)
            : alloc_(alloc), dealloc_(dealloc), block_index_(block_index), control_(control), block_type_(block_type) { }

            smart_reallocate_t(FuncAlloc alloc, FuncDealloc dealloc, const uint8_t block_type)
            : alloc_(alloc), dealloc_(dealloc), block_type_(block_type) {
                if (control_) { block_index_ = alloc(block_type_); allocated_by_smart_ = true; }
            }

            ~smart_reallocate_t() {
                if (control_ && block_index_ != 0) { dealloc_(block_index_); }
            }
        };

        allocation_map_t reallocate_linearized_block_by_descriptor(const linearized_block_descriptor_t & descriptor);
        void commit_from_linearized_block(allocation_map_t descriptor);

        void commit_from_block_descriptor(const linearized_block_descriptor_t & descriptor);

    public:
        cfs_inode_service_t(
            uint64_t index,
            filesystem * parent_fs_governor,
            cfs_block_manager_t * block_manager,
            cfs_journaling_t * journal,
            cfs_block_attribute_access_t * block_attribute);

        uint64_t read(char * data, uint64_t size, uint64_t offset) const;
        uint64_t write(const char * data, uint64_t size, uint64_t offset);

        /// resize this inode
        /// @param new_size New size
        void resize(uint64_t new_size);

        void chdev(int dev);
        void chrdev(dev_t dev);
        void chmod(int mode);
        void chown(int uid, int gid);
        void set_atime(timespec st_atim);
        void set_ctime(timespec st_ctim);
        void set_mtime(timespec st_mtim);

        /// get struct stat
        [[nodiscard]] struct stat get_stat () const { return *cfs_inode_attribute; }
    };

    class cfs_directory_t : public cfs_inode_service_t {
    public:
        cfs_directory_t(
            uint64_t index,
            filesystem * parent_fs_governor,
            cfs_block_manager_t * block_manager,
            cfs_journaling_t * journal,
            cfs_block_attribute_access_t * block_attribute);

        /// create an inode under current directory
        cfs_inode_service_t make_inode(const std::string & name);
    };
}

#endif //CFS_CFSBASICCOMPONENTS_H