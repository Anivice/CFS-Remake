#ifndef CFS_CFSBASICCOMPONENTS_H
#define CFS_CFSBASICCOMPONENTS_H

#include "cfs.h"
#include "smart_block_t.h"
#include "generalCFSbaseError.h"

make_simple_error_class(no_more_free_spaces)

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
        [[nodiscard]] uint64_t dump_crc64() const;
    };

    class cfs_bitmap_block_mirroring_t
    {
        cfs_bitmap_singular_t mirror1;
        cfs_bitmap_singular_t mirror2;
        cfs::filesystem * parent_fs_governor_;
        cfs_journaling_t * journal_;

        std::atomic_int64_t last_get = -1;

        std::map < uint64_t, bool > bit_cache_;
        std::map < uint64_t, uint64_t > access_counter_;
        std::mutex mutex_;

    public:
        explicit cfs_bitmap_block_mirroring_t(cfs::filesystem * parent_fs_governor, cfs_journaling_t * journal);

        /// Add index into cache pool
        /// @param index index
        /// @param new_bit New bit
        void add_cache(uint64_t index, bool new_bit);

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

        // std::map < uint64_t, bool > debug_map_;
    };

    class cfs_block_attribute_access_t {
        filesystem * parent_fs_governor_;
        cfs_journaling_t * journal_;
    public:
        explicit cfs_block_attribute_access_t(filesystem * parent_fs_governor, cfs_journaling_t * journal);

        /// get the attribute at provided index
        /// @param index Block index
        /// @return block attribute
        [[nodiscard]] cfs_block_attribute_t get(uint64_t index);

        /// get the attribute at provided index
        /// @param index Block index
        /// @param attr New attribute
        void set(uint64_t index, cfs_block_attribute_t attr);
    };

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

    class cfs_copy_on_write_data_block_t
    {
        filesystem * parent_fs_governor_;
        cfs_block_manager_t * block_manager_;
        cfs_journaling_t * journal_;
        cfs_block_attribute_access_t * block_attribute_;
        std::map < uint64_t, std::mutex > inode_locks;

    public:
        explicit cfs_copy_on_write_data_block_t(
                filesystem * parent_fs_governor,
                cfs_block_manager_t * block_manager,
                cfs_journaling_t * journal,
                cfs_block_attribute_access_t * block_attribute
            )
        :
        parent_fs_governor_(parent_fs_governor),
        block_manager_(block_manager),
        journal_(journal),
        block_attribute_(block_attribute)
        { }

    private:
        [[nodiscard]] auto lock_page(const uint64_t index) const
            { return parent_fs_governor_->lock(index); }
        [[nodiscard]] auto lock_page(const uint64_t start, const uint64_t end) const
            { return parent_fs_governor_->lock(start, end); }

    public:

        /// copy-on-write for one block
        /// @param index Block index
        /// @return Redundancy block index
        uint64_t copy_on_write(const uint64_t index)
        {
            if (!block_attribute_->get(index).newly_allocated_thus_no_cow)
            {
                bool success = true;
                g_transaction(journal_, success, GlobalTransaction_CreateRedundancy, index);
                const auto new_block = block_manager_->allocate();
                const auto new_ = lock_page(new_block);
                const auto old_ = lock_page(index);
                std::memcpy(new_.data(), old_.data(), old_.size());
                return new_block;
            }

            return -1;
        }
    };

    class cfs_inode_service_t : protected cfs_inode_t
    {
        filesystem::guard inode_effective_lock;
        filesystem * parent_fs_governor_;
        cfs_block_manager_t * block_manager_;
        cfs_journaling_t * journal_;
        cfs_block_attribute_access_t * block_attribute_;
        cfs_copy_on_write_data_block_t * cow_;
        const uint64_t block_size_;
        const uint64_t block_index_;

    public:
        cfs_inode_service_t(
            uint64_t index,
            filesystem * parent_fs_governor,
            cfs_block_manager_t * block_manager,
            cfs_journaling_t * journal,
            cfs_block_attribute_access_t * block_attribute,
            cfs_copy_on_write_data_block_t * cow);

        uint64_t read(char * data, uint64_t size, uint64_t offset);
        uint64_t write(const char * data, uint64_t size, uint64_t offset);

        void chdev(int dev);
        void chrdev(dev_t dev);
        void chmod(int mode);
        void chown(int uid, int gid);
        void set_atime(timespec st_atim);
        void set_ctime(timespec st_ctim);
        void set_mtime(timespec st_mtim);
    };

    class cfs_directory_t : public cfs_inode_service_t {
    public:
        cfs_directory_t(
            uint64_t index,
            filesystem * parent_fs_governor,
            cfs_block_manager_t * block_manager,
            cfs_journaling_t * journal,
            cfs_block_attribute_access_t * block_attribute,
            cfs_copy_on_write_data_block_t * cow);

        /// create an inode under current directory
        cfs_inode_service_t make_inode(const std::string & name);
    };
}

#endif //CFS_CFSBASICCOMPONENTS_H