#ifndef CFS_CFSBASICCOMPONENTS_H
#define CFS_CFSBASICCOMPONENTS_H

#include "cfs.h"
#include "smart_block_t.h"

namespace cfs
{
    class cfs_journaling_t
    {
        cfs::filesystem * parent_fs_governor_;
        const uint64_t journal_start;
        const uint64_t journal_end;
        const uint64_t block_size;
        const uint64_t body_size = 0;

        char * journal_raw_buffer;
        char * journal_body;
        struct journal_header_t {
            uint64_t write_off;
            uint64_t flipped:1;
            uint64_t _reserved_:63;
        };

        journal_header_t * journal_header; // start of the ring
        journal_header_t * journal_header_cow; // end of the ring

        void write_to_ring(void * buffer, const uint64_t size)
        {

        }

    public:
        explicit cfs_journaling_t(cfs::filesystem * parent_fs_governor) :
            parent_fs_governor_(parent_fs_governor),
            journal_start(parent_fs_governor->static_info_.journal_start),
            journal_end(parent_fs_governor->static_info_.journal_end),
            block_size(parent_fs_governor->static_info_.block_size)
        {
            for (auto i = parent_fs_governor->static_info_.journal_start; i < parent_fs_governor->static_info_.journal_end; i++) {
                parent_fs_governor->bitlocker_.lock(i);
            }

            journal_raw_buffer = parent_fs_governor->file_.data() + journal_start * block_size;
            journal_body = journal_raw_buffer + sizeof(journal_header_t);
            journal_header = (journal_header_t*)journal_raw_buffer;
            journal_header_cow = (journal_header_t*)(parent_fs_governor->file_.data() + journal_end * block_size - sizeof(journal_header_t));
            *(uint64_t*)&body_size = (journal_end - journal_start) * block_size - (sizeof(journal_header_t) * 2);
        }

        ~cfs_journaling_t()
        {
            for (auto i = parent_fs_governor_->static_info_.journal_start; i < parent_fs_governor_->static_info_.journal_end; i++)
            {
                parent_fs_governor_->bitlocker_.unlock(i);
            }
        }

        void push_action(uint64_t action,
            const uint64_t action_param0,
            const uint64_t action_param1,
            const uint64_t action_param2,
            const uint64_t action_param3,
            const uint64_t action_param4)
        {

        }

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

    public:
        explicit cfs_bitmap_block_mirroring_t(cfs::filesystem * parent_fs_governor);

        /// Get bit at the specific location
        /// @param index Bit Index
        /// @return The bit at the specific location
        /// @throws cfs::error::assertion_failed Out of bounds
        bool get_bit(const uint64_t index);

        /// Set the bit at the specific location
        /// @param index Bit Index
        /// @param new_bit The new bit value
        /// @return NONE
        /// @throws cfs::error::assertion_failed Out of bounds
        void set_bit(const uint64_t index, const bool new_bit);
    };
}

#endif //CFS_CFSBASICCOMPONENTS_H