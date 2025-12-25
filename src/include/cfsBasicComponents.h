#ifndef CFS_CFSBASICCOMPONENTS_H
#define CFS_CFSBASICCOMPONENTS_H

#include "cfs.h"
#include "commandTemplateTree.h"
#include "smart_block_t.h"
#include "generalCFSbaseError.h"
#include "tsl/hopscotch_map.h"

make_simple_error_class(no_more_free_spaces)

#define auto_write_two_two(j, f, s_a, s_d, ss_a, ss_d, f_a, f_d)                        \
    cfs::journal_auto_write_t journal_auto_write(j, f,                                  \
        s_a, s_d, 0, 0, 0, 0,                                                           \
        ss_a, ss_d, 0, 0, 0, 0,                                                         \
        f_a, f_d, 0, 0, 0, 0)

#define auto_write_three_two(j, f, s_a, s_d, s_dx, ss_a, ss_d, f_a, f_d)                \
    cfs::journal_auto_write_t journal_auto_write(j, f,                                  \
        s_a, s_d, s_dx, 0, 0, 0,                                                        \
        ss_a, ss_d, s_dx, 0, 0, 0,                                                      \
        f_a, f_d, s_dx, 0, 0, 0)

#define auto_write_four_two(j, f, s_a, s_d, s_dx, s_dx1, ss_a, ss_d, f_a, f_d)          \
    cfs::journal_auto_write_t journal_auto_write(j, f,                                  \
        s_a, s_d, s_dx, s_dx1, 0, 0,                                                    \
        ss_a, ss_d, s_dx, s_dx1, 0, 0,                                                  \
        f_a, f_d,s_dx, s_dx1, 0, 0)

#define auto_write_five_two(j, f, s_a, s_d, s_dx, s_dx1, s_dx2, ss_a, ss_d, f_a, f_d)   \
    cfs::journal_auto_write_t journal_auto_write(j, f,                                  \
        s_a, s_d, s_dx, s_dx1, s_dx2, 0,                                                \
        ss_a, ss_d, s_dx, s_dx1, s_dx2, 0,                                              \
        f_a, f_d, s_dx, s_dx1, s_dx2, 0)

