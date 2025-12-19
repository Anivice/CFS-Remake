#ifndef CFS_CFSBASICCOMPONENTS_H
#define CFS_CFSBASICCOMPONENTS_H

#include "cfs.h"
#include "smart_block_t.h"

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

        // std::map < uint64_t, bool > debug_map_;
    };

    class cfs_block_attribute_access_t {
        filesystem * parent_fs_governor_;
        cfs_journaling_t * journal_;
    public:
        explicit cfs_block_attribute_access_t(filesystem * parent_fs_governor, cfs_journaling_t * journal)
        : parent_fs_governor_(parent_fs_governor), journal_(journal)
        {
        }

        /// get the attribute at provided index
        /// @param index Block index
        /// @return block attribute
        [[nodiscard]] cfs_block_attribute_t get(const uint64_t index)
        {
            const auto offset = index * sizeof(cfs_block_attribute_t);
            const auto in_page_offset = offset % parent_fs_governor_->static_info_.block_size;
            const auto page = offset / parent_fs_governor_->static_info_.block_size +
                parent_fs_governor_->static_info_.data_block_attribute_table_start;
            cfs_block_attribute_t ret {};
            const auto lock = parent_fs_governor_->lock(page);
            std::memcpy(&ret, lock.data() + in_page_offset, sizeof(ret));
            return ret;
        }

        /// get the attribute at provided index
        /// @param index Block index
        /// @param attr New attribute
        void set(const uint64_t index, const cfs_block_attribute_t attr)
        {
            const auto offset = index * sizeof(cfs_block_attribute_t);
            const auto in_page_offset = offset % parent_fs_governor_->static_info_.block_size;
            const auto page = offset / parent_fs_governor_->static_info_.block_size +
                parent_fs_governor_->static_info_.data_block_attribute_table_start;
            cfs_block_attribute_t original {};
            const auto lock = parent_fs_governor_->lock(page);
            std::memcpy(&original, lock.data() + in_page_offset, sizeof(original));
            journal_->push_action(FilesystemAttributeModification,
                *reinterpret_cast<uint32_t *>(&original),
                *reinterpret_cast<const uint32_t *>(&attr),
                index);
            std::memcpy(lock.data() + in_page_offset, &attr, sizeof(attr));
            journal_->push_action(ActionFinishedAndNoExceptionCaughtDuringTheOperation);
        }
    };

    class cfs_block_manager_t {
        cfs_bitmap_block_mirroring_t * bitmap_;
        filesystem::cfs_header_block_t * header_;

    public:
        cfs_block_manager_t(cfs_bitmap_block_mirroring_t * bitmap, filesystem::cfs_header_block_t * header)
            : bitmap_(bitmap), header_(header) { }

    };
}

#endif //CFS_CFSBASICCOMPONENTS_H