#define auto_write_six_two(j, f, s_a, s_d, s_dx, s_dx1, s_dx2, s_dx3, ss_a, ss_d, f_a, f_d)         \
    cfs::journal_auto_write_t journal_auto_write(j, f,                                              \
        s_a, s_d, s_dx, s_dx1, s_dx2, s_dx3,                                                        \
        ss_a, ss_d, s_dx, s_dx1, s_dx2, s_dx3,                                                      \
        f_a, f_d, s_dx, s_dx1, s_dx2, s_dx3)

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

        [[nodiscard]] void * data() const { return this->data_array_; }
        [[nodiscard]] uint64_t size() const { return this->bytes_required_; }

        /// get CRC64 of the whole bitmap
        // [[nodiscard]] uint64_t dump_checksum64();
    };

    class cfs_bitmap_block_mirroring_t
    {
        cfs_bitmap_singular_t mirror1;
        cfs_bitmap_singular_t mirror2;
        cfs::filesystem * parent_fs_governor_;
        cfs_journaling_t * journal_;
        std::mutex dump_mutex_;

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

        std::vector<uint8_t> dump()
        {
            std::lock_guard lock(dump_mutex_);
            std::vector<uint8_t> ret;
            ret.resize(mirror1.size());
            std::memcpy(ret.data(), mirror1.data(), mirror1.size());
            return ret;
        }
    };
    
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
        // std::atomic_bool dirty_ = false;

    public:
        // bool dirty() { return dirty_; }

        std::vector<uint8_t> dump()
        {
            const uint64_t map_len = parent_fs_governor_->static_info_.data_table_end - parent_fs_governor_->static_info_.data_table_start;
            std::vector<uint32_t> ret(map_len, 0);
            for (uint64_t i = 0; i < map_len; i++)
            {
                uint32_t it = 0;
                *(cfs_block_attribute_t *)(&it) = get(i);
                ret.push_back(it);
            }

            std::vector<uint8_t> dump(map_len * sizeof(uint32_t), 0);
            std::memcpy(dump.data(), ret.data(), dump.size());
            return dump;
        }

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

            friend class cfs_block_attribute_access_t;
            NO_COPY_OBJ(smart_lock_t)
        };

    protected:
        [[nodiscard]] smart_lock_t lock(const uint64_t index) { return smart_lock_t(this, index); }

    public:
        cfs_block_attribute_t get(const uint64_t index) { return *lock(index); }

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

        /// move value
        /// @tparam Type1 From
        /// @tparam Type2 To
        /// @param index Block Index
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
        requires (std::is_same_v<Type, allocation_oom_scan_per_refresh_count>
            || std::is_same_v<Type, index_node_referencing_number>)
        void inc(uint64_t index, uint32_t value = 1);

        template < typename Type >
        requires (std::is_same_v<Type, allocation_oom_scan_per_refresh_count>
            || std::is_same_v<Type, index_node_referencing_number>)
        void dec(uint64_t index, uint32_t value = 1);

        void clear(const uint64_t index, const cfs_block_attribute_t & value = { })
        {
            auto lock_ = lock(index);

            if (value.block_type == COW_REDUNDANCY_BLOCK && lock_->block_type != COW_REDUNDANCY_BLOCK) {
                this->parent_fs_governor_->cfs_header_block.dec<allocated_non_cow_blocks>();
            } else if (value.block_type != COW_REDUNDANCY_BLOCK && lock_->block_type == COW_REDUNDANCY_BLOCK) {
                this->parent_fs_governor_->cfs_header_block.inc<allocated_non_cow_blocks>();
            }

            *lock_ = value;
        }
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
        auto lock_ = lock(index);
        // if (!lock_->newly_allocated_thus_no_cow) {
            // dirty_ = true;
        // }

        if constexpr (std::is_same_v<Type, block_status>) {
            lock_->block_status = value;
        }
        else if constexpr (std::is_same_v<Type, block_type>) {
            if (value == COW_REDUNDANCY_BLOCK && lock_->block_type != COW_REDUNDANCY_BLOCK) {
                this->parent_fs_governor_->cfs_header_block.dec<allocated_non_cow_blocks>();
            } else if (value != COW_REDUNDANCY_BLOCK && lock_->block_type == COW_REDUNDANCY_BLOCK) {
                this->parent_fs_governor_->cfs_header_block.inc<allocated_non_cow_blocks>();
            }

            lock_->block_type = value;
        }
        else if constexpr (std::is_same_v<Type, block_type_cow>) {
            lock_->block_type_cow = value;
        }
        else if constexpr (std::is_same_v<Type, allocation_oom_scan_per_refresh_count>) {
            lock_->allocation_oom_scan_per_refresh_count = value;
        }
        else if constexpr (std::is_same_v<Type, newly_allocated_thus_no_cow>) {
            lock_->newly_allocated_thus_no_cow = value;
        }
        else if constexpr (std::is_same_v<Type, index_node_referencing_number>) {
            lock_->index_node_referencing_number = value;
        }
        else if constexpr (std::is_same_v<Type, block_checksum>) {
            lock_->block_checksum = value;
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
        auto lock_ = lock(index);
        // if (!lock_->newly_allocated_thus_no_cow) {
            // dirty_ = true;
        // }
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
    requires (std::is_same_v<Type, allocation_oom_scan_per_refresh_count>
        || std::is_same_v<Type, index_node_referencing_number>)
    void cfs_block_attribute_access_t::inc(const uint64_t index, const uint32_t value)
    {
        const auto lock_ = lock(index);
        if constexpr (std::is_same_v<Type, allocation_oom_scan_per_refresh_count>) {
            if ((lock_->allocation_oom_scan_per_refresh_count + value) <= 0xF) { // only 4 bits
                lock_->allocation_oom_scan_per_refresh_count += value;
            }
        }
        else if constexpr (std::is_same_v<Type, index_node_referencing_number>) {
            cfs_assert_simple((lock_->index_node_referencing_number + value) < 0xFFFF); // 16 bits
            lock_->index_node_referencing_number += value;
        }
        else {
            // already guarded in requires
        }
    }

    template<typename Type>
    requires (std::is_same_v<Type, allocation_oom_scan_per_refresh_count>
        || std::is_same_v<Type, index_node_referencing_number>)
    void cfs_block_attribute_access_t::dec(const uint64_t index, const uint32_t value)
    {
        const auto lock_ = lock(index);
        if constexpr (std::is_same_v<Type, allocation_oom_scan_per_refresh_count>) {
            if (lock_->allocation_oom_scan_per_refresh_count >= value) {
                lock_->allocation_oom_scan_per_refresh_count -= value;
            }
        }
        else if constexpr (std::is_same_v<Type, index_node_referencing_number>) {
            if (lock_->index_node_referencing_number >= value) {
                lock_->index_node_referencing_number -= value;
            }
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
            cfs_journaling_t * journal);

        /// allocate a new block
        /// @return New block index
        /// @throws cfs::error::no_more_free_spaces Space ran out
        [[nodiscard]] uint64_t allocate();

        /// deallocate a block
        /// @param index Block index
        void deallocate(uint64_t index);

        /// dump bitmap data
        [[nodiscard]] std::vector<uint8_t> dump_bitmap_data() const { return bitmap_->dump(); }

        /// get allocation status of a block
        [[nodiscard]] bool blk_at(const uint64_t index) const { return bitmap_->get_bit(index); }
    };

    template < typename F> concept Allocator_ = requires(F f, const uint8_t c) { { std::invoke(f, c) } -> std::same_as<uint64_t>; };
    template < typename F> concept Deallocator_ = requires(F f, uint64_t index) {
        { std::invoke(f, index) } -> std::same_as<void>;
    };

    class inode_t;
    class cfs_inode_service_t : protected cfs_inode_t
    {
        std::vector < uint8_t > before_;
        std::mutex mutex_;

    protected:
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

        class page_locker_t
        {
            filesystem::guard lock_;
            cfs_inode_service_t * parent_;
            std::vector < uint8_t > before_;
            const uint64_t index_;

        public:
            const filesystem::guard * operator->() const { return &lock_; }

            /**
             * Automatic page lock
             * @param index Page index
             * @param fs Filesystem manager
             * @param parent Service parent
             */
            page_locker_t(uint64_t index, filesystem * fs, cfs_inode_service_t * parent);

            /// page destructor, automatically commit checksum changes
            ~page_locker_t();
        };

        friend class page_locker_t;

        /// lock data block ID
        /// @param index data block ID
        /// @param linker Linker statement flag. Set to false and attempt to read a pointer will cause error
        /// @return page lock
        [[nodiscard]] page_locker_t lock_page(uint64_t index, bool linker = false);

        /// copy-on-write for one block
        /// @param index Block index
        /// @param linker Linker statement flag. Set to false and attempt to read a pointer will cause error
        /// @return Redundancy block index
        uint64_t copy_on_write(uint64_t index, bool linker = false);

        /// Linearize all blocks by st_size
        /// @return linearized pointers in std::vector <uint64_t> * 3 struct
        [[nodiscard]] linearized_block_t linearize_all_blocks();

        /// calculate how many pointers for each level
        /// @return pointers required for each level for this particular size
        [[nodiscard]] linearized_block_descriptor_t size_to_linearized_block_descriptor(uint64_t size) const;

        /// Smart allocator and deallocator
        /// @tparam FuncAlloc Allocator
        /// @tparam FuncDealloc Deallocator
        template < Allocator_ FuncAlloc, Deallocator_ FuncDealloc >
        class smart_reallocate_t {
        public:
            FuncAlloc alloc_; // alloc call back
            FuncDealloc dealloc_; // dealloc call back
            uint64_t block_index_ = UINT64_MAX; // block index, inited or allocated
            bool control_ = true; // is this block under SA's control, if not SA performs nothing
            bool allocated_by_smart_ = false; // is this blocked allocated by SA or inited from external source
            uint8_t block_type_ = STORAGE_BLOCK; // filled info when allocation, not synced from attribute table

            /// Create a smart allocator
            /// @param alloc Allocator
            /// @param dealloc Deallocator
            /// @param block_index initialized block index (for deallocation)
            /// @param control If this block is under smart allocator's control, if not SA does nothing
            /// @param block_type filled info in attribute table
            explicit smart_reallocate_t(FuncAlloc alloc, FuncDealloc dealloc, uint64_t block_index, bool control, uint8_t block_type);

            /// Blank allocator
            /// @param alloc Allocator
            /// @param dealloc Deallocator
            /// @param block_type filled info in attribute table
            smart_reallocate_t(FuncAlloc alloc, FuncDealloc dealloc, const uint8_t block_type)
            : alloc_(alloc), dealloc_(dealloc), block_type_(block_type) {
                if (control_) { block_index_ = alloc(block_type_); allocated_by_smart_ = true; }
            }

            ~smart_reallocate_t() {
                if (control_ && block_index_ != 0) { dealloc_(block_index_); }
            }
        };

        /// Reallocate blocks by descriptor table
        /// @param descriptor Descriptor table, generated by size_to_linearized_block_descriptor()
        /// @return Allocation map
        allocation_map_t reallocate_linearized_block_by_descriptor(const linearized_block_descriptor_t & descriptor);

        /// Write allocation map to inode indexes
        /// @param descriptor allocation map, generated from reallocate_linearized_block_by_descriptor() or your own
        void commit_from_linearized_block(allocation_map_t descriptor);

        /// basically, commit_from_linearized_block(reallocate_linearized_block_by_descriptor(desc))
        /// @param descriptor descriptor table
        void commit_from_block_descriptor(const linearized_block_descriptor_t & descriptor);

        /// resize this inode
        /// @param new_size New size
        void resize_unblocked(uint64_t new_size);

        /// write to inode data
        /// write automatically resizes when offset+size > st_size, but will not shrink
        /// you have to call resize(0) to shrink the inode
        /// @param data src
        /// @param size write size
        /// @param offset write offset
        /// @return size written
        uint64_t write_unblocked(const char * data, uint64_t size, uint64_t offset, bool hole_write = false);

    public:
        /// Create a low level inode service routine
        /// @param index Inode index
        /// @param parent_fs_governor
        /// @param block_manager
        /// @param journal
        /// @param block_attribute
        cfs_inode_service_t(
            uint64_t index,
            filesystem * parent_fs_governor,
            cfs_block_manager_t * block_manager,
            cfs_journaling_t * journal,
            cfs_block_attribute_access_t * block_attribute);

        ~cfs_inode_service_t();

        /// Read inode data
        /// @param data dest
        /// @param size read size
        /// @param offset read offset
        /// @return size read
        uint64_t read(char * data, uint64_t size, uint64_t offset);

        /// write to inode data
        /// write automatically resizes when offset+size > st_size, but will not shrink
        /// you have to call resize(0) to shrink the inode
        /// @param data src
        /// @param size write size
        /// @param offset write offset
        /// @return size written
        uint64_t write(const char * data, uint64_t size, uint64_t offset) {
            std::lock_guard<std::mutex> lock_guard_(mutex_);
            return write_unblocked(data, size, offset);
        }

        // !!! The following are metadata editing functions that should be called from inode_t
        // to create copy-on-write redundancies, which, inode service routine is incapable of doing !!!

        /// resize this inode
        /// @param new_size New size
        void resize(uint64_t new_size);

        void chdev(dev_t dev);                // change st_dev
        void chrdev(dev_t dev);             // change st_rdev
        void chmod(mode_t mode);               // change st_mode
        void chown(uid_t uid, gid_t gid);       // change st_uid, st_gid
        void set_atime(timespec st_atim);   // change st_atim
        void set_ctime(timespec st_ctim);   // change st_ctim
        void set_mtime(timespec st_mtim);   // change st_mtim

        /// get struct stat
        [[nodiscard]] struct stat get_stat ();

        friend class inode_t;
        friend class dentry_t;
    };

    template<Allocator_ FuncAlloc, Deallocator_ FuncDealloc>
    cfs_inode_service_t::smart_reallocate_t<FuncAlloc, FuncDealloc>::
    smart_reallocate_t(FuncAlloc alloc, FuncDealloc dealloc,
        const uint64_t block_index,
        const bool control,
        const uint8_t block_type)
    : alloc_(alloc), dealloc_(dealloc), block_index_(block_index), control_(control), block_type_(block_type) { }
}

#endif //CFS_CFSBASICCOMPONENTS_